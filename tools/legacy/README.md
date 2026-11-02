# Legacy prototypes

Early experiments, kept for reference; the PC port does not use them.

- `xbe_to_pe.py`: first XBE → PE conversion (sections copied, no patching).
- `patch_exe.py`: rebuilt an IAT of Xbox kernel ordinals for an HLE kernel.
- `xbe_stripper.py`: generated the C++ HLE kernel thunks of `src/Host` (the `oogie_port` CMake target).

The working route is `tools/build_flat_pe.py` (run through `./pc.sh build`): XAPI goes straight to kernel32, the Xbox libraries are replaced by `src/PC`.
