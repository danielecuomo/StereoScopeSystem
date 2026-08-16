# Performance optimization pass

This pass targets the runtime hot paths of the SMS/Game Gear core without changing Z80 opcode semantics or emulation timing.

## Changes

- Cache the three Sega mapper ROM page base pointers and use them in the inline fast-read path. Bank switching refreshes the cached pointers; save-state restore rebuilds them.
- Replace byte-at-a-time cartridge slot initialization with bulk `memcpy`/`memset`.
- Replace byte-at-a-time Sega RAM persistence I/O with bulk stream operations.
- Remove the scheduler lambda from `Video::GetCyclesToNextEvent()` and keep the same event-selection semantics with direct branches.
- Precompute the native SMS RGB565 palette instead of rebuilding 64 entries on every framebuffer conversion.

## Validation

- All 40 `source/gearsystem/*.cpp` translation units pass host `g++` syntax checks with `-O3 -Wall -Werror` under the project's warning policy and `GS_DISABLE_DISASSEMBLER` configuration.
- The Nintendo 3DS `devkitARM` toolchain is not installed in the analysis environment, so the 3DS ELF/3DSX/CIA binaries were not rebuilt here.

## Compatibility

No Z80 instruction handler, timing table, VDP event condition, mapper selection rule, or audio algorithm was intentionally changed.
