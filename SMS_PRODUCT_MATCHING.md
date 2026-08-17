# SMS SegaScope product-number matching

The SegaScope ROM browser identifies the eight built-in menu entries from the
16-byte `TMR SEGA` cartridge header, using the 16-bit product code at header
offset `0x0A-0x0B`.

The ROM filename is no longer used for game identification. It is only used as
the path to the ROM selected after its product code has been recognized.

Recognized product codes:

| Game | Product code |
|---|---|
| Blade Eagle 3-D | `EDFB` |
| Line of Fire | `DB85` |
| Maze Hunter 3-D | `9387` |
| Missile Defense 3-D | `154A` |
| Out Run 3-D | `4358` |
| Poseidon Wars 3-D | `3E00` |
| Space Harrier 3-D | `4786` |
| Zaxxon 3-D | `2767` |

The scanner checks the standard Sega header offsets and also accounts for the
common 512-byte ROM-dump prefix.
