SMS performance test: RunFor(8)

This test keeps the previous fixes and changes only the emulation scheduler:
Gearsystem previously called Processor::RunFor(1) once per CPU instruction and then
Video::Tick/Audio::Tick after every instruction. That creates millions of C++ calls per
SMS frame. This test batches up to 8 Z80 T-states per RunFor call before ticking video/audio.

If performance improves substantially, the bottleneck is confirmed to be scheduler overhead.
If a game shows raster/timing glitches, the chunk can be reduced to 4 or 2.
