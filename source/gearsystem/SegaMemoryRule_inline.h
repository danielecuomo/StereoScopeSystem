#ifndef SEGAMEMORYRULE_INLINE_H
#define SEGAMEMORYRULE_INLINE_H

#include "Memory.h"

INLINE u8 SegaMemoryRule::PerformReadFast(u16 address)
{
    if (address < 0x0400)
        return m_pMemory->Retrieve(address);

    if (address < 0x4000)
        return m_pROM[address + m_iMapperSlotAddress[0]];

    if (address < 0x8000)
        return m_pROM[(address - 0x4000) + m_iMapperSlotAddress[1]];

    if (address < 0xC000)
    {
        if (m_bRAMEnabled)
            return m_pRAMBanks[(address - 0x8000) + m_RAMBankStartAddress];
        return m_pROM[(address - 0x8000) + m_iMapperSlotAddress[2]];
    }

    return m_pMemory->Retrieve(address);
}

#endif
