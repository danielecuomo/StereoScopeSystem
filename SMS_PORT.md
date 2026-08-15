# Red Viper SMS port

This tree replaces the Virtual Boy V810 emulation path with the Gearsystem Sega Master System core.

## Supported input

- D-pad: directions
- A: SMS button 1
- B: SMS button 2
- START: SMS Start
- SELECT: exit to the launcher

## ROM loading

The build scans `sdmc:/` and subdirectories for `.sms` files. It also supports a bundled ROM by putting the ROM in `romfs/` and naming it in `romfs/filename.txt`.

Save RAM is loaded/saved next to the ROM as `.sav`.

## Scope

This is an SMS-focused port, not a compatibility layer that can execute Virtual Boy ROMs. The original Red Viper V810/Virtual Boy source remains in the tree for reference, but the new Makefile does not compile it.
