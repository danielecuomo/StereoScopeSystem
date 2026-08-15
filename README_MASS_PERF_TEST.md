# SMS mass performance build

This build is an aggressive performance pass intended for the SMS port on Nintendo 3DS.

## Runtime changes

- Enables New 3DS CPU/L2 speedup with `osSetSpeedupEnable(true)`.
- Raises the application CPU time limit to 89%.
- Disables the per-instruction opcode histogram and instruction counter in the performance build.
- Removes the empty `DisassembleNextOPCode()` call from the hot instruction loop when the disassembler is disabled.
- Keeps LTO and adds frame-pointer/unwind/stack-protector removal plus section GC.
- Avoids the disabled-YM2413 per-master-clock `Sync()` loop; the disabled path advances sample timing arithmetically.
- Replaces the SMS RGB565 framebuffer conversion with a palette lookup.
- Replaces the texture upload's full-frame copy + temporary tile + second copy with direct PICA200 tile gathering.
- Caches SMS background tile metadata/pattern bytes for the eight pixels of a tile row instead of re-reading VRAM for every pixel.
- Caches sprite pattern bytes for each rendered sprite row.
- Starts the normal SMS frontend with 3D disabled and only enables the right-eye target if SegaScope activity is detected.
- Uses `C3D_FRAME_NONBLOCK` so a busy GPU does not stall the emulation loop.
- Alternates the two SMS textures in normal mono mode so the CPU can upload the next frame while the GPU is consuming the previous one.

## Compatibility strategy

The Z80 opcode semantics themselves were not rewritten. The earlier experimental hot-opcode replacement was intentionally not retained because it caused the game image to disappear.

The highest-risk changes are the VDP scanline caching and the presentation mode; these should be tested first with Line of Fire and then with another SMS title. The direct texture swizzle was checked against the previous permutation algorithm and produces the same tile ordering.
