"""
Xbox D3D8 and XPP (XDK 5849) entry points called by the game -> PC
replacements in src/PC (render.c, input.c). Used by build_flat_pe.py.

Addresses come from tools/xbsymscan.py plus the ret sizes of the originals;
each C function has the same calling convention and stack size.
"""

import os
import subprocess

import nv2a_vsh

SRC = 'src/PC/pc.c'
OBJ = 'build/pc/pc.o'

# va: (C symbol as decorated by clang, what it replaces)
D3D_TO_PC = {
    0x1d8ee0: ('_pc_CreateDevice@24', 'Direct3D_CreateDevice'),
    0x1d7610: ('_pc_Clear@24', 'D3DDevice_Clear'),
    0x1d7460: ('_pc_Swap@4', 'D3DDevice_Swap'),
    0x1d6830: ('_pc_CreateTexture2@28', 'D3DDevice_CreateTexture2'),
    0x1d6950: ('_pc_LockRect@20', 'D3DTexture_LockRect'),
    0x1d6900: ('_pc_GetSurfaceLevel2@8', 'D3DTexture_GetSurfaceLevel2'),
    0x1d8490: ('_pc_SurfaceGetDesc@8', 'D3DSurface_GetDesc'),
    0x1d8f90: ('_pc_CreateVertexBuffer2@4', 'D3DDevice_CreateVertexBuffer2'),
    0x1d8fe0: ('_pc_VertexBufferLock2@8', 'D3DVertexBuffer_Lock2'),
    0x1d6650: ('_pc_ResourceAddRef@4', 'D3DResource_AddRef'),
    0x1d6690: ('_pc_ResourceRelease@4', 'D3DResource_Release'),
    0x1d6770: ('_pc_ResourceRegister@8', 'D3DResource_Register'),
    0x1d67a0: ('_pc_ResourceBlockUntilNotBusy@4', 'D3DResource_BlockUntilNotBusy'),
    0x1d3bd0: ('_pc_GetBackBuffer2@4', 'D3DDevice_GetBackBuffer2'),
    0x1d3f50: ('_pc_GetRenderTarget2@0', 'D3DDevice_GetRenderTarget2'),
    0x1d3f70: ('_pc_GetDepthStencilSurface2@0', 'D3DDevice_GetDepthStencilSurface2'),
    0x1d3950: ('_pc_SetRenderTarget@8', 'D3DDevice_SetRenderTarget'),
    0x1d3c20: ('_pc_CopyRects@20', 'D3DDevice_CopyRects'),
    0x1d41f0: ('_pc_SetTexture@8', 'D3DDevice_SetTexture'),
    0x1d41c0: ('_pc_GetTexture2@4', 'D3DDevice_GetTexture2'),
    0x1d3430: ('_pc_SetTransform@8', 'D3DDevice_SetTransform'),
    0x1d3550: ('_pc_GetTransform@8', 'D3DDevice_GetTransform'),
    0x1d3f90: ('_pc_SetViewport@4', 'D3DDevice_SetViewport'),
    0x1d3580: ('_pc_GetViewport@4', 'D3DDevice_GetViewport'),
    0x1d3680: ('_pc_GetScissors@12', 'D3DDevice_GetScissors'),
    0x1d44c0: ('_pc_SetScissors@12', 'D3DDevice_SetScissors'),
    0x1d3640: ('_pc_GetTile@8', 'D3DDevice_GetTile'),
    0x1d44b0: ('_pc_SetTile@8', 'D3DDevice_SetTile'),
    0x1d47c0: ('_pc_SetScreenSpaceOffset@8', 'D3DDevice_SetScreenSpaceOffset'),
    0x1d33a0: ('_pc_SetGammaRamp@8', 'D3DDevice_SetGammaRamp'),
    0x1d4a90: ('@pc_SetRenderState_Simple@8', 'D3DDevice_SetRenderState_Simple'),
    0x1d4e10: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState_FogColor'),
    0x1d4e60: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState_CullMode'),
    0x1d5ce0: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState_ZEnable'),
    0x1d5d70: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState (D3D__RenderState 0x1e2cd0)'),
    0x1d5e10: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState (D3D__RenderState 0x1e2cd4)'),
    0x1d5f90: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState_RopZCmpAlwaysRead'),
    0x1d6050: ('_pc_SetRenderState_1@4', 'D3DDevice_SetRenderState_MultiSampleAntiAlias'),
    0x1d5300: ('_pc_SetTextureState_TexCoordIndex@8', 'D3DDevice_SetTextureState_TexCoordIndex'),
    0x1d2a70: ('_pc_CreateVertexShader@16', 'D3DDevice_CreateVertexShader'),
    0x1d3200: ('_pc_SetVertexShader@4', 'D3DDevice_SetVertexShader'),
    0x1d2bb0: ('_pc_GetVertexShader@4', 'D3DDevice_GetVertexShader'),
    0x1d2f90: ('_pc_GetVertexShaderSize@8', 'D3DDevice_GetVertexShaderSize'),
    0x1d2e90: ('_pc_LoadVertexShader@8', 'D3DDevice_LoadVertexShader'),
    0x1d2ef0: ('_pc_SelectVertexShader@8', 'D3DDevice_SelectVertexShader'),
    0x1d2fb0: ('_pc_SetShaderConstantMode@4', 'D3DDevice_SetShaderConstantMode'),
    0x1d2bd0: ('@pc_SetVertexShaderConstant1@8', 'D3DDevice_SetVertexShaderConstant1'),
    0x1d2c30: ('@pc_SetVertexShaderConstant4@8', 'D3DDevice_SetVertexShaderConstant4'),
    0x1d2dc0: ('@pc_SetVertexShaderConstantNotInline@12', 'D3DDevice_SetVertexShaderConstantNotInline'),
    0x1d22c0: ('_pc_CreatePixelShader@8', 'D3DDevice_CreatePixelShader'),
    0x1d2370: ('_pc_SetPixelShader@4', 'D3DDevice_SetPixelShader'),
    0x1d2350: ('_pc_GetPixelShader@4', 'D3DDevice_GetPixelShader'),
    0x1d2310: ('_pc_SetPixelShaderProgram@4', 'D3DDevice_SetPixelShaderProgram'),
    0x1d2e00: ('_pc_SetStreamSource@12', 'D3DDevice_SetStreamSource'),
    0x1d6200: ('_pc_DrawVerticesUP@16', 'D3DDevice_DrawVerticesUP'),
    0x1d6370: ('_pc_DrawVertices@12', 'D3DDevice_DrawVertices'),
    0x1d8ec0: ('_pc_SetPushBufferSize@8', 'D3D_SetPushBufferSize'),
    0x1d35d0: ('_pc_SetVBlankCallback_1db8@4', 'D3DDevice vertical-blank callback (+0x1db8)'),
    0x1d35f0: ('_pc_SetVBlankCallback_1db4@4', 'D3DDevice vertical-blank callback (+0x1db4)'),
    0x1d7a50: ('_pc_Set1@4', 'D3DDevice field setter (+0x19fc)'),
    0x1d7de0: ('_pc_Get1@4', 'D3DDevice push-buffer query'),
    0x1d3610: ('_pc_BlockUntilVerticalBlank@0', 'D3DDevice vertical blank wait'),
}

# XPP (USB gamepad) -> src/PC/input.c
XPP_TO_PC = {
    0x259b6d: ('_pc_XInitDevices@8', 'XInitDevices (jump stub the game calls)'),
    0x258f84: ('_pc_XInitDevices@8', 'XInitDevices'),
    0x259b72: ('_pc_XGetDevices@4', 'XGetDevices'),
    0x259b94: ('_pc_XGetDeviceChanges@12', 'XGetDeviceChanges'),
    0x25986b: ('_pc_XInputOpen@16', 'XInputOpen'),
    0x2598c1: ('_pc_XInputClose@4', 'XInputClose'),
    0x2598cd: ('_pc_XInputGetCapabilities@8', 'XInputGetCapabilities'),
    0x259aa5: ('_pc_XInputGetState@8', 'XInputGetState'),
    0x259b18: ('_pc_XInputSetState@8', 'XInputSetState'),
}

# XAPI contiguous memory -> src/PC/memory.c (Xbox physical memory window)
MEM_TO_PC = {
    0x1bdc57: ('_pc_XPhysicalAlloc@16', 'XPhysicalAlloc'),
    0x1bdc9a: ('_pc_XPhysicalAllocEx@20', 'XPhysicalAllocEx'),
    0x1bdcc7: ('_pc_XPhysicalFree@4', 'XPhysicalFree'),
    0x1bfe94: ('_pc_XMemAlloc@8', 'XMemAlloc'),
    0x1bff34: ('_pc_XMemFree@8', 'XMemFree'),
}

# XACT / DirectSound -> src/PC/audio.c (silent for now)
AUDIO_TO_PC = {
    0x22470e: ('_pc_XACTEngineCreate@8', 'XACTEngineCreate'),
    0x2246dc: ('_pc_XACTEngineDoWork@0', 'XACTEngineDoWork'),
    0x222ade: ('_pc_XACT_CreateSoundSource@12', 'IXACTEngine_CreateSoundSource'),
    0x222b1d: ('_pc_XACT_CreateSoundBank@16', 'IXACTEngine_CreateSoundBank'),
    0x222b60: ('_pc_XACT_RegisterWaveBank@16', 'IXACTEngine_RegisterWaveBank'),
    0x222ba3: ('_pc_XACT_RegisterStreamedWaveBank@12', 'IXACTEngine_RegisterStreamedWaveBank'),
    0x222be2: ('_pc_XACT_Nop2@8', 'IXACTEngine_UnRegisterWaveBank'),
    0x222c1d: ('_pc_XACT_Nop3@12', 'IXACTEngine (3 args)'),
    0x222c3d: ('_pc_XACT_Nop2@8', 'IXACTEngine_RegisterNotification'),
    0x222c78: ('_pc_XACT_Nop2@8', 'IXACTEngine_UnRegisterNotification'),
    0x222cb3: ('_pc_XACT_GetNotification@12', 'IXACTEngine_GetNotification'),
    0x222cf2: ('_pc_XACT_Nop2@8', 'IXACTEngine_FlushNotification'),
    0x222d2d: ('_pc_XACT_Out2@8', 'IXACTEngine (engine, out)'),
    0x222d65: ('_pc_XACT_Nop2@8', 'IXACTEngine (2 args)'),
    0x222da2: ('_pc_XACT_Nop1@4', 'IXACT object Release'),
    0x222dcd: ('_pc_XACT_GetCueIndex@12', 'IXACTSoundBank_GetSoundCueIndexFromFriendlyName'),
    0x222e00: ('_pc_XACT_Nop3@12', 'IXACTSoundBank (3 args)'),
    0x222e33: ('_pc_XACT_Nop3@12', 'IXACTSoundBank_PlayEx'),
    0x222e66: ('_pc_XACT_Nop4@16', 'IXACTSoundBank (4 args)'),
    0x222e9d: ('_pc_XACT_Nop3@12', 'IXACTSoundBank (3 args)'),
    0x222ed0: ('_pc_XACT_Nop1@4', 'IDirectSoundStream (1 arg)'),
    0x222ed5: ('_pc_XACT_Nop5@20', 'IDirectSoundStream_SetPosition/SetVelocity'),
    0x222efe: ('_pc_XACT_Nop2@8', 'IDirectSoundStream (2 args)'),
    0x222f03: ('_pc_XACT_Nop2@8', 'IDirectSoundStream (2 args)'),
    0x222f08: ('_pc_XACT_Nop1@4', 'IDirectSoundStream (1 arg)'),
    0x222f0d: ('_pc_XACT_Out2@8', 'IDirectSoundStream_GetStatus'),
    0x204492: ('_pc_DirectSoundUseLightHRTF4Channel@0', 'DirectSoundUseLightHRTF4Channel'),
    0x205ece: ('_pc_XAudioDownloadEffectsImage@16', 'XAudioDownloadEffectsImage'),
}

PC_TABLE = {**D3D_TO_PC, **XPP_TO_PC, **MEM_TO_PC, **AUDIO_TO_PC}

# DLL of each Windows function the renderer imports (anything else: kernel32)
DLL_OF = {
    'd3d9.dll': {'Direct3DCreate9'},
    'user32.dll': {'RegisterClassA', 'CreateWindowExA', 'ShowWindow', 'PeekMessageA', 'TranslateMessage',
                   'DispatchMessageA', 'DefWindowProcA', 'PostQuitMessage', 'AdjustWindowRect', 'LoadCursorA',
                   'GetAsyncKeyState'},
    'xinput9_1_0.dll': {'XInputGetState'},
    'd3dcompiler_47.dll': {'D3DCompile'},
}


def dll_of(fn):
    for dll, names in DLL_OF.items():
        if fn in names:
            return dll
    return 'kernel32.dll'


def compile_renderer():
    """clang -c src/PC/pc.c -> i386 COFF object (any clang with the x86 backend).

    The game's vertex shaders are translated first (build/pc/vshaders.inc)."""
    os.makedirs(os.path.dirname(OBJ), exist_ok=True)
    nv2a_vsh.emit_c(nv2a_vsh.xbe_reader('rom/extracted/default.xbe'), os.path.join(os.path.dirname(OBJ), 'vshaders.inc'))
    clang = os.environ.get('CLANG', 'clang')
    subprocess.run([clang, '--target=i686-pc-windows-gnu', '-c', '-O2', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
                    '-fno-ident', '-mno-stack-arg-probe', '-msse2', '-Wall', '-Wno-unused-function',
                    '-I', os.path.dirname(OBJ), SRC, '-o', OBJ], check=True)
    return OBJ
