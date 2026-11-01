#!/usr/bin/env python3
"""
STM Data Structurer & Cataloger
Organizes stm_data archives for 'The Nightmare Before Christmas: Oogie's Revenge'.
Extracts and converts stages into structured directories with models, textures,
scenes, and decoded dialogue texts.
"""

import os
import sys
import json
import struct
import argparse
from capcom_tool import decompress_capcom_lz, unpack_stm_archive

WORLD_NAMES = {
    '00': 'Halloween Town Plaza & Jack\'s House',
    '01': 'Hinterlands & Forest Paths',
    '02': 'Dr. Finkelstein\'s Laboratory',
    '03': 'Oogie\'s Casino & Dungeons',
    '04': 'Town Hall & Streets',
    '05': 'Rooftops & Towers',
    '06': 'Graveyard & Crypts',
    '07': 'Christmas Town & Spiral Passage',
    '08': 'Santa\'s Workshop & Toy Factory',
    '09': 'Treehouse & Trash Area',
    '0a': 'Spiral Hill',
    '0b': 'Boss Musical Battles',
    '0c': 'Final Battle - Giant Oogie',
    'trap': 'Minigame - Trap',
    'treat': 'Minigame - Treat',
    'trick': 'Minigame - Trick',
    'warp': 'Warp Gates',
    'zero': 'Zero the Dog Special',
    'xroom': 'Debug Test Room',
    'title': 'Main Title & UI Screens'
}

def decode_bms_text(bms_bytes):
    if len(bms_bytes) < 8:
        return []
    count = struct.unpack('<I', bms_bytes[:4])[0]
    if count == 0 or count > 5000:
        return []
    offsets = [struct.unpack('<I', bms_bytes[4+i*4 : 8+i*4])[0] for i in range(count)]
    offsets.append(len(bms_bytes))
    
    punct_map = {
        0x8002: '!', 0x8008: "'", 0x800D: ',', 0x800E: '-', 0x800F: '.',
        0x8101: '\n', 0x8136: '?', 0x8137: '!', 0x8138: '-', 0x8139: ':',
        0x813b: "'", 0x813d: '"', 0x813e: '"', 0x813f: '(', 0x8140: ')',
        0x8144: '.', 0x8143: ','
    }
    
    results = []
    for idx in range(count):
        part = bms_bytes[offsets[idx]:offsets[idx+1]]
        if len(part) < 2:
            results.append('')
            continue
        words = struct.unpack(f'<{len(part)//2}H', part[:len(part)-len(part)%2])
        chars = []
        for w in words:
            if (w & 0xff) in [0x80, 0x81]:
                w = ((w & 0xff) << 8) | (w >> 8)
            code = w
            
            if code in [0x8000, 0x0000]:
                chars.append(' ')
            elif code in [0x8001, 0x8101]:
                chars.append('\n')
            elif 0x8034 <= code <= 0x804D:
                chars.append(chr(code - 0x8034 + ord('a')))
            elif 0x801A <= code <= 0x8033:
                chars.append(chr(code - 0x801A + ord('A')))
            elif 0x8010 <= code <= 0x8019:
                chars.append(chr(code - 0x8010 + ord('0')))
            elif code in punct_map:
                chars.append(punct_map[code])
        text = ''.join(chars).strip()
        if text:
            results.append(text)
    return results

def parse_data_inf(data_inf_path):
    with open(data_inf_path, 'rb') as f:
        data = f.read()
    magic = data[:4]
    if magic != b'@INF':
        raise ValueError("Invalid data.inf format")
    count = struct.unpack('<I', data[8:12])[0]
    pos = 16
    stages = []
    for _ in range(count):
        name = data[pos:pos+8].split(b'\x00')[0].decode('ascii', errors='ignore')
        uncomp_sz, comp_sz = struct.unpack('<II', data[pos+8:pos+16])
        stages.append({
            'name': name,
            'uncompressed_size': uncomp_sz,
            'compressed_size': comp_sz
        })
        pos += 16
    return stages

def get_stage_world(stage_name):
    for prefix in sorted(WORLD_NAMES.keys(), key=len, reverse=True):
        if stage_name.startswith(prefix):
            return prefix, WORLD_NAMES[prefix]
    return 'misc', 'Miscellaneous'

def organize_stage(stage_raw_dir, stage_out_dir, stage_name):
    os.makedirs(stage_out_dir, exist_ok=True)
    dirs = {
        'models': os.path.join(stage_out_dir, 'models'),
        'textures': os.path.join(stage_out_dir, 'textures'),
        'scene': os.path.join(stage_out_dir, 'scene'),
        'messages': os.path.join(stage_out_dir, 'messages'),
        'collision': os.path.join(stage_out_dir, 'collision'),
        'logic': os.path.join(stage_out_dir, 'logic'),
        'audio': os.path.join(stage_out_dir, 'audio')
    }
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)

    manifest = {
        'id': stage_name,
        'models': [],
        'textures': [],
        'messages': {}
    }

    import shutil
    for root, _, files in os.walk(stage_raw_dir):
        for f in files:
            src = os.path.join(root, f)
            ext = os.path.splitext(f)[1].lower()
            if ext in ['.pod', '.txi', '.amd']:
                shutil.copy2(src, os.path.join(dirs['models'], f))
                if ext == '.pod':
                    manifest['models'].append(f)
            elif ext in ['.pig', '.dds']:
                shutil.copy2(src, os.path.join(dirs['textures'], f))
                # Also save a copy with .dds extension for standard viewers
                base_tex = os.path.splitext(f)[0]
                dds_path = os.path.join(dirs['textures'], base_tex + '.dds')
                if not os.path.exists(dds_path):
                    shutil.copy2(src, dds_path)
                manifest['textures'].append(f)
            elif f in ['bg_list.txt', 'cnfg.txt'] or ext in ['.cam', '.clp', '.lgt']:
                shutil.copy2(src, os.path.join(dirs['scene'], f))
            elif ext == '.bms':
                shutil.copy2(src, os.path.join(dirs['messages'], f))
                # Decode text
                with open(src, 'rb') as bms_f:
                    decoded = decode_bms_text(bms_f.read())
                if decoded:
                    txt_path = os.path.join(dirs['messages'], os.path.splitext(f)[0] + '.txt')
                    with open(txt_path, 'w', encoding='utf-8') as out_txt:
                        out_txt.write('\n\n---\n\n'.join(decoded))
                    manifest['messages'][f] = len(decoded)
            elif ext == '.hm':
                shutil.copy2(src, os.path.join(dirs['collision'], f))
            elif ext in ['.lgc', '.atr', '.rbp']:
                shutil.copy2(src, os.path.join(dirs['logic'], f))
            elif ext in ['.sec', '.sel', '.ses']:
                shutil.copy2(src, os.path.join(dirs['audio'], f))

    with open(os.path.join(stage_out_dir, 'manifest.json'), 'w') as mf:
        json.dump(manifest, mf, indent=2)

def main():
    parser = argparse.ArgumentParser(description="Structure stm_data into clean organized folders.")
    parser.add_argument('--inf', default='extracted/data.inf', help="Path to data.inf")
    parser.add_argument('--stm-dir', default='extracted/stm_data', help="Path to stm_data directory")
    parser.add_argument('--out-dir', default='structured_data', help="Output directory")
    parser.add_argument('--stage', default=None, help="Specific stage to extract (e.g. 0000, 020a, or 'all')")
    parser.add_argument('--list-only', action='store_true', help="Only display the stage catalog")
    args = parser.parse_args()

    if not os.path.exists(args.inf):
        print(f"Error: {args.inf} not found.")
        sys.exit(1)

    stages = parse_data_inf(args.inf)
    print(f"Loaded {len(stages)} entries from {args.inf}")

    catalog = {}
    for st in stages:
        name = st['name']
        world_id, world_desc = get_stage_world(name)
        if world_id not in catalog:
            catalog[world_id] = {
                'title': world_desc,
                'stages': []
            }
        lz_path = os.path.join(args.stm_dir, f"{name}.lz")
        st['exists_lz'] = os.path.exists(lz_path)
        catalog[world_id]['stages'].append(st)

    if args.list_only or args.stage is None:
        print("\n=== STAGE CATALOG ===")
        for wid, wdata in catalog.items():
            print(f"\n[{wid}] {wdata['title']} ({len(wdata['stages'])} stages):")
            for s in wdata['stages']:
                status = "✓ LZ" if s['exists_lz'] else "- RAW"
                print(f"   • {s['name']:<10} {status} | uncomp: {s['uncompressed_size']:>10,} bytes")

    if args.list_only:
        return

    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, 'catalog.json'), 'w') as f:
        json.dump(catalog, f, indent=2)
    print(f"\nSaved catalog to {args.out_dir}/catalog.json")

    stages_to_extract = []
    if args.stage == 'all':
        stages_to_extract = [s['name'] for s in stages if s['exists_lz']]
    elif args.stage:
        stages_to_extract = [args.stage]
    else:
        # Default extract sample essential stages (0000, 020a, xroom, title_u)
        sample = ['0000', '020a', 'xroom', 'title_u']
        stages_to_extract = [s for s in sample if os.path.exists(os.path.join(args.stm_dir, f"{s}.lz"))]

    print(f"\nExtracting and structuring {len(stages_to_extract)} stage(s)...")
    import tempfile

    for s_name in stages_to_extract:
        lz_file = os.path.join(args.stm_dir, f"{s_name}.lz")
        if not os.path.exists(lz_file):
            print(f"Warning: {lz_file} does not exist, skipping.")
            continue
        print(f"Processing stage: {s_name}...")
        with open(lz_file, 'rb') as f:
            raw_lz = f.read()
        decompressed = decompress_capcom_lz(raw_lz)
        
        # Unpack to temporary directory first
        with tempfile.TemporaryDirectory() as tmp_raw:
            unpack_stm_archive(decompressed, tmp_raw)
            stage_out = os.path.join(args.out_dir, 'stages', s_name)
            organize_stage(tmp_raw, stage_out, s_name)
            print(f"  -> Structured into: {stage_out}")

    print("\nStructuring complete!")

if __name__ == '__main__':
    main()

