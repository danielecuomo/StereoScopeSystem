# SegaScope 3-D touch launcher

The SMS launcher presents eight SegaScope 3-D games on the Nintendo 3DS bottom touchscreen as large, readable 2-column x 4-row touch buttons:

1. Blade Eagle 3-D
2. Line of Fire
3. Maze Hunter 3-D
4. Missile Defense 3-D
5. Out Run 3-D
6. Poseidon Wars 3-D
7. Space Harrier 3-D
8. Zaxxon 3-D

### Fast startup

The launcher no longer recursively scans the entire SD card. That was the cause of very long startup times on large SD cards. It checks the SD root and common ROM directories (`roms`, `ROMs`, `sms`, `SMS`, `games`, `Games`) and only descends one directory level below those folders.

If the ROMs are stored elsewhere, add that directory to `commonDirs` in `source/sms/main.cpp`.

### Controls

- Touch a game button to launch it.
- D-pad selects a game; A launches it.
- B exits the launcher.

The launcher uses a deliberately simple high-contrast grayscale palette to avoid relying on external artwork or fragile texture assets.
