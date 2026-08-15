# SMS timing diagnostic build

This build keeps the previous performance changes and adds a low-overhead timing overlay on the bottom screen.

The overlay updates once per second and reports:

- `Region`: PAL or NTSC
- `FPS`: actual frames completed per second, including presentation
- `Clock/frame`: Gearsystem master clock cycles advanced per emulated frame
- `EMU`: average time spent in `RunToVBlank()` per frame
- `VIDEO`: average `Render16bit()` conversion time per frame
- `AUDIO`: average audio submission time per frame
- `UPLOAD`: average texture swizzle/upload time per frame
- `PRESENT`: average `renderFrame()` time, including display synchronization
- `TOTAL`: average complete frontend frame time

The measurements are averaged over a one-second window to avoid per-frame console output affecting timing.

Please run a game for at least 5 seconds and report the values shown in the overlay. The important values are FPS, EMU, PRESENT, TOTAL, and Clock/frame.
