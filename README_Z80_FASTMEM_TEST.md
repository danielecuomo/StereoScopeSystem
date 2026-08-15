# Z80 fast-memory test

This test keeps the previous timing/rendering changes and adds one targeted CPU-path optimization for standard Sega SMS cartridges:

- `Memory::Read()` bypasses the virtual `MemoryRule::PerformRead()` call when the active mapper is `SegaMemoryRule`.
- `SegaMemoryRule::PerformReadFast()` is inline and uses cached ROM pointer + mapper offsets.
- No emulation clock, VDP timing, audio timing, or frame pacing values were changed.

The purpose is to measure whether the indirect/virtual memory-read path is a major part of the `EMU` time reported by the diagnostic overlay.
