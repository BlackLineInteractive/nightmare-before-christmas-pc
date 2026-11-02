/*
 * PC audio for default.xbe: replaces the Xbox XACT engine (and the
 * DirectSound under it, which drives the MCPX APU directly).
 *
 * The game's own banks are played: wave banks (.xwb, "WBND" version 3:
 * Xbox ADPCM or 16-bit PCM, in memory or streamed from the disc files) and
 * sound banks (.xsb, "SDBK" version 11: cue -> sound -> tracks -> events).
 * A mixer thread resamples every voice to 48 kHz stereo and feeds waveOut.
 *
 * Signatures are the XDK 5849 C wrappers (pThis first); see AUDIO_TO_PC in
 * tools/pc_render.py for the addresses. Not done: 3D positioning (sound
 * sources only carry a volume), effects (reverb image), Bink's movie sound.
 */
#include "win32.h"

#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005)

IMPORT HANDLE WINAPI CreateEventA(void *sa, BOOL manual, BOOL initial,
                                  const char *name);
IMPORT u32 WINAPI WaitForSingleObject(HANDLE h, u32 ms);
IMPORT BOOL WINAPI ReadFile(HANDLE h, void *p, u32 n, u32 *done, void *ov);
IMPORT BOOL WINAPI GetOverlappedResult(HANDLE h, void *ov, u32 *done,
                                       BOOL wait);
IMPORT u32 WINAPI GetLastError(void);
IMPORT void WINAPI InitializeCriticalSection(void *cs);
IMPORT void WINAPI EnterCriticalSection(void *cs);
IMPORT void WINAPI LeaveCriticalSection(void *cs);

/* winmm */
typedef struct {
  u16 tag, channels;
  u32 rate, bytes_per_sec;
  u16 align, bits, extra;
} WAVEFORMATEX;
typedef struct WAVEHDR {
  void *data;
  u32 len, recorded, user, flags, loops;
  struct WAVEHDR *next;
  u32 reserved;
} WAVEHDR;
IMPORT u32 WINAPI waveOutOpen(HANDLE *h, u32 dev, const WAVEFORMATEX *fmt,
                              u32 cb, u32 inst, u32 flags);
IMPORT u32 WINAPI waveOutPrepareHeader(HANDLE h, WAVEHDR *hdr, u32 size);
IMPORT u32 WINAPI waveOutWrite(HANDLE h, WAVEHDR *hdr, u32 size);

static u32 g_audio_cs[8]; /* CRITICAL_SECTION: game threads vs. mixer */
static void lock(void) { EnterCriticalSection(g_audio_cs); }
static void unlock(void) { LeaveCriticalSection(g_audio_cs); }

static u32 g_rand = 12345;
static u32 rnd(void) {
  g_rand = g_rand * 1103515245u + 12345u;
  return g_rand >> 16;
}

/* 2^x, good to ~0.1% (volumes, pitch) */
static float exp2f_(float x) {
  if (x < -30)
    return 0;
  if (x > 30)
    x = 30;
  int i = (int)x;
  if ((float)i > x)
    i--;
  float f = x - (float)i;
  float p = 1.0f + f * (0.693147f +
                        f * (0.240227f + f * (0.0555041f + f * 0.0096181f)));
  while (i > 0) {
    p *= 2;
    i--;
  }
  while (i < 0) {
    p *= 0.5f;
    i++;
  }
  return p;
}
/* DirectSound volume (1/100 dB) -> gain */
static float db100_gain(s32 v) {
  return v >= 0 ? 1.0f : exp2f_((float)v * (3.321928f / 2000.0f));
}

static int name_eq(const char *a, const char *b) {
  for (int i = 0; i < 16; i++) {
    char x = a[i], y = b[i];
    if (x >= 'a' && x <= 'z')
      x -= 32;
    if (y >= 'a' && y <= 'z')
      y -= 32;
    if (x != y)
      return 0;
    if (!x)
      return 1;
  }
  return 1;
}

/* ------------------------------------------------------------ wave banks */

#define WB_MAX 64
typedef struct {
  u32 magic; /* 'WBND' while registered */
  char name[16];
  const u8 *mem; /* in-memory bank, or */
  HANDLE file;
  u32 file_base; /* streamed from a file */
  u32 count, data_off;
  u8 *meta; /* count x 24: flags/duration, format, play region, loop region */
} WaveBank;
static WaveBank g_wb[WB_MAX];

static WaveBank *wb_alloc(void) {
  for (int i = 0; i < WB_MAX; i++)
    if (!g_wb[i].magic)
      return &g_wb[i];
  return NULL;
}

static int wb_parse(WaveBank *w, const u8 *hdr, u32 hdr_len) {
  const u32 *h = (const u32 *)hdr;
  if (hdr_len < 0x50 || h[0] != 0x444E4257 || h[1] != 3)
    return 0; /* "WBND" v3 */
  u32 meta_off = h[4], data_off = h[8];
  u32 count = *(const u32 *)(hdr + 0x2C), esize = *(const u32 *)(hdr + 0x40);
  if (esize != 24 || meta_off + count * 24 > hdr_len)
    return 0;
  memcpy(w->name, hdr + 0x30, 16);
  w->count = count;
  w->data_off = data_off;
  w->meta = HeapAlloc(GetProcessHeap(), 0, count * 24 + 1);
  memcpy(w->meta, hdr + meta_off, count * 24);
  w->magic = 0x444E4257;
  return 1;
}

static WaveBank *wb_find(const char *name) {
  for (int i = 0; i < WB_MAX; i++)
    if (g_wb[i].magic && name_eq(g_wb[i].name, name))
      return &g_wb[i];
  return NULL;
}

/* synchronous read from a (possibly overlapped) file handle at an offset */
static u32 file_read(HANDLE f, u32 off, void *p, u32 n) {
  static HANDLE ev;
  if (!ev)
    ev = CreateEventA(NULL, 1, 0, NULL);
  u32 ov[5] = {0, 0, off, 0, (u32)ev}, done = 0;
  if (!ReadFile(f, p, n, &done, ov)) {
    if (GetLastError() != 997 /* ERROR_IO_PENDING */)
      return 0;
    if (!GetOverlappedResult(f, ov, &done, 1))
      return 0;
  }
  return done;
}

/* ----------------------------------------------------------- sound banks */

#define SB_MAX 64
typedef struct {
  u32 magic; /* 'SDBK' while alive */
  const u8 *data;
  u32 size;
  u32 sounds, cues, wavebanks;
} SoundBank;
static SoundBank g_sb[SB_MAX];

static const u8 *sb_cue(SoundBank *s, u32 i) { return s->data + 0x38 + 20 * i; }
static const u8 *sb_sound(SoundBank *s, u32 i) {
  return s->data + 0x38 + 20 * s->cues + 20 * i;
}
static const char *sb_wb_name(SoundBank *s, u32 i) {
  return (const char *)s->data + *(const u32 *)(s->data + 8) + 16 * i;
}

/* ------------------------------------------------------- voices and cues */

typedef struct {
  u32 magic;
  s32 volume;
} Source;
#define SRC_MAX 256
static Source g_src[SRC_MAX];
static u32 g_nsrc;

#define CHUNK 0x9000 /* streamed read size: 1024 stereo ADPCM blocks */
typedef struct {
  int active;
  struct Cue *cue;
  WaveBank *wb;
  u32 fmt, ch, rate, adpcm,
      align; /* align: bytes per block (ADPCM) or frame (PCM) */
  u32 off, len, loop_start, loop_len; /* bytes, inside the bank's data */
  s32 loops;                          /* remaining, -1 = forever */
  u32 pos;                            /* bytes of the play region consumed */
  s16 buf[64 * 2];
  u32 nbuf, ibuf; /* decoded frames of the current block */
  s16 cur[2], prev[2];
  float frac, step, gain;
  u8 *stream;
  u32 s_off, s_len; /* streamed banks: raw bytes at s_off */
} Voice;
#define VOICE_MAX 48
static Voice g_voice[VOICE_MAX];

typedef struct {
  const u8 *ev;
  u32 left; /* events still to run */
  Voice *v;
  s32 vol;
  float pitch; /* from volume / pitch events */
} Track;
typedef struct Cue {
  u32 magic; /* 'CUE ' while alive */
  SoundBank *sb;
  u32 index;
  Source *src;
  int paused;
  u32 t;   /* samples played (48 kHz) */
  s32 delay; /* samples to wait before starting (lines sound up with the picture) */
  s32 vol; /* the sound's volume */
  Track tr[4];
  u32 ntr;
} Cue;
#define CUE_MAX 96
static Cue g_cue[CUE_MAX];

static const s16 g_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const s32 g_index_adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* one Xbox ADPCM block: per channel a 4-byte header (sample, step index),
 * then 8 rounds of 4 bytes per channel = 64 samples per channel (the
 * header sample only seeds the predictor, as in vgmstream's Xbox IMA) */
static void adpcm_block(const u8 *b, u32 ch, s16 *out) {
  for (u32 c = 0; c < ch; c++) {
    s32 pred = *(const s16 *)(b + 4 * c), idx = b[4 * c + 2];
    if (idx > 88)
      idx = 88;
    for (u32 r = 0; r < 8; r++) {
      const u8 *d = b + 4 * ch + (r * ch + c) * 4;
      for (u32 n = 0; n < 8; n++) {
        u32 nib = (d[n >> 1] >> ((n & 1) * 4)) & 15;
        s32 step = g_step[idx], diff = step >> 3;
        if (nib & 1)
          diff += step >> 2;
        if (nib & 2)
          diff += step >> 1;
        if (nib & 4)
          diff += step;
        pred += nib & 8 ? -diff : diff;
        if (pred > 32767)
          pred = 32767;
        else if (pred < -32768)
          pred = -32768;
        idx += g_index_adj[nib & 7];
        if (idx < 0)
          idx = 0;
        else if (idx > 88)
          idx = 88;
        out[(r * 8 + n) * ch + c] = (s16)pred;
      }
    }
  }
}

/* bytes of the bank data at [off, off + n) */
static const u8 *wave_bytes(Voice *v, u32 off, u32 n) {
  if (v->wb->mem)
    return v->wb->mem + v->wb->data_off + off;
  if (!v->stream)
    v->stream = HeapAlloc(GetProcessHeap(), 0, CHUNK);
  if (off < v->s_off || off + n > v->s_off + v->s_len) {
    u32 want = v->off + v->len - off;
    if (want > CHUNK)
      want = CHUNK - CHUNK % v->align;
    if (want < n)
      want = n;
    v->s_off = off;
    v->s_len = file_read(v->wb->file, v->wb->file_base + v->wb->data_off + off,
                         v->stream, want);
    if (v->s_len < n) {
      v->s_len = 0;
      return NULL;
    }
  }
  return v->stream + (off - v->s_off);
}

/* decode the next block / frame into v->buf; 0 at the end of the wave */
static int voice_refill(Voice *v) {
  if (v->pos + v->align > v->len) {
    if (v->loops == 0)
      return 0;
    if (v->loops > 0)
      v->loops--;
    v->pos = v->loop_len ? v->loop_start - v->loop_start % v->align : 0;
    if (v->pos + v->align > v->len)
      return 0;
  }
  const u8 *b = wave_bytes(v, v->off + v->pos, v->align);
  if (!b)
    return 0;
  if (v->adpcm) {
    adpcm_block(b, v->ch, v->buf);
    v->nbuf = 64;
  } else {
    memcpy(v->buf, b, v->align);
    v->nbuf = 1;
  }
  v->pos += v->align;
  v->ibuf = 0;
  return 1;
}

static void voice_stop(Voice *v) {
  v->active = 0;
  v->cue = NULL;
}

static void voice_start(Track *t, Cue *c, u32 wave, u32 bank, u32 loops) {
  if (bank >= c->sb->wavebanks)
    return;
  WaveBank *wb = wb_find(sb_wb_name(c->sb, bank));
  if (!wb || wave >= wb->count)
    return;
  if (t->v && t->v->cue == c)
    voice_stop(t->v);
  Voice *v = NULL;
  for (int i = 0; i < VOICE_MAX && !v; i++)
    if (!g_voice[i].active)
      v = &g_voice[i];
  if (!v)
    return;
  const u32 *m = (const u32 *)(wb->meta + wave * 24);
  u8 *keep = v->stream;
  memset(v, 0, sizeof *v);
  v->stream = keep;
  v->wb = wb;
  v->cue = c;
  v->fmt = m[1];
  v->adpcm = (m[1] & 3) == 1;
  v->ch = (m[1] >> 2) & 7;
  v->rate = (m[1] >> 5) & 0x3FFFFFF;
  if (!v->ch || v->ch > 2 || !v->rate)
    return;
  v->align = v->adpcm ? 36 * v->ch : 2 * v->ch;
  v->off = m[2];
  v->len = m[3];
  v->loop_start = m[4];
  v->loop_len = m[5];
  v->loops = loops == 0xFFFF ? -1 : (s32)loops;
  v->step = (float)v->rate / 48000.0f * t->pitch;
  v->s_len = 0;
  t->v = v;
  v->active = 1;
}

static void cue_event_loop(Cue *c) {
  for (u32 i = 0; i < c->ntr; i++) {
    Track *t = &c->tr[i];
    while (t->left) {
      u32 h = *(const u32 *)t->ev, type = h & 0xFF;
      if ((h >> 8) * 48 > c->t)
        break; /* timestamp in ms */
      const u16 *p = (const u16 *)(t->ev + 4);
      u32 size = 12;
      switch (type) {
      case 0x00:
        voice_start(t, c, p[2], p[3], p[1]);
        break;     /* play wave */
      case 0x04: { /* pitch (1/256 semitone) */
        s16 lo = (s16)p[2], hi = (s16)p[3];
        s32 v = hi > lo ? lo + (s32)(rnd() % (u32)(hi - lo + 1)) : lo;
        t->pitch = exp2f_((float)v / 3072.0f);
        size = 16;
        break;
      }
      case 0x05: { /* volume (1/100 dB) */
        s16 lo = (s16)p[2], hi = (s16)p[3];
        t->vol = hi > lo ? lo + (s32)(rnd() % (u32)(hi - lo + 1)) : lo;
        size = 16;
        break;
      }
      case 0x0C:
        size = 8;
        break;
      case 0x10: { /* play one of several waves */
        u32 n = (*(const u32 *)(t->ev + 4) - 8) / 4;
        const u8 *e = t->ev + 16 + 4 * (n ? rnd() % n : 0);
        if (n)
          voice_start(t, c, *(const u16 *)e, e[2], 0);
        size = 8 + *(const u32 *)(t->ev + 4);
        break;
      }
      case 0x0A:
        size = 8 + *(const u32 *)(t->ev + 4);
        break;
      default:
        t->left = 1;
        break; /* unknown: stop the track */
      }
      t->ev += size;
      t->left--;
    }
  }
}

static void cue_stop(Cue *c) {
  for (u32 i = 0; i < c->ntr; i++)
    if (c->tr[i].v && c->tr[i].v->cue == c)
      voice_stop(c->tr[i].v);
  c->magic = 0;
}

static int cue_alive(Cue *c) {
  for (u32 i = 0; i < c->ntr; i++)
    if (c->tr[i].left ||
        (c->tr[i].v && c->tr[i].v->cue == c && c->tr[i].v->active))
      return 1;
  return 0;
}

/* The game starts its sounds a few frames ahead of what is on screen (the
 * Xbox output path had latency this one lacks): cues wait PC_AUDIODELAY ms,
 * 100 by default. */
static s32 g_delay = 100 * 48;

static u32 g_cue_log;
static Cue *cue_play(SoundBank *sb, u32 index, Source *src) {
  if (sb && index < sb->cues && g_cue_log < 200) {
    g_cue_log++;
    log_str("[pc] audio: cue ");
    log_str((const char *)sb->data + *(const u32 *)(sb_cue(sb, index) + 4));
    log_str("\n");
  }
  if (!sb || index >= sb->cues)
    return NULL;
  u32 snd = *(const u16 *)(sb_cue(sb, index) + 2);
  if (snd >= sb->sounds)
    return NULL;
  Cue *c = NULL;
  for (int i = 0; i < CUE_MAX && !c; i++)
    if (!g_cue[i].magic)
      c = &g_cue[i];
  if (!c) { /* all busy: take the oldest-looking finished one */
    for (int i = 0; i < CUE_MAX && !c; i++)
      if (!cue_alive(&g_cue[i])) {
        cue_stop(&g_cue[i]);
        c = &g_cue[i];
      }
    if (!c)
      return NULL;
  }
  memset(c, 0, sizeof *c);
  c->magic = 0x20455543;
  c->delay = g_delay;
  c->sb = sb;
  c->index = index;
  c->src = src;
  const u8 *s = sb_sound(sb, snd);
  c->vol = *(const s16 *)(s + 4);
  u32 ntr = s[8], flags = s[11];
  if (flags & 8) { /* simple sound: one wave, no events */
    c->ntr = 1;
    c->tr[0].pitch = 1;
    voice_start(&c->tr[0], c, *(const u16 *)s, *(const u16 *)(s + 2), 0);
    return c;
  }
  u32 to = *(const u32 *)s;
  if (!to)
    return c;
  if (ntr > 4)
    ntr = 4;
  for (u32 i = 0; i < ntr; i++) {
    const u8 *te = sb->data + to + 4 * i;
    c->tr[i].left = te[0];
    c->tr[i].ev = sb->data + *(const u16 *)(te + 1);
    c->tr[i].pitch = 1;
  }
  c->ntr = ntr;
  cue_event_loop(c);
  return c;
}

#define MIX_FRAMES 1024

/* ------------------------------------------------- DirectSound buffers */

/* Bink plays a movie's sound through one looping DirectSound buffer whose
 * memory is its own (SetBufferData): it writes PCM ahead of the play
 * cursor and paces itself on GetCurrentPosition. */
typedef struct {
  u32 magic;           /* 'DSB ' while alive */
  u32 ch, bits, rate;
  u8 *data;
  u32 bytes, pos;      /* pos: play cursor, bytes */
  int playing, looping;
  s32 volume;
  float frac;
} DSBuf;
#define DSB_MAX 8
static DSBuf g_dsb[DSB_MAX];

static float g_master = 1.0f;

static void dsb_mix(DSBuf *b, float *a) {
  u32 frame = b->ch * (b->bits / 8);
  if (!b->data || !frame || b->bytes < frame)
    return;
  float g = db100_gain(b->volume) * g_master;
  float step = (float)b->rate / 48000.0f;
  for (u32 n = 0; n < MIX_FRAMES; n++) {
    const u8 *p = b->data + b->pos;
    float l, r;
    if (b->bits == 16) {
      l = (float)((const s16 *)p)[0] * (1.0f / 32768.0f);
      r = b->ch > 1 ? (float)((const s16 *)p)[1] * (1.0f / 32768.0f) : l;
    } else {
      l = ((float)p[0] - 128.0f) * (1.0f / 128.0f);
      r = b->ch > 1 ? ((float)p[1] - 128.0f) * (1.0f / 128.0f) : l;
    }
    a[2 * n] += l * g;
    a[2 * n + 1] += r * g;
    b->frac += step;
    while (b->frac >= 1.0f) {
      b->frac -= 1.0f;
      b->pos += frame;
      if (b->pos + frame > b->bytes) {
        b->pos = 0;
        if (!b->looping) {
          b->playing = 0;
          return;
        }
      }
    }
  }
}

/* ---------------------------------------------------------------- mixer */

#define MIX_BUFS 4
static s16 g_mixbuf[MIX_BUFS][MIX_FRAMES * 2];
static float g_acc[MIX_FRAMES * 2];

static void mix(s16 *out) {
  memset(g_acc, 0, sizeof g_acc);
  lock();
  for (int i = 0; i < CUE_MAX; i++)
    if (g_cue[i].magic && !g_cue[i].paused) {
      if (g_cue[i].delay > 0) {
        g_cue[i].delay -= MIX_FRAMES;
        continue;
      }
      g_cue[i].t += MIX_FRAMES;
      cue_event_loop(&g_cue[i]);
    }
  for (int i = 0; i < DSB_MAX; i++)
    if (g_dsb[i].magic && g_dsb[i].playing)
      dsb_mix(&g_dsb[i], g_acc);
  for (int i = 0; i < VOICE_MAX; i++) {
    Voice *v = &g_voice[i];
    if (!v->active)
      continue;
    Cue *c = v->cue;
    if (c && (c->paused || c->delay > 0))
      continue;
    Track *t = NULL;
    if (c)
      for (u32 k = 0; k < c->ntr; k++)
        if (c->tr[k].v == v)
          t = &c->tr[k];
    s32 cb = (c ? c->vol : 0) + (t ? t->vol : 0) +
             (c && c->src ? c->src->volume : 0);
    float g = db100_gain(cb) * g_master * (1.0f / 32768.0f);
    float *a = g_acc;
    for (u32 n = 0; n < MIX_FRAMES; n++) {
      while (v->frac >= 1.0f) {
        v->frac -= 1.0f;
        v->prev[0] = v->cur[0];
        v->prev[1] = v->cur[1];
        if (v->ibuf >= v->nbuf && !voice_refill(v)) {
          voice_stop(v);
          break;
        }
        s16 *s = v->buf + v->ibuf * v->ch;
        v->cur[0] = s[0];
        v->cur[1] = v->ch > 1 ? s[1] : s[0];
        v->ibuf++;
      }
      if (!v->active)
        break;
      float f = v->frac;
      a[2 * n] +=
          ((float)v->prev[0] + ((float)v->cur[0] - (float)v->prev[0]) * f) * g;
      a[2 * n + 1] +=
          ((float)v->prev[1] + ((float)v->cur[1] - (float)v->prev[1]) * f) * g;
      v->frac += v->step;
    }
  }
  unlock();
  for (u32 n = 0; n < MIX_FRAMES * 2; n++) {
    float s = g_acc[n];
    if (s > 1.0f)
      s = 1.0f;
    else if (s < -1.0f)
      s = -1.0f;
    out[n] = (s16)(s * 32767.0f);
  }
  /* PC_AUDIODUMP=1: the mixed output also goes to audio_dump.raw (48 kHz stereo s16) */
  static HANDLE dump = (HANDLE)1;
  if (dump == (HANDLE)1) {
    char e[4];
    dump = GetEnvironmentVariableA("PC_AUDIODUMP", e, 4) > 0
               ? CreateFileA("audio_dump.raw", 0x40000000, 1, NULL, 2, 0x80, NULL)
               : NULL;
  }
  if (dump) {
    u32 w;
    WriteFile(dump, out, MIX_FRAMES * 4, &w, NULL);
  }
}

static u32 WINAPI mixer_thread(void *arg) {
  HANDLE ev = CreateEventA(NULL, 0, 0, NULL), wo = NULL;
  WAVEFORMATEX f = {1, 2, 48000, 48000 * 4, 4, 16, 0};
  if (waveOutOpen(&wo, 0xFFFFFFFF, &f, (u32)ev, 0,
                  0x50000 /* CALLBACK_EVENT */)) {
    log_str("[pc] audio: waveOutOpen failed, no sound\n");
    return 0;
  }
  static WAVEHDR hdr[MIX_BUFS];
  for (int i = 0; i < MIX_BUFS; i++) {
    hdr[i].data = g_mixbuf[i];
    hdr[i].len = sizeof g_mixbuf[i];
    waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
    mix(g_mixbuf[i]);
    waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
  }
  log_str("[pc] audio: 48 kHz stereo through waveOut\n");
  for (;;) {
    WaitForSingleObject(ev, 100);
    for (int i = 0; i < MIX_BUFS; i++)
      if (hdr[i].flags & 1 /* WHDR_DONE */) {
        hdr[i].flags &= ~1u;
        mix(g_mixbuf[i]);
        waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
      }
  }
}

static void audio_init(void) {
  static int done;
  if (done)
    return;
  done = 1;
  InitializeCriticalSection(g_audio_cs);
  char b[4];
  if (GetEnvironmentVariableA("PC_NOSOUND", b, 4) > 0)
    return;
  char d[8];
  u32 n = GetEnvironmentVariableA("PC_AUDIODELAY", d, 8);
  if (n > 0 && n < 8) {
    s32 ms = 0;
    for (u32 i = 0; i < n && d[i] >= '0' && d[i] <= '9'; i++)
      ms = ms * 10 + d[i] - '0';
    g_delay = ms * 48;
  }
  CreateThread(NULL, 0, mixer_thread, NULL, 0, NULL);
}

/* ------------------------------------------------------------ XACT API */

static u32 g_engine[16];

/* XACTEngineCreate(pParams, ppEngine) */
HRESULT WINAPI pc_XACTEngineCreate(const void *params, void **pp) {
  audio_init();
  *pp = g_engine;
  return S_OK;
}
void WINAPI pc_XACTEngineDoWork(void) {}

/* IXACTEngine_CreateSoundSource(pEngine, dwFlags, ppSoundSource) */
HRESULT WINAPI pc_XACT_CreateSoundSource(void *e, u32 flags, void **pp) {
  lock();
  Source *s = &g_src[g_nsrc++ % SRC_MAX];
  s->magic = 0x20435253;
  s->volume = 0;
  unlock();
  *pp = s;
  return S_OK;
}

/* IXACTEngine_CreateSoundBank(pEngine, pvBuffer, dwSize, ppSoundBank): the
 * buffer stays the game's for the bank's lifetime */
HRESULT WINAPI pc_XACT_CreateSoundBank(void *e, const u8 *buf, u32 size,
                                       void **pp) {
  lock();
  SoundBank *s = NULL;
  for (int i = 0; i < SB_MAX && !s; i++)
    if (!g_sb[i].magic)
      s = &g_sb[i];
  if (s && size >= 0x38 && *(const u32 *)buf == 0x4B424453 /* SDBK */) {
    s->data = buf;
    s->size = size;
    s->sounds = *(const u16 *)(buf + 0x1C);
    s->cues = *(const u16 *)(buf + 0x1E);
    s->wavebanks = *(const u16 *)(buf + 0x22);
    s->magic = 0x4B424453;
  } else
    s = NULL;
  unlock();
  if (!s) {
    log_str("[pc] audio: bad sound bank\n");
    *pp = g_engine;
    return S_OK;
  }
  *pp = s;
  return S_OK;
}

/* Wave banks are "prepared" at once: each registration queues the
 * WAVEBANKPREPARED notification the game waits for before it goes on
 * (sound init thread, 0x1acf31). */
#define XACTNOTIFICATIONTYPE_WAVEBANKPREPARED 3
static void *g_prepared[16];
static u32 g_prepared_head, g_prepared_tail;

static void *prepared(WaveBank *w) {
  void *r = w ? (void *)w : (void *)g_engine;
  g_prepared[g_prepared_tail++ & 15] = r;
  return r;
}

/* IXACTEngine_RegisterWaveBank(pEngine, pvData, dwSize, ppWaveBank) */
HRESULT WINAPI pc_XACT_RegisterWaveBank(void *e, const u8 *data, u32 size,
                                        void **pp) {
  lock();
  WaveBank *w = wb_alloc();
  if (w) {
    memset(w, 0, sizeof *w);
    if (wb_parse(w, data, size))
      w->mem = data;
    else
      w = NULL;
  }
  *pp = prepared(w);
  unlock();
  return S_OK;
}

/* IXACTEngine_RegisterStreamedWaveBank(pEngine, pParams, ppWaveBank):
 * params +0 file handle, +4 offset of the bank in the file */
HRESULT WINAPI pc_XACT_RegisterStreamedWaveBank(void *e, const u32 *params,
                                                void **pp) {
  static u8 hdr[0x2000];
  lock();
  WaveBank *w = wb_alloc();
  if (w) {
    memset(w, 0, sizeof *w);
    u32 n = file_read((HANDLE)params[0], params[1], hdr, sizeof hdr);
    if (wb_parse(w, hdr, n)) {
      w->file = (HANDLE)params[0];
      w->file_base = params[1];
    } else
      w = NULL;
  }
  *pp = prepared(w);
  unlock();
  return S_OK;
}

/* IXACTEngine_UnRegisterWaveBank(pEngine, pWaveBank) */
HRESULT WINAPI pc_XACT_UnRegisterWaveBank(void *e, WaveBank *w) {
  if (w < g_wb || w >= g_wb + WB_MAX)
    return S_OK;
  lock();
  for (int i = 0; i < VOICE_MAX; i++)
    if (g_voice[i].active && g_voice[i].wb == w)
      voice_stop(&g_voice[i]);
  if (w->meta)
    HeapFree(GetProcessHeap(), 0, w->meta);
  w->magic = 0;
  w->meta = NULL;
  unlock();
  return S_OK;
}

/* GetNotification(pEngine, pNotificationDesc, pNotification): +0 WORD type, +4
 * pWaveBank */
HRESULT WINAPI pc_XACT_GetNotification(void *e, const void *desc, void *out) {
  if (g_prepared_head == g_prepared_tail)
    return E_FAIL; /* none pending */
  u8 *n = out;
  *(u16 *)n = XACTNOTIFICATIONTYPE_WAVEBANKPREPARED;
  *(u16 *)(n + 2) = 0;
  *(void **)(n + 4) = g_prepared[g_prepared_head++ & 15];
  return S_OK;
}
HRESULT WINAPI pc_XACT_Out2(void *e, u32 *out) {
  if (out)
    *out = 0;
  return S_OK;
}

static SoundBank *as_sb(void *p) {
  SoundBank *s = p;
  return s >= g_sb && s < g_sb + SB_MAX && s->magic ? s : NULL;
}
static Cue *as_cue(void *p) {
  Cue *c = p;
  return c >= g_cue && c < g_cue + CUE_MAX && c->magic ? c : NULL;
}
static Source *as_src(void *p) {
  Source *s = p;
  return s >= g_src && s < g_src + SRC_MAX ? s : NULL;
}

/* IXACTSoundBank_GetSoundCueIndexFromFriendlyName(pSoundBank, pFriendlyName,
 * pdwCueIndex) */
HRESULT WINAPI pc_XACT_GetCueIndex(void *p, const char *name, u32 *index) {
  SoundBank *s = as_sb(p);
  *index = 0xFFFFFFFF;
  if (!s)
    return E_FAIL;
  for (u32 i = 0; i < s->cues; i++) {
    const char *n = (const char *)s->data + *(const u32 *)(sb_cue(s, i) + 4);
    int k = 0;
    while (n[k] && n[k] == name[k])
      k++;
    if (!n[k] && !name[k]) {
      *index = i;
      return S_OK;
    }
  }
  return E_FAIL;
}

/* IXACTSoundBank_Play / PlayEx(pSoundBank, pParams, ppSoundCue):
 * params +0 flags, +4 cue index, +8 sound source */
static HRESULT play(void *p, const u32 *params, void **out) {
  lock();
  Cue *c = cue_play(as_sb(p), params[1], as_src((void *)params[2]));
  unlock();
  if (out)
    *out = c;
  return c ? S_OK : E_FAIL;
}
HRESULT WINAPI pc_XACT_SB_Play(void *p, const u32 *params, void **out) {
  return play(p, params, out);
}
HRESULT WINAPI pc_XACT_SB_PlayEx(void *p, const u32 *params, void **out) {
  return play(p, params, out);
}

/* IXACTSoundBank_Stop(pSoundBank, dwCueIndex, dwFlags, pSoundCue): one
 * cue, or every playing instance of a cue index when pSoundCue is NULL */
HRESULT WINAPI pc_XACT_SB_Stop(void *p, u32 index, u32 flags, void *cue) {
  lock();
  Cue *c = as_cue(cue);
  if (c)
    cue_stop(c);
  else if (index != 0xFFFFFFFF)
    for (int i = 0; i < CUE_MAX; i++)
      if (g_cue[i].magic && g_cue[i].sb == p && g_cue[i].index == index)
        cue_stop(&g_cue[i]);
  unlock();
  return S_OK;
}

/* IXACTSoundBank pause (pSoundBank, pSoundCue, bPause) */
HRESULT WINAPI pc_XACT_SB_Pause(void *p, void *cue, u32 pause) {
  lock();
  Cue *c = as_cue(cue);
  if (c)
    c->paused = pause != 0;
  unlock();
  return S_OK;
}

/* Release of a sound bank stops its cues */
HRESULT WINAPI pc_XACT_Release(void *p) {
  lock();
  SoundBank *s = as_sb(p);
  if (s) {
    for (int i = 0; i < CUE_MAX; i++)
      if (g_cue[i].magic && g_cue[i].sb == s)
        cue_stop(&g_cue[i]);
    s->magic = 0;
  }
  unlock();
  return S_OK;
}

/* IXACTSoundSource */
HRESULT WINAPI pc_XACT_Src_Release(void *s) { return S_OK; }
HRESULT WINAPI pc_XACT_Src_SetPosition(void *s, float x, float y, float z,
                                       u32 apply) {
  return S_OK;
}
HRESULT WINAPI pc_XACT_Src_2a(void *s, u32 a) { return S_OK; }
HRESULT WINAPI pc_XACT_Src_SetVolume(void *p, s32 v) {
  Source *s = as_src(p);
  if (s && v <= 0 && v >= -10000)
    s->volume = v;
  return S_OK;
}
HRESULT WINAPI pc_XACT_Src_1(void *s) { return S_OK; }
/* GetStatus: 1 while a voice of this source plays */
HRESULT WINAPI pc_XACT_Src_GetStatus(void *p, u32 *out) {
  u32 st = 0;
  lock();
  for (int i = 0; i < VOICE_MAX; i++)
    if (g_voice[i].active && g_voice[i].cue && g_voice[i].cue->src == p)
      st = 1;
  unlock();
  if (out)
    *out = st;
  return S_OK;
}

/* calls with nothing to hand back, by argument count */
HRESULT WINAPI pc_XACT_Nop1(void *a) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop2(void *a, void *b) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop3(void *a, void *b, void *c) { return S_OK; }

/* DirectSound (the parts Bink uses) */
static u32 g_dsound[16];
static DSBuf *as_dsb(void *p) { DSBuf *b = p; return b >= g_dsb && b < g_dsb + DSB_MAX && b->magic ? b : NULL; }

/* DirectSoundCreate(pGuid, ppDirectSound, pUnkOuter) */
HRESULT WINAPI pc_DirectSoundCreate(void *guid, void **pp, void *unk) {
  audio_init();
  *pp = g_dsound;
  return S_OK;
}
u32 WINAPI pc_DS_Release(void *ds) { return 0; }

/* IDirectSound_CreateSoundBuffer(pDS, pDesc, ppBuffer, pUnkOuter):
 * desc +0 size, +4 flags, +8 bytes, +12 format */
HRESULT WINAPI pc_DS_CreateSoundBuffer(void *ds, const u32 *desc, void **pp, void *unk) {
  const u8 *wfx = (const u8 *)desc[3];
  DSBuf *b = NULL;
  lock();
  for (int i = 0; i < DSB_MAX && !b; i++)
    if (!g_dsb[i].magic)
      b = &g_dsb[i];
  if (b && wfx) {
    memset(b, 0, sizeof *b);
    b->ch = *(const u16 *)(wfx + 2);
    b->rate = *(const u32 *)(wfx + 4);
    b->bits = *(const u16 *)(wfx + 14);
    b->magic = 0x20425344;
  } else
    b = NULL;
  unlock();
  *pp = b;
  return b ? S_OK : E_FAIL;
}
u32 WINAPI pc_DSB_Release(void *p) {
  DSBuf *b = as_dsb(p);
  if (b) { lock(); b->magic = 0; b->playing = 0; unlock(); }
  return 0;
}
HRESULT WINAPI pc_DSB_SetBufferData(void *p, void *data, u32 bytes) {
  DSBuf *b = as_dsb(p);
  if (b) { lock(); b->data = data; b->bytes = bytes; b->pos = 0; unlock(); }
  return S_OK;
}
/* Play(pBuffer, reserved, priority, flags: 1 = looping) */
HRESULT WINAPI pc_DSB_Play(void *p, u32 r1, u32 r2, u32 flags) {
  DSBuf *b = as_dsb(p);
  if (b) { b->looping = flags & 1; b->playing = 1; }
  return S_OK;
}
HRESULT WINAPI pc_DSB_Stop(void *p) {
  DSBuf *b = as_dsb(p);
  if (b) b->playing = 0;
  return S_OK;
}
HRESULT WINAPI pc_DSB_GetStatus(void *p, u32 *st) {
  DSBuf *b = as_dsb(p);
  *st = b && b->playing ? (b->looping ? 5 : 1) : 0;   /* PLAYING | LOOPING */
  return S_OK;
}
HRESULT WINAPI pc_DSB_SetCurrentPosition(void *p, u32 pos) {
  DSBuf *b = as_dsb(p);
  if (b) { lock(); b->pos = pos < b->bytes ? pos - pos % (b->ch * b->bits / 8) : 0; unlock(); }
  return S_OK;
}
/* GetCurrentPosition(pBuffer, pdwPlay, pdwWrite): the write cursor is
 * where the mixer reads next */
HRESULT WINAPI pc_DSB_GetCurrentPosition(void *p, u32 *play, u32 *write) {
  DSBuf *b = as_dsb(p);
  u32 pos = b ? b->pos : 0;
  if (play) *play = pos;
  if (write) *write = pos;
  return S_OK;
}
/* Lock(pBuffer, offset, bytes, ppv1, pn1, ppv2, pn2, flags: 2 = entire buffer) */
HRESULT WINAPI pc_DSB_Lock(void *p, u32 off, u32 n, void **p1, u32 *n1, void **p2, u32 *n2, u32 flags) {
  DSBuf *b = as_dsb(p);
  if (!b || !b->data) return E_FAIL;
  if (flags & 2) { off = 0; n = b->bytes; }
  if (off >= b->bytes) off = 0;
  if (n > b->bytes) n = b->bytes;
  u32 first = b->bytes - off < n ? b->bytes - off : n;
  *p1 = b->data + off; *n1 = first;
  if (p2) *p2 = n > first ? b->data : NULL;
  if (n2) *n2 = n - first;
  return S_OK;
}
HRESULT WINAPI pc_DSB_SetVolume(void *p, s32 v) {
  DSBuf *b = as_dsb(p);
  if (b) b->volume = v;
  return S_OK;
}

/* DirectSound */
void WINAPI pc_DirectSoundUseLightHRTF4Channel(void) {}
/* XAudioDownloadEffectsImage(pszImageName, pImageLoc, dwFlags, ppImageDesc) */
HRESULT WINAPI pc_XAudioDownloadEffectsImage(const char *name, void *loc,
                                             u32 flags, void **pp) {
  if (pp)
    *pp = g_engine;
  return S_OK;
}
