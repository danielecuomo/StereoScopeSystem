#ifndef MISSILE_DEFENSE_3D_PATCHER_H
#define MISSILE_DEFENSE_3D_PATCHER_H

#include "definitions.h"

class MissileDefense3DPatcher
{
public:
    static const u32 ROM_CRC = 0xFBE5CFBB;
    static bool Apply(u8* rom, int size, u32 crc);
};

#endif
