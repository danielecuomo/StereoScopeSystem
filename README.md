# StereoScopeSystem

Nintendo 3DS homebrew application for playing Sega Master System (`.sms`) ROMs, built around the Gearsystem emulation core.

## Features

- Sega Master System emulation through the bundled Gearsystem core.
- Nintendo 3DS native video and audio output using devkitPro/citro libraries.
- Stereoscopic 3D rendering support for the 3DS.
- Dedicated shortcuts for eight Sega 3-D titles, identified from the ROM's Sega product code rather than its filename:
  - Blade Eagle
  - Line of Fire
  - Maze Hunter
  - Missile Defense
  - Out Run
  - Poseidon Wars
  - Space Harrier
  - Zaxxon

## Requirements

Building requires a Nintendo 3DS homebrew development environment based on:

- devkitPro
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

## Controls

### ROM browser

| 3DS control | Action |
|---|---|
| D-Pad | Select ROM |
| A | Launch selected ROM |
| Touchscreen | Select a ROM |

### In-game

| 3DS control | SMS action |
|---|---|
| D-Pad | Directional input |
| A | Button 1 |
| B | Button 2 |
| START | Start |
| SELECT | Return to ROM browser |

The Light Phaser for **Missile Defense** is currently not supported.

## Notes

This repository contains the emulator/application source and does **not** include commercial game ROMs.

Use only ROM images that you are legally entitled to use.

## License

See the individual source files and bundled components for their applicable copyright and license terms. The `source/gearsystem/` directory contains the Gearsystem emulation core and should be treated according to the licensing terms of that component.

## Test

Tested on New 3DS XL
