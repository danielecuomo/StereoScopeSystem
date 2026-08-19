#include "MissileDefense3DPatcher.h"
#include "log.h"

namespace
{
    struct PatchByte
    {
        int offset;
        u8 original;
        u8 patched;
    };

    // Missile Defense 3-D's Light Phaser acquisition routine at $2479.
    // The patched routine keeps the original Z80 algorithm intact, but
    // redirects only its three IN A,(n) sources to private emulator ports:
    //   $FD = virtual TH
    //   $FE = virtual HCounter
    //   $FF = virtual VCounter
    // All replacements are one-byte operand changes, so instruction timing
    // and the game's call/stack tricks remain unchanged.
    static const PatchByte kPatch[] =
    {
        {0x247A, 0xDD, 0xFD}, {0x2480, 0x7F, 0xFE}, {0x248A, 0x7E, 0xFF},
        {0x24A0, 0xDD, 0xFD}, {0x24A6, 0x7F, 0xFE}, {0x24B1, 0x7E, 0xFF},
        {0x24BF, 0xDD, 0xFD}, {0x24C5, 0x7F, 0xFE}, {0x24D1, 0x7E, 0xFF},
        {0x24DE, 0xDD, 0xFD}, {0x24E4, 0x7F, 0xFE}, {0x24F0, 0x7E, 0xFF},
        {0x2510, 0xDD, 0xFD}, {0x2516, 0x7F, 0xFE}, {0x2520, 0x7E, 0xFF},
        {0x2536, 0xDD, 0xFD}, {0x253C, 0x7F, 0xFE}, {0x2547, 0x7E, 0xFF}
    };
}

bool MissileDefense3DPatcher::Apply(u8* rom, int size, u32 crc)
{
    if (crc != ROM_CRC || rom == nullptr || size < 0x2548)
        return false;

    for (const PatchByte& p : kPatch)
    {
        if (rom[p.offset] != p.original && rom[p.offset] != p.patched)
        {
            Log("Missile Defense 3-D patch refused: unexpected byte at $%04X (got %02X)",
                p.offset, rom[p.offset]);
            return false;
        }
    }

    bool changed = false;
    for (const PatchByte& p : kPatch)
    {
        if (rom[p.offset] == p.original)
        {
            rom[p.offset] = p.patched;
            changed = true;
        }
    }

    Log("Missile Defense 3-D: %s Light Phaser acquisition patch applied (CRC %08X)",
        changed ? "dynamic" : "already", crc);
    return true;
}
