# StereoScopeSystem

Nintendo 3DS homebrew application for playing Sega Master System (`.sms`) ROMs, built around the Gearsystem emulation core.

The application is currently identified in the build metadata as **Red Viper SMS** (`v1.3.2`) and includes support for stereoscopic/3D presentation features intended for compatible Sega 3-D titles.

## Features

- Sega Master System emulation through the bundled Gearsystem core.
- Nintendo 3DS native video and audio output using devkitPro/citro libraries.
- Stereoscopic 3D rendering support for the 3DS.
- ROM browser for `.sms` files.
- Dedicated shortcuts for eight Sega 3-D titles, identified from the ROM's Sega product code rather than its filename:
  - Blade Eagle 3-D
  - Line of Fire
  - Maze Hunter 3-D
  - Missile Defense 3-D
  - Out Run 3-D
  - Poseidon Wars 3-D
  - Space Harrier 3-D
  - Zaxxon 3-D
- Touchscreen aiming support for **Missile Defense 3-D**.
- Optimized release build with LTO and section garbage collection.

## Requirements

Building requires a Nintendo 3DS homebrew development environment based on:

- [devkitPro](https://devkitpro.org/)
- devkitARM
- `3ds-dev`
- `3dstools`
- GNU Make

The repository includes helper scripts under `scripts/` for installing the devkitPro environment and packages.

For CIA generation, the Makefile additionally expects:

- `cxitool`
- `makerom`

These tools must be available in `PATH`.

## Building

After installing the 3DS development environment:

```sh
make
```

The default target is a release build.

Other build configurations are available:

```sh
make testing
make debug
make slowdebug
```

Clean generated files with:

```sh
make clean
```

A successful build produces the corresponding 3DS homebrew artifacts, including the `.3dsx` and `.smdh`. When the required CIA packaging tools are installed, the build also produces a `.cia`.

## ROMs

The application looks for SMS ROMs in:

```text
sdmc:/3ds/sms_roms
```

Only files with the `.sms` extension are considered.

For the eight built-in 3-D shortcuts, identification is based on the Sega cartridge product code stored in the ROM header. The filename does not need to match the game title.

Example SD card layout:

```text
SD:/3ds/sms_roms/
├── game1.sms
├── game2.sms
└── ...
```

The application does not recursively scan the SD card.

## Controls

### ROM browser

| 3DS control | Action |
|---|---|
| D-Pad | Select ROM |
| A | Launch selected ROM |
| B | Exit browser |
| Touchscreen | Select a ROM |

### In-game

| 3DS control | SMS action |
|---|---|
| D-Pad | Directional input |
| A | Button 1 |
| B | Button 2 |
| START | Start |
| SELECT | Return to ROM browser |

For **Missile Defense 3-D**, the touchscreen is used to control the Light Phaser aiming position and **A** acts as the trigger.

## Project layout

```text
.
├── Makefile
├── icon.png
├── resources/
│   └── AppInfo
├── romfs/
├── scripts/
│   ├── fetch-submodules
│   ├── install-3DS-dev-packages
│   └── install-devkitpro-pacman
└── source/
    ├── sms/
    └── gearsystem/
```

Build products are intentionally excluded from version control. See `.gitignore`.

## Version

Current application version:

```text
v1.3.2
```

The build also embeds the short Git commit hash when the source tree is built from a Git repository.

## Notes

This repository contains the emulator/application source and does **not** include commercial game ROMs.

Use only ROM images that you are legally entitled to use.

## License

See the individual source files and bundled components for their applicable copyright and license terms. The `source/gearsystem/` directory contains the Gearsystem emulation core and should be treated according to the licensing terms of that component.
