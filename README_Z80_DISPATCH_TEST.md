# Z80 dispatcher + handler optimization test

This build combines the previous working optimizations with a direct switch-based Z80 opcode dispatcher.

Changes:
- Keeps GS_DISABLE_DISASSEMBLER.
- Keeps the PAL/NTSC event-aware RunToVBlank batching.
- Keeps the existing RunFor(16) test behavior.
- Replaces the hot member-function-pointer opcode dispatch with direct switch dispatch for base, CB and ED opcodes.
- Removes the opcode function-pointer tables and their initialization.

The direct calls make opcode handlers visible to the compiler as ordinary direct calls, allowing better optimization/inlining at -O3.
No opcode semantics or timing tables are changed.
