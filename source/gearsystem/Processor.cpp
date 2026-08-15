/*
 * Gearsystem - Sega Master System / Game Gear Emulator
 * Copyright (C) 2013  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include <algorithm>
#include <cassert>
#include <ctype.h>
#include <string.h>
#include "Processor.h"
#include "TraceLogger.h"
#include "opcode_timing.h"
#if !defined(GS_DISABLE_DISASSEMBLER) || defined(GS_DEBUG)
#include "opcode_names.h"
#endif
#include "IOPorts.h"
#include "common.h"

Processor::Processor(Memory* pMemory)
{
    m_pMemory = pMemory;
    m_pMemory->SetProcessor(this);
    InitPointer(m_pIOPorts);
    InitPointer(m_pTraceLogger);
    m_bIFF1 = false;
    m_bIFF2 = false;
    m_bHalt = false;
    m_bBranchTaken = false;
    m_iTStates = 0;
    m_iInjectedTStates = 0;
    m_instructionCount = 0;
#if GS_PERF_DIAGNOSTICS
    memset(m_opcodeHistogram, 0, sizeof(m_opcodeHistogram));
    memset(m_opcodeCBHistogram, 0, sizeof(m_opcodeCBHistogram));
    memset(m_opcodeEDHistogram, 0, sizeof(m_opcodeEDHistogram));
#endif
    m_bAfterEI = false;
    m_Q = 0;
    m_QTemp = 0;
    m_iInterruptMode = 0;
    m_bINTRequested = false;
    m_bNMIRequested = false;
    m_bPrefixedCBOpcode = false;
    m_PrefixedCBValue = 0;
    m_bInputLastCycle = false;
    m_iHaltCycle = 0;
    m_bCycleAccurateHalt = false;
    m_ProActionReplayList.clear();
    m_breakpoints_enabled = false;
    m_breakpoints_irq_enabled = false;
    m_cpu_breakpoint_hit = false;
    m_memory_breakpoint_hit = false;
    m_run_to_breakpoint_hit = false;
    m_run_to_breakpoint_requested = false;
    m_disassembler_syntax = GS_Disassembler_Syntax_Gearsystem;
    m_debug_next_irq = 1;

    m_ProcessorState.AF = &AF;
    m_ProcessorState.BC = &BC;
    m_ProcessorState.DE = &DE;
    m_ProcessorState.HL = &HL;
    m_ProcessorState.AF2 = &AF2;
    m_ProcessorState.BC2 = &BC2;
    m_ProcessorState.DE2 = &DE2;
    m_ProcessorState.HL2 = &HL2;
    m_ProcessorState.IX = &IX;
    m_ProcessorState.IY = &IY;
    m_ProcessorState.SP = &SP;
    m_ProcessorState.PC = &PC;
    m_ProcessorState.WZ = &WZ;
    m_ProcessorState.I = &I;
    m_ProcessorState.R = &R;
    m_ProcessorState.IFF1 = &m_bIFF1;
    m_ProcessorState.IFF2 = &m_bIFF2;
    m_ProcessorState.Halt = &m_bHalt;
    m_ProcessorState.NMI = &m_bNMIRequested;
    m_ProcessorState.INT = &m_bINTRequested;
    m_ProcessorState.InterruptMode = &m_iInterruptMode;
}

Processor::~Processor()
{
}

void Processor::SetDisassemblerSyntax(GS_Disassembler_Syntax syntax)
{
    if (syntax < GS_Disassembler_Syntax_Gearsystem || syntax >= GS_Disassembler_Syntax_Count)
        syntax = GS_Disassembler_Syntax_Gearsystem;

    m_disassembler_syntax = syntax;
}

GS_Disassembler_Syntax Processor::GetDisassemblerSyntax() const
{
    return m_disassembler_syntax;
}

void Processor::Init()
{
    Reset();
}

void Processor::Reset(bool cycleAccurateHalt)
{
    m_bIFF1 = false;
    m_bIFF2 = false;
    m_bHalt = false;
    m_bBranchTaken = false;
    m_iTStates = 0;
    m_iInjectedTStates = 0;
    m_instructionCount = 0;
#if GS_PERF_DIAGNOSTICS
    memset(m_opcodeHistogram, 0, sizeof(m_opcodeHistogram));
    memset(m_opcodeCBHistogram, 0, sizeof(m_opcodeCBHistogram));
    memset(m_opcodeEDHistogram, 0, sizeof(m_opcodeEDHistogram));
#endif
    m_bAfterEI = false;
    m_iInterruptMode = 0;
    PC.SetValue(0x0000);
    SP.SetValue(0xDFF0);
    IX.SetValue(0xFFFF);
    IY.SetValue(0xFFFF);
    AF.SetValue(0x0040);  // Zero flag set
    BC.SetValue(0x0000);
    DE.SetValue(0x0000);
    HL.SetValue(0x0000);
    AF2.SetValue(0x0000);
    BC2.SetValue(0x0000);
    DE2.SetValue(0x0000);
    HL2.SetValue(0x0000);
    WZ.SetValue(0x0000);
    I = 0x00;
    R = 0x00;
    m_Q = 0;
    m_QTemp = 0;
    m_bINTRequested = false;
    m_bNMIRequested = false;
    m_bPrefixedCBOpcode = false;
    m_PrefixedCBValue = 0;
    m_bInputLastCycle = false;
    m_iHaltCycle = 0;
    m_bCycleAccurateHalt = cycleAccurateHalt;
    m_ProActionReplayList.clear();
    m_cpu_breakpoint_hit = false;
    m_memory_breakpoint_hit = false;
    m_run_to_breakpoint_hit = false;
    m_run_to_breakpoint_requested = false;
    ClearDisassemblerCallStack();
    m_debug_next_irq = 1;
}

void Processor::SetIOPOrts(IOPorts* pIOPorts)
{
    m_pIOPorts = pIOPorts;
}

IOPorts* Processor::GetIOPOrts()
{
    return m_pIOPorts;
}

u32 Processor::RunFor(u32 tstates)
{
    u32 executed = 0;

    while (executed < tstates)
    {
        m_iTStates = 0;
#if !defined(GS_DISABLE_DISASSEMBLER)
        m_cpu_breakpoint_hit = false;
        m_memory_breakpoint_hit = false;
        m_run_to_breakpoint_hit = false;
#endif

        if (!m_bInputLastCycle)
        {
            if (m_bNMIRequested)
            {
                LeaveHalt();
                m_bNMIRequested = false;
                m_bIFF1 = false;
#if !defined(GS_DISABLE_DISASSEMBLER)
                u16 pc = PC.GetValue();
#endif
                StackPush(&PC);
                PC.SetValue(0x0066);
                m_iTStates += 11;
                IncreaseR();
                WZ.SetValue(PC.GetValue());
#if !defined(GS_DISABLE_DISASSEMBLER)
                m_debug_next_irq = 2;
                PushCallStack(pc, 0x0066, pc, 0);
                if (m_pTraceLogger->IsEnabled(TRACE_CPU_IRQ))
                {
                    GS_Trace_Entry e = {};
                    e.type = TRACE_CPU_IRQ;
                    e.irq.pc = pc;
                    e.irq.vector = 0x0066;
                    e.irq.type = 2;
                    m_pTraceLogger->TraceLog(e);
                }
#endif
#if !defined(GS_DISABLE_DISASSEMBLER)
                DisassembleNextOPCode();
#endif
                return m_iTStates;
            }
            else if (m_bIFF1 && m_bINTRequested && !m_bAfterEI)
            {
                LeaveHalt();
                m_bIFF1 = false;
                m_bIFF2 = false;
#if !defined(GS_DISABLE_DISASSEMBLER)
                u16 pc = PC.GetValue();
#endif
                // The interrupt acknowledge bus floats high, so IM 0 receives RST 38h.
                u16 interrupt_vector = 0x0038;
                u32 interrupt_tstates = 13;

                if (m_iInterruptMode == 2)
                {
                    u16 vector_address = (I << 8) | 0x00FF;
                    u8 l = m_pMemory->Read(vector_address);
                    u8 h = m_pMemory->Read(static_cast<u16> (vector_address + 1));
                    interrupt_vector = (h << 8) | l;
                    interrupt_tstates = 19;
                }

                StackPush(&PC);
                PC.SetValue(interrupt_vector);
                m_iTStates += interrupt_tstates;
                IncreaseR();
                WZ.SetValue(PC.GetValue());
                UpdateProActionReplay();
#if !defined(GS_DISABLE_DISASSEMBLER)
                m_debug_next_irq = 3;
                PushCallStack(pc, interrupt_vector, pc, m_pMemory->GetBank(interrupt_vector));
                if (m_pTraceLogger->IsEnabled(TRACE_CPU_IRQ))
                {
                    GS_Trace_Entry e = {};
                    e.type = TRACE_CPU_IRQ;
                    e.irq.pc = pc;
                    e.irq.vector = interrupt_vector;
                    e.irq.type = 3;
                    m_pTraceLogger->TraceLog(e);
                }
#endif
#if !defined(GS_DISABLE_DISASSEMBLER)
                DisassembleNextOPCode();
#endif
                return m_iTStates;
            }

            m_bAfterEI = false;
        }

        if (m_bHalt && m_bCycleAccurateHalt)
        {
            m_iHaltCycle = (m_iHaltCycle + 1) & 3;
            if (m_iHaltCycle == 0)
                IncreaseR();
            m_iTStates = 1;
            executed += 1;
            return 1;
        }

#if !defined(GS_DISABLE_DISASSEMBLER)
        u16 prev_pc = PC.GetValue();
#endif

        if (m_bInputLastCycle)
            ExecuteInputLastCycle();
        else
            ExecuteOPCode();
#if GS_PERF_DIAGNOSTICS
        ++m_instructionCount;
#endif

#if !defined(GS_DISABLE_DISASSEMBLER)
        if (m_pTraceLogger->IsEnabled(TRACE_CPU))
        {
            GS_Trace_Entry e = {};
            e.type = TRACE_CPU;
            e.cpu.pc = prev_pc;
            GS_Disassembler_Record* record = m_pMemory->GetDisassemblerRecord(prev_pc);
            e.cpu.bank = IsValidPointer(record) ? record->bank : 0;
            e.cpu.af = AF.GetValue();
            e.cpu.bc = BC.GetValue();
            e.cpu.de = DE.GetValue();
            e.cpu.hl = HL.GetValue();
            e.cpu.sp = SP.GetValue();
            m_pTraceLogger->TraceLog(e);
        }
#endif

        executed += m_iTStates;

        if (m_iInjectedTStates > 0)
        {
            executed += m_iInjectedTStates;
            m_iInjectedTStates = 0;
        }
    }

    return executed;
}

u64 Processor::GetInstructionCount() const
{
    return m_instructionCount;
}

u32 Processor::RunInstruction()
{
    u32 executed = 0;

    do
    {
        executed += RunFor(1);
    }
    while (m_bInputLastCycle);

    return executed;
}

void Processor::InjectTStates(u32 tstates)
{
    m_iInjectedTStates += tstates;
}

void Processor::RequestINT(bool assert)
{
    m_bINTRequested = assert;
}

void Processor::RequestNMI()
{
    m_bNMIRequested = true;
}

void Processor::SetTraceLogger(TraceLogger* pTraceLogger)
{
    m_pTraceLogger = pTraceLogger;
}

void Processor::DispatchOpcode(u8 opcode)
{
    switch (opcode)
    {
        case 0x00:
            OPCode0x00();
            break;
        case 0x01:
            OPCode0x01();
            break;
        case 0x02:
            OPCode0x02();
            break;
        case 0x03:
            OPCode0x03();
            break;
        case 0x04:
            OPCode0x04();
            break;
        case 0x05:
            OPCode0x05();
            break;
        case 0x06:
            OPCode0x06();
            break;
        case 0x07:
            OPCode0x07();
            break;
        case 0x08:
            OPCode0x08();
            break;
        case 0x09:
            OPCode0x09();
            break;
        case 0x0A:
            OPCode0x0A();
            break;
        case 0x0B:
            OPCode0x0B();
            break;
        case 0x0C:
            OPCode0x0C();
            break;
        case 0x0D:
            OPCode0x0D();
            break;
        case 0x0E:
            OPCode0x0E();
            break;
        case 0x0F:
            OPCode0x0F();
            break;
        case 0x10:
            OPCode0x10();
            break;
        case 0x11:
            OPCode0x11();
            break;
        case 0x12:
            OPCode0x12();
            break;
        case 0x13:
            OPCode0x13();
            break;
        case 0x14:
            OPCode0x14();
            break;
        case 0x15:
            OPCode0x15();
            break;
        case 0x16:
            OPCode0x16();
            break;
        case 0x17:
            OPCode0x17();
            break;
        case 0x18:
            OPCode0x18();
            break;
        case 0x19:
            OPCode0x19();
            break;
        case 0x1A:
            OPCode0x1A();
            break;
        case 0x1B:
            OPCode0x1B();
            break;
        case 0x1C:
            OPCode0x1C();
            break;
        case 0x1D:
            OPCode0x1D();
            break;
        case 0x1E:
            OPCode0x1E();
            break;
        case 0x1F:
            OPCode0x1F();
            break;
        case 0x20:
            OPCode0x20();
            break;
        case 0x21:
            OPCode0x21();
            break;
        case 0x22:
            OPCode0x22();
            break;
        case 0x23:
            OPCode0x23();
            break;
        case 0x24:
            OPCode0x24();
            break;
        case 0x25:
            OPCode0x25();
            break;
        case 0x26:
            OPCode0x26();
            break;
        case 0x27:
            OPCode0x27();
            break;
        case 0x28:
            OPCode0x28();
            break;
        case 0x29:
            OPCode0x29();
            break;
        case 0x2A:
            OPCode0x2A();
            break;
        case 0x2B:
            OPCode0x2B();
            break;
        case 0x2C:
            OPCode0x2C();
            break;
        case 0x2D:
            OPCode0x2D();
            break;
        case 0x2E:
            OPCode0x2E();
            break;
        case 0x2F:
            OPCode0x2F();
            break;
        case 0x30:
            OPCode0x30();
            break;
        case 0x31:
            OPCode0x31();
            break;
        case 0x32:
            OPCode0x32();
            break;
        case 0x33:
            OPCode0x33();
            break;
        case 0x34:
            OPCode0x34();
            break;
        case 0x35:
            OPCode0x35();
            break;
        case 0x36:
            OPCode0x36();
            break;
        case 0x37:
            OPCode0x37();
            break;
        case 0x38:
            OPCode0x38();
            break;
        case 0x39:
            OPCode0x39();
            break;
        case 0x3A:
            OPCode0x3A();
            break;
        case 0x3B:
            OPCode0x3B();
            break;
        case 0x3C:
            OPCode0x3C();
            break;
        case 0x3D:
            OPCode0x3D();
            break;
        case 0x3E:
            OPCode0x3E();
            break;
        case 0x3F:
            OPCode0x3F();
            break;
        case 0x40:
            OPCode0x40();
            break;
        case 0x41:
            OPCode0x41();
            break;
        case 0x42:
            OPCode0x42();
            break;
        case 0x43:
            OPCode0x43();
            break;
        case 0x44:
            OPCode0x44();
            break;
        case 0x45:
            OPCode0x45();
            break;
        case 0x46:
            OPCode0x46();
            break;
        case 0x47:
            OPCode0x47();
            break;
        case 0x48:
            OPCode0x48();
            break;
        case 0x49:
            OPCode0x49();
            break;
        case 0x4A:
            OPCode0x4A();
            break;
        case 0x4B:
            OPCode0x4B();
            break;
        case 0x4C:
            OPCode0x4C();
            break;
        case 0x4D:
            OPCode0x4D();
            break;
        case 0x4E:
            OPCode0x4E();
            break;
        case 0x4F:
            OPCode0x4F();
            break;
        case 0x50:
            OPCode0x50();
            break;
        case 0x51:
            OPCode0x51();
            break;
        case 0x52:
            OPCode0x52();
            break;
        case 0x53:
            OPCode0x53();
            break;
        case 0x54:
            OPCode0x54();
            break;
        case 0x55:
            OPCode0x55();
            break;
        case 0x56:
            OPCode0x56();
            break;
        case 0x57:
            OPCode0x57();
            break;
        case 0x58:
            OPCode0x58();
            break;
        case 0x59:
            OPCode0x59();
            break;
        case 0x5A:
            OPCode0x5A();
            break;
        case 0x5B:
            OPCode0x5B();
            break;
        case 0x5C:
            OPCode0x5C();
            break;
        case 0x5D:
            OPCode0x5D();
            break;
        case 0x5E:
            OPCode0x5E();
            break;
        case 0x5F:
            OPCode0x5F();
            break;
        case 0x60:
            OPCode0x60();
            break;
        case 0x61:
            OPCode0x61();
            break;
        case 0x62:
            OPCode0x62();
            break;
        case 0x63:
            OPCode0x63();
            break;
        case 0x64:
            OPCode0x64();
            break;
        case 0x65:
            OPCode0x65();
            break;
        case 0x66:
            OPCode0x66();
            break;
        case 0x67:
            OPCode0x67();
            break;
        case 0x68:
            OPCode0x68();
            break;
        case 0x69:
            OPCode0x69();
            break;
        case 0x6A:
            OPCode0x6A();
            break;
        case 0x6B:
            OPCode0x6B();
            break;
        case 0x6C:
            OPCode0x6C();
            break;
        case 0x6D:
            OPCode0x6D();
            break;
        case 0x6E:
            OPCode0x6E();
            break;
        case 0x6F:
            OPCode0x6F();
            break;
        case 0x70:
            OPCode0x70();
            break;
        case 0x71:
            OPCode0x71();
            break;
        case 0x72:
            OPCode0x72();
            break;
        case 0x73:
            OPCode0x73();
            break;
        case 0x74:
            OPCode0x74();
            break;
        case 0x75:
            OPCode0x75();
            break;
        case 0x76:
            OPCode0x76();
            break;
        case 0x77:
            OPCode0x77();
            break;
        case 0x78:
            OPCode0x78();
            break;
        case 0x79:
            OPCode0x79();
            break;
        case 0x7A:
            OPCode0x7A();
            break;
        case 0x7B:
            OPCode0x7B();
            break;
        case 0x7C:
            OPCode0x7C();
            break;
        case 0x7D:
            OPCode0x7D();
            break;
        case 0x7E:
            OPCode0x7E();
            break;
        case 0x7F:
            OPCode0x7F();
            break;
        case 0x80:
            OPCode0x80();
            break;
        case 0x81:
            OPCode0x81();
            break;
        case 0x82:
            OPCode0x82();
            break;
        case 0x83:
            OPCode0x83();
            break;
        case 0x84:
            OPCode0x84();
            break;
        case 0x85:
            OPCode0x85();
            break;
        case 0x86:
            OPCode0x86();
            break;
        case 0x87:
            OPCode0x87();
            break;
        case 0x88:
            OPCode0x88();
            break;
        case 0x89:
            OPCode0x89();
            break;
        case 0x8A:
            OPCode0x8A();
            break;
        case 0x8B:
            OPCode0x8B();
            break;
        case 0x8C:
            OPCode0x8C();
            break;
        case 0x8D:
            OPCode0x8D();
            break;
        case 0x8E:
            OPCode0x8E();
            break;
        case 0x8F:
            OPCode0x8F();
            break;
        case 0x90:
            OPCode0x90();
            break;
        case 0x91:
            OPCode0x91();
            break;
        case 0x92:
            OPCode0x92();
            break;
        case 0x93:
            OPCode0x93();
            break;
        case 0x94:
            OPCode0x94();
            break;
        case 0x95:
            OPCode0x95();
            break;
        case 0x96:
            OPCode0x96();
            break;
        case 0x97:
            OPCode0x97();
            break;
        case 0x98:
            OPCode0x98();
            break;
        case 0x99:
            OPCode0x99();
            break;
        case 0x9A:
            OPCode0x9A();
            break;
        case 0x9B:
            OPCode0x9B();
            break;
        case 0x9C:
            OPCode0x9C();
            break;
        case 0x9D:
            OPCode0x9D();
            break;
        case 0x9E:
            OPCode0x9E();
            break;
        case 0x9F:
            OPCode0x9F();
            break;
        case 0xA0:
            OPCode0xA0();
            break;
        case 0xA1:
            OPCode0xA1();
            break;
        case 0xA2:
            OPCode0xA2();
            break;
        case 0xA3:
            OPCode0xA3();
            break;
        case 0xA4:
            OPCode0xA4();
            break;
        case 0xA5:
            OPCode0xA5();
            break;
        case 0xA6:
            OPCode0xA6();
            break;
        case 0xA7:
            OPCode0xA7();
            break;
        case 0xA8:
            OPCode0xA8();
            break;
        case 0xA9:
            OPCode0xA9();
            break;
        case 0xAA:
            OPCode0xAA();
            break;
        case 0xAB:
            OPCode0xAB();
            break;
        case 0xAC:
            OPCode0xAC();
            break;
        case 0xAD:
            OPCode0xAD();
            break;
        case 0xAE:
            OPCode0xAE();
            break;
        case 0xAF:
            OPCode0xAF();
            break;
        case 0xB0:
            OPCode0xB0();
            break;
        case 0xB1:
            OPCode0xB1();
            break;
        case 0xB2:
            OPCode0xB2();
            break;
        case 0xB3:
            OPCode0xB3();
            break;
        case 0xB4:
            OPCode0xB4();
            break;
        case 0xB5:
            OPCode0xB5();
            break;
        case 0xB6:
            OPCode0xB6();
            break;
        case 0xB7:
            OPCode0xB7();
            break;
        case 0xB8:
            OPCode0xB8();
            break;
        case 0xB9:
            OPCode0xB9();
            break;
        case 0xBA:
            OPCode0xBA();
            break;
        case 0xBB:
            OPCode0xBB();
            break;
        case 0xBC:
            OPCode0xBC();
            break;
        case 0xBD:
            OPCode0xBD();
            break;
        case 0xBE:
            OPCode0xBE();
            break;
        case 0xBF:
            OPCode0xBF();
            break;
        case 0xC0:
            OPCode0xC0();
            break;
        case 0xC1:
            OPCode0xC1();
            break;
        case 0xC2:
            OPCode0xC2();
            break;
        case 0xC3:
            OPCode0xC3();
            break;
        case 0xC4:
            OPCode0xC4();
            break;
        case 0xC5:
            OPCode0xC5();
            break;
        case 0xC6:
            OPCode0xC6();
            break;
        case 0xC7:
            OPCode0xC7();
            break;
        case 0xC8:
            OPCode0xC8();
            break;
        case 0xC9:
            OPCode0xC9();
            break;
        case 0xCA:
            OPCode0xCA();
            break;
        case 0xCB:
            OPCode0xCB();
            break;
        case 0xCC:
            OPCode0xCC();
            break;
        case 0xCD:
            OPCode0xCD();
            break;
        case 0xCE:
            OPCode0xCE();
            break;
        case 0xCF:
            OPCode0xCF();
            break;
        case 0xD0:
            OPCode0xD0();
            break;
        case 0xD1:
            OPCode0xD1();
            break;
        case 0xD2:
            OPCode0xD2();
            break;
        case 0xD3:
            OPCode0xD3();
            break;
        case 0xD4:
            OPCode0xD4();
            break;
        case 0xD5:
            OPCode0xD5();
            break;
        case 0xD6:
            OPCode0xD6();
            break;
        case 0xD7:
            OPCode0xD7();
            break;
        case 0xD8:
            OPCode0xD8();
            break;
        case 0xD9:
            OPCode0xD9();
            break;
        case 0xDA:
            OPCode0xDA();
            break;
        case 0xDB:
            OPCode0xDB();
            break;
        case 0xDC:
            OPCode0xDC();
            break;
        case 0xDD:
            OPCode0xDD();
            break;
        case 0xDE:
            OPCode0xDE();
            break;
        case 0xDF:
            OPCode0xDF();
            break;
        case 0xE0:
            OPCode0xE0();
            break;
        case 0xE1:
            OPCode0xE1();
            break;
        case 0xE2:
            OPCode0xE2();
            break;
        case 0xE3:
            OPCode0xE3();
            break;
        case 0xE4:
            OPCode0xE4();
            break;
        case 0xE5:
            OPCode0xE5();
            break;
        case 0xE6:
            OPCode0xE6();
            break;
        case 0xE7:
            OPCode0xE7();
            break;
        case 0xE8:
            OPCode0xE8();
            break;
        case 0xE9:
            OPCode0xE9();
            break;
        case 0xEA:
            OPCode0xEA();
            break;
        case 0xEB:
            OPCode0xEB();
            break;
        case 0xEC:
            OPCode0xEC();
            break;
        case 0xED:
            OPCode0xED();
            break;
        case 0xEE:
            OPCode0xEE();
            break;
        case 0xEF:
            OPCode0xEF();
            break;
        case 0xF0:
            OPCode0xF0();
            break;
        case 0xF1:
            OPCode0xF1();
            break;
        case 0xF2:
            OPCode0xF2();
            break;
        case 0xF3:
            OPCode0xF3();
            break;
        case 0xF4:
            OPCode0xF4();
            break;
        case 0xF5:
            OPCode0xF5();
            break;
        case 0xF6:
            OPCode0xF6();
            break;
        case 0xF7:
            OPCode0xF7();
            break;
        case 0xF8:
            OPCode0xF8();
            break;
        case 0xF9:
            OPCode0xF9();
            break;
        case 0xFA:
            OPCode0xFA();
            break;
        case 0xFB:
            OPCode0xFB();
            break;
        case 0xFC:
            OPCode0xFC();
            break;
        case 0xFD:
            OPCode0xFD();
            break;
        case 0xFE:
            OPCode0xFE();
            break;
        case 0xFF:
            OPCode0xFF();
            break;
        default:
            InvalidOPCode();
            break;
    }
}
void Processor::DispatchOpcodeCB(u8 opcode)
{
    switch (opcode)
    {
        case 0x00:
            OPCodeCB0x00();
            break;
        case 0x01:
            OPCodeCB0x01();
            break;
        case 0x02:
            OPCodeCB0x02();
            break;
        case 0x03:
            OPCodeCB0x03();
            break;
        case 0x04:
            OPCodeCB0x04();
            break;
        case 0x05:
            OPCodeCB0x05();
            break;
        case 0x06:
            OPCodeCB0x06();
            break;
        case 0x07:
            OPCodeCB0x07();
            break;
        case 0x08:
            OPCodeCB0x08();
            break;
        case 0x09:
            OPCodeCB0x09();
            break;
        case 0x0A:
            OPCodeCB0x0A();
            break;
        case 0x0B:
            OPCodeCB0x0B();
            break;
        case 0x0C:
            OPCodeCB0x0C();
            break;
        case 0x0D:
            OPCodeCB0x0D();
            break;
        case 0x0E:
            OPCodeCB0x0E();
            break;
        case 0x0F:
            OPCodeCB0x0F();
            break;
        case 0x10:
            OPCodeCB0x10();
            break;
        case 0x11:
            OPCodeCB0x11();
            break;
        case 0x12:
            OPCodeCB0x12();
            break;
        case 0x13:
            OPCodeCB0x13();
            break;
        case 0x14:
            OPCodeCB0x14();
            break;
        case 0x15:
            OPCodeCB0x15();
            break;
        case 0x16:
            OPCodeCB0x16();
            break;
        case 0x17:
            OPCodeCB0x17();
            break;
        case 0x18:
            OPCodeCB0x18();
            break;
        case 0x19:
            OPCodeCB0x19();
            break;
        case 0x1A:
            OPCodeCB0x1A();
            break;
        case 0x1B:
            OPCodeCB0x1B();
            break;
        case 0x1C:
            OPCodeCB0x1C();
            break;
        case 0x1D:
            OPCodeCB0x1D();
            break;
        case 0x1E:
            OPCodeCB0x1E();
            break;
        case 0x1F:
            OPCodeCB0x1F();
            break;
        case 0x20:
            OPCodeCB0x20();
            break;
        case 0x21:
            OPCodeCB0x21();
            break;
        case 0x22:
            OPCodeCB0x22();
            break;
        case 0x23:
            OPCodeCB0x23();
            break;
        case 0x24:
            OPCodeCB0x24();
            break;
        case 0x25:
            OPCodeCB0x25();
            break;
        case 0x26:
            OPCodeCB0x26();
            break;
        case 0x27:
            OPCodeCB0x27();
            break;
        case 0x28:
            OPCodeCB0x28();
            break;
        case 0x29:
            OPCodeCB0x29();
            break;
        case 0x2A:
            OPCodeCB0x2A();
            break;
        case 0x2B:
            OPCodeCB0x2B();
            break;
        case 0x2C:
            OPCodeCB0x2C();
            break;
        case 0x2D:
            OPCodeCB0x2D();
            break;
        case 0x2E:
            OPCodeCB0x2E();
            break;
        case 0x2F:
            OPCodeCB0x2F();
            break;
        case 0x30:
            OPCodeCB0x30();
            break;
        case 0x31:
            OPCodeCB0x31();
            break;
        case 0x32:
            OPCodeCB0x32();
            break;
        case 0x33:
            OPCodeCB0x33();
            break;
        case 0x34:
            OPCodeCB0x34();
            break;
        case 0x35:
            OPCodeCB0x35();
            break;
        case 0x36:
            OPCodeCB0x36();
            break;
        case 0x37:
            OPCodeCB0x37();
            break;
        case 0x38:
            OPCodeCB0x38();
            break;
        case 0x39:
            OPCodeCB0x39();
            break;
        case 0x3A:
            OPCodeCB0x3A();
            break;
        case 0x3B:
            OPCodeCB0x3B();
            break;
        case 0x3C:
            OPCodeCB0x3C();
            break;
        case 0x3D:
            OPCodeCB0x3D();
            break;
        case 0x3E:
            OPCodeCB0x3E();
            break;
        case 0x3F:
            OPCodeCB0x3F();
            break;
        case 0x40:
            OPCodeCB0x40();
            break;
        case 0x41:
            OPCodeCB0x41();
            break;
        case 0x42:
            OPCodeCB0x42();
            break;
        case 0x43:
            OPCodeCB0x43();
            break;
        case 0x44:
            OPCodeCB0x44();
            break;
        case 0x45:
            OPCodeCB0x45();
            break;
        case 0x46:
            OPCodeCB0x46();
            break;
        case 0x47:
            OPCodeCB0x47();
            break;
        case 0x48:
            OPCodeCB0x48();
            break;
        case 0x49:
            OPCodeCB0x49();
            break;
        case 0x4A:
            OPCodeCB0x4A();
            break;
        case 0x4B:
            OPCodeCB0x4B();
            break;
        case 0x4C:
            OPCodeCB0x4C();
            break;
        case 0x4D:
            OPCodeCB0x4D();
            break;
        case 0x4E:
            OPCodeCB0x4E();
            break;
        case 0x4F:
            OPCodeCB0x4F();
            break;
        case 0x50:
            OPCodeCB0x50();
            break;
        case 0x51:
            OPCodeCB0x51();
            break;
        case 0x52:
            OPCodeCB0x52();
            break;
        case 0x53:
            OPCodeCB0x53();
            break;
        case 0x54:
            OPCodeCB0x54();
            break;
        case 0x55:
            OPCodeCB0x55();
            break;
        case 0x56:
            OPCodeCB0x56();
            break;
        case 0x57:
            OPCodeCB0x57();
            break;
        case 0x58:
            OPCodeCB0x58();
            break;
        case 0x59:
            OPCodeCB0x59();
            break;
        case 0x5A:
            OPCodeCB0x5A();
            break;
        case 0x5B:
            OPCodeCB0x5B();
            break;
        case 0x5C:
            OPCodeCB0x5C();
            break;
        case 0x5D:
            OPCodeCB0x5D();
            break;
        case 0x5E:
            OPCodeCB0x5E();
            break;
        case 0x5F:
            OPCodeCB0x5F();
            break;
        case 0x60:
            OPCodeCB0x60();
            break;
        case 0x61:
            OPCodeCB0x61();
            break;
        case 0x62:
            OPCodeCB0x62();
            break;
        case 0x63:
            OPCodeCB0x63();
            break;
        case 0x64:
            OPCodeCB0x64();
            break;
        case 0x65:
            OPCodeCB0x65();
            break;
        case 0x66:
            OPCodeCB0x66();
            break;
        case 0x67:
            OPCodeCB0x67();
            break;
        case 0x68:
            OPCodeCB0x68();
            break;
        case 0x69:
            OPCodeCB0x69();
            break;
        case 0x6A:
            OPCodeCB0x6A();
            break;
        case 0x6B:
            OPCodeCB0x6B();
            break;
        case 0x6C:
            OPCodeCB0x6C();
            break;
        case 0x6D:
            OPCodeCB0x6D();
            break;
        case 0x6E:
            OPCodeCB0x6E();
            break;
        case 0x6F:
            OPCodeCB0x6F();
            break;
        case 0x70:
            OPCodeCB0x70();
            break;
        case 0x71:
            OPCodeCB0x71();
            break;
        case 0x72:
            OPCodeCB0x72();
            break;
        case 0x73:
            OPCodeCB0x73();
            break;
        case 0x74:
            OPCodeCB0x74();
            break;
        case 0x75:
            OPCodeCB0x75();
            break;
        case 0x76:
            OPCodeCB0x76();
            break;
        case 0x77:
            OPCodeCB0x77();
            break;
        case 0x78:
            OPCodeCB0x78();
            break;
        case 0x79:
            OPCodeCB0x79();
            break;
        case 0x7A:
            OPCodeCB0x7A();
            break;
        case 0x7B:
            OPCodeCB0x7B();
            break;
        case 0x7C:
            OPCodeCB0x7C();
            break;
        case 0x7D:
            OPCodeCB0x7D();
            break;
        case 0x7E:
            OPCodeCB0x7E();
            break;
        case 0x7F:
            OPCodeCB0x7F();
            break;
        case 0x80:
            OPCodeCB0x80();
            break;
        case 0x81:
            OPCodeCB0x81();
            break;
        case 0x82:
            OPCodeCB0x82();
            break;
        case 0x83:
            OPCodeCB0x83();
            break;
        case 0x84:
            OPCodeCB0x84();
            break;
        case 0x85:
            OPCodeCB0x85();
            break;
        case 0x86:
            OPCodeCB0x86();
            break;
        case 0x87:
            OPCodeCB0x87();
            break;
        case 0x88:
            OPCodeCB0x88();
            break;
        case 0x89:
            OPCodeCB0x89();
            break;
        case 0x8A:
            OPCodeCB0x8A();
            break;
        case 0x8B:
            OPCodeCB0x8B();
            break;
        case 0x8C:
            OPCodeCB0x8C();
            break;
        case 0x8D:
            OPCodeCB0x8D();
            break;
        case 0x8E:
            OPCodeCB0x8E();
            break;
        case 0x8F:
            OPCodeCB0x8F();
            break;
        case 0x90:
            OPCodeCB0x90();
            break;
        case 0x91:
            OPCodeCB0x91();
            break;
        case 0x92:
            OPCodeCB0x92();
            break;
        case 0x93:
            OPCodeCB0x93();
            break;
        case 0x94:
            OPCodeCB0x94();
            break;
        case 0x95:
            OPCodeCB0x95();
            break;
        case 0x96:
            OPCodeCB0x96();
            break;
        case 0x97:
            OPCodeCB0x97();
            break;
        case 0x98:
            OPCodeCB0x98();
            break;
        case 0x99:
            OPCodeCB0x99();
            break;
        case 0x9A:
            OPCodeCB0x9A();
            break;
        case 0x9B:
            OPCodeCB0x9B();
            break;
        case 0x9C:
            OPCodeCB0x9C();
            break;
        case 0x9D:
            OPCodeCB0x9D();
            break;
        case 0x9E:
            OPCodeCB0x9E();
            break;
        case 0x9F:
            OPCodeCB0x9F();
            break;
        case 0xA0:
            OPCodeCB0xA0();
            break;
        case 0xA1:
            OPCodeCB0xA1();
            break;
        case 0xA2:
            OPCodeCB0xA2();
            break;
        case 0xA3:
            OPCodeCB0xA3();
            break;
        case 0xA4:
            OPCodeCB0xA4();
            break;
        case 0xA5:
            OPCodeCB0xA5();
            break;
        case 0xA6:
            OPCodeCB0xA6();
            break;
        case 0xA7:
            OPCodeCB0xA7();
            break;
        case 0xA8:
            OPCodeCB0xA8();
            break;
        case 0xA9:
            OPCodeCB0xA9();
            break;
        case 0xAA:
            OPCodeCB0xAA();
            break;
        case 0xAB:
            OPCodeCB0xAB();
            break;
        case 0xAC:
            OPCodeCB0xAC();
            break;
        case 0xAD:
            OPCodeCB0xAD();
            break;
        case 0xAE:
            OPCodeCB0xAE();
            break;
        case 0xAF:
            OPCodeCB0xAF();
            break;
        case 0xB0:
            OPCodeCB0xB0();
            break;
        case 0xB1:
            OPCodeCB0xB1();
            break;
        case 0xB2:
            OPCodeCB0xB2();
            break;
        case 0xB3:
            OPCodeCB0xB3();
            break;
        case 0xB4:
            OPCodeCB0xB4();
            break;
        case 0xB5:
            OPCodeCB0xB5();
            break;
        case 0xB6:
            OPCodeCB0xB6();
            break;
        case 0xB7:
            OPCodeCB0xB7();
            break;
        case 0xB8:
            OPCodeCB0xB8();
            break;
        case 0xB9:
            OPCodeCB0xB9();
            break;
        case 0xBA:
            OPCodeCB0xBA();
            break;
        case 0xBB:
            OPCodeCB0xBB();
            break;
        case 0xBC:
            OPCodeCB0xBC();
            break;
        case 0xBD:
            OPCodeCB0xBD();
            break;
        case 0xBE:
            OPCodeCB0xBE();
            break;
        case 0xBF:
            OPCodeCB0xBF();
            break;
        case 0xC0:
            OPCodeCB0xC0();
            break;
        case 0xC1:
            OPCodeCB0xC1();
            break;
        case 0xC2:
            OPCodeCB0xC2();
            break;
        case 0xC3:
            OPCodeCB0xC3();
            break;
        case 0xC4:
            OPCodeCB0xC4();
            break;
        case 0xC5:
            OPCodeCB0xC5();
            break;
        case 0xC6:
            OPCodeCB0xC6();
            break;
        case 0xC7:
            OPCodeCB0xC7();
            break;
        case 0xC8:
            OPCodeCB0xC8();
            break;
        case 0xC9:
            OPCodeCB0xC9();
            break;
        case 0xCA:
            OPCodeCB0xCA();
            break;
        case 0xCB:
            OPCodeCB0xCB();
            break;
        case 0xCC:
            OPCodeCB0xCC();
            break;
        case 0xCD:
            OPCodeCB0xCD();
            break;
        case 0xCE:
            OPCodeCB0xCE();
            break;
        case 0xCF:
            OPCodeCB0xCF();
            break;
        case 0xD0:
            OPCodeCB0xD0();
            break;
        case 0xD1:
            OPCodeCB0xD1();
            break;
        case 0xD2:
            OPCodeCB0xD2();
            break;
        case 0xD3:
            OPCodeCB0xD3();
            break;
        case 0xD4:
            OPCodeCB0xD4();
            break;
        case 0xD5:
            OPCodeCB0xD5();
            break;
        case 0xD6:
            OPCodeCB0xD6();
            break;
        case 0xD7:
            OPCodeCB0xD7();
            break;
        case 0xD8:
            OPCodeCB0xD8();
            break;
        case 0xD9:
            OPCodeCB0xD9();
            break;
        case 0xDA:
            OPCodeCB0xDA();
            break;
        case 0xDB:
            OPCodeCB0xDB();
            break;
        case 0xDC:
            OPCodeCB0xDC();
            break;
        case 0xDD:
            OPCodeCB0xDD();
            break;
        case 0xDE:
            OPCodeCB0xDE();
            break;
        case 0xDF:
            OPCodeCB0xDF();
            break;
        case 0xE0:
            OPCodeCB0xE0();
            break;
        case 0xE1:
            OPCodeCB0xE1();
            break;
        case 0xE2:
            OPCodeCB0xE2();
            break;
        case 0xE3:
            OPCodeCB0xE3();
            break;
        case 0xE4:
            OPCodeCB0xE4();
            break;
        case 0xE5:
            OPCodeCB0xE5();
            break;
        case 0xE6:
            OPCodeCB0xE6();
            break;
        case 0xE7:
            OPCodeCB0xE7();
            break;
        case 0xE8:
            OPCodeCB0xE8();
            break;
        case 0xE9:
            OPCodeCB0xE9();
            break;
        case 0xEA:
            OPCodeCB0xEA();
            break;
        case 0xEB:
            OPCodeCB0xEB();
            break;
        case 0xEC:
            OPCodeCB0xEC();
            break;
        case 0xED:
            OPCodeCB0xED();
            break;
        case 0xEE:
            OPCodeCB0xEE();
            break;
        case 0xEF:
            OPCodeCB0xEF();
            break;
        case 0xF0:
            OPCodeCB0xF0();
            break;
        case 0xF1:
            OPCodeCB0xF1();
            break;
        case 0xF2:
            OPCodeCB0xF2();
            break;
        case 0xF3:
            OPCodeCB0xF3();
            break;
        case 0xF4:
            OPCodeCB0xF4();
            break;
        case 0xF5:
            OPCodeCB0xF5();
            break;
        case 0xF6:
            OPCodeCB0xF6();
            break;
        case 0xF7:
            OPCodeCB0xF7();
            break;
        case 0xF8:
            OPCodeCB0xF8();
            break;
        case 0xF9:
            OPCodeCB0xF9();
            break;
        case 0xFA:
            OPCodeCB0xFA();
            break;
        case 0xFB:
            OPCodeCB0xFB();
            break;
        case 0xFC:
            OPCodeCB0xFC();
            break;
        case 0xFD:
            OPCodeCB0xFD();
            break;
        case 0xFE:
            OPCodeCB0xFE();
            break;
        case 0xFF:
            OPCodeCB0xFF();
            break;
        default:
            InvalidOPCode();
            break;
    }
}
void Processor::DispatchOpcodeED(u8 opcode)
{
    switch (opcode)
    {
        case 0x40:
            OPCodeED0x40();
            break;
        case 0x41:
            OPCodeED0x41();
            break;
        case 0x42:
            OPCodeED0x42();
            break;
        case 0x43:
            OPCodeED0x43();
            break;
        case 0x44:
            OPCodeED0x44();
            break;
        case 0x45:
            OPCodeED0x45();
            break;
        case 0x46:
            OPCodeED0x46();
            break;
        case 0x47:
            OPCodeED0x47();
            break;
        case 0x48:
            OPCodeED0x48();
            break;
        case 0x49:
            OPCodeED0x49();
            break;
        case 0x4A:
            OPCodeED0x4A();
            break;
        case 0x4B:
            OPCodeED0x4B();
            break;
        case 0x4C:
            OPCodeED0x4C();
            break;
        case 0x4D:
            OPCodeED0x4D();
            break;
        case 0x4E:
            OPCodeED0x4E();
            break;
        case 0x4F:
            OPCodeED0x4F();
            break;
        case 0x50:
            OPCodeED0x50();
            break;
        case 0x51:
            OPCodeED0x51();
            break;
        case 0x52:
            OPCodeED0x52();
            break;
        case 0x53:
            OPCodeED0x53();
            break;
        case 0x54:
            OPCodeED0x54();
            break;
        case 0x55:
            OPCodeED0x55();
            break;
        case 0x56:
            OPCodeED0x56();
            break;
        case 0x57:
            OPCodeED0x57();
            break;
        case 0x58:
            OPCodeED0x58();
            break;
        case 0x59:
            OPCodeED0x59();
            break;
        case 0x5A:
            OPCodeED0x5A();
            break;
        case 0x5B:
            OPCodeED0x5B();
            break;
        case 0x5C:
            OPCodeED0x5C();
            break;
        case 0x5D:
            OPCodeED0x5D();
            break;
        case 0x5E:
            OPCodeED0x5E();
            break;
        case 0x5F:
            OPCodeED0x5F();
            break;
        case 0x60:
            OPCodeED0x60();
            break;
        case 0x61:
            OPCodeED0x61();
            break;
        case 0x62:
            OPCodeED0x62();
            break;
        case 0x63:
            OPCodeED0x63();
            break;
        case 0x64:
            OPCodeED0x64();
            break;
        case 0x65:
            OPCodeED0x65();
            break;
        case 0x66:
            OPCodeED0x66();
            break;
        case 0x67:
            OPCodeED0x67();
            break;
        case 0x68:
            OPCodeED0x68();
            break;
        case 0x69:
            OPCodeED0x69();
            break;
        case 0x6A:
            OPCodeED0x6A();
            break;
        case 0x6B:
            OPCodeED0x6B();
            break;
        case 0x6C:
            OPCodeED0x6C();
            break;
        case 0x6D:
            OPCodeED0x6D();
            break;
        case 0x6E:
            OPCodeED0x6E();
            break;
        case 0x6F:
            OPCodeED0x6F();
            break;
        case 0x70:
            OPCodeED0x70();
            break;
        case 0x71:
            OPCodeED0x71();
            break;
        case 0x72:
            OPCodeED0x72();
            break;
        case 0x73:
            OPCodeED0x73();
            break;
        case 0x74:
            OPCodeED0x74();
            break;
        case 0x75:
            OPCodeED0x75();
            break;
        case 0x76:
            OPCodeED0x76();
            break;
        case 0x77:
            InvalidOPCode();
            break;
        case 0x78:
            OPCodeED0x78();
            break;
        case 0x79:
            OPCodeED0x79();
            break;
        case 0x7A:
            OPCodeED0x7A();
            break;
        case 0x7B:
            OPCodeED0x7B();
            break;
        case 0x7C:
            OPCodeED0x7C();
            break;
        case 0x7D:
            OPCodeED0x7D();
            break;
        case 0x7E:
            OPCodeED0x7E();
            break;
        case 0xA0:
            OPCodeED0xA0();
            break;
        case 0xA1:
            OPCodeED0xA1();
            break;
        case 0xA2:
            OPCodeED0xA2();
            break;
        case 0xA3:
            OPCodeED0xA3();
            break;
        case 0xA4:
            InvalidOPCode();
            break;
        case 0xA5:
            InvalidOPCode();
            break;
        case 0xA6:
            InvalidOPCode();
            break;
        case 0xA7:
            InvalidOPCode();
            break;
        case 0xA8:
            OPCodeED0xA8();
            break;
        case 0xA9:
            OPCodeED0xA9();
            break;
        case 0xAA:
            OPCodeED0xAA();
            break;
        case 0xAB:
            OPCodeED0xAB();
            break;
        case 0xAC:
            InvalidOPCode();
            break;
        case 0xAD:
            InvalidOPCode();
            break;
        case 0xAE:
            InvalidOPCode();
            break;
        case 0xAF:
            InvalidOPCode();
            break;
        case 0xB0:
            OPCodeED0xB0();
            break;
        case 0xB1:
            OPCodeED0xB1();
            break;
        case 0xB2:
            OPCodeED0xB2();
            break;
        case 0xB3:
            OPCodeED0xB3();
            break;
        case 0xB4:
            InvalidOPCode();
            break;
        case 0xB5:
            InvalidOPCode();
            break;
        case 0xB6:
            InvalidOPCode();
            break;
        case 0xB7:
            InvalidOPCode();
            break;
        case 0xB8:
            OPCodeED0xB8();
            break;
        case 0xB9:
            OPCodeED0xB9();
            break;
        case 0xBA:
            OPCodeED0xBA();
            break;
        case 0xBB:
            OPCodeED0xBB();
            break;
        default:
            InvalidOPCode();
            break;
    }
}
void Processor::ExecuteOPCode()
{
    u8 opcode = FetchOPCode();

    switch (opcode)
    {
        case 0xDD:
        case 0xFD:
        {
            int more_prefixes = false;
            while ((opcode == 0xDD) || (opcode == 0xFD))
            {
                m_CurrentPrefix = opcode;
                opcode = FetchOPCode();
                if (more_prefixes)
                    m_iTStates += 4;
                more_prefixes = true;
                IncreaseR();
            }
            break;
        }
        default:
        {
            m_CurrentPrefix = 0x00;
            break;
        }
    }

    switch (opcode)
    {
        case 0xCB:
        {
            IncreaseR();

            if (IsPrefixedInstruction())
            {
                m_bPrefixedCBOpcode = true;
                m_PrefixedCBValue = m_pMemory->Read(PC.GetValue());
                PC.Increment();
            }
            else
                IncreaseR();

            opcode = FetchOPCode();

#if GS_PERF_DIAGNOSTICS
            if ((m_instructionCount & 63) == 0) ++m_opcodeCBHistogram[opcode];
#endif
            DispatchOpcodeCB(opcode);

            if (IsPrefixedInstruction())
            {
                m_iTStates += kOPCodeXYCBTStates[opcode];
                m_bPrefixedCBOpcode = false;
            }
            else
                m_iTStates += kOPCodeCBTStates[opcode];

            break;
        }
        case 0xED:
        {
            IncreaseR();
            IncreaseR();

            if (IsPrefixedInstruction())
                m_iTStates += 4;
            m_CurrentPrefix = 0x00;
            opcode = FetchOPCode();

#if GS_PERF_DIAGNOSTICS
            if ((m_instructionCount & 63) == 0) ++m_opcodeEDHistogram[opcode];
#endif
            DispatchOpcodeED(opcode);

            m_iTStates += kOPCodeEDTStates[opcode];
            break;
        }
        default:
        {
            if (!m_bInputLastCycle)
                IncreaseR();

#if GS_PERF_DIAGNOSTICS
            if ((m_instructionCount & 63) == 0) ++m_opcodeHistogram[opcode];
#endif
            DispatchOpcode(opcode);

            if (IsPrefixedInstruction())
                m_iTStates += kOPCodeXYTStates[opcode];
            else
                m_iTStates += kOPCodeTStates[opcode];

            if (m_bBranchTaken)
            {
                m_bBranchTaken = false;
                m_iTStates += kOPCodeTStatesBranched[opcode];
            }
            break;
        }
    }
}

void Processor::InvalidOPCode()
{
#ifdef GS_DEBUG
    u16 opcode_address = PC.GetValue() - 1;
    u16 prefix_address = PC.GetValue() - 2;
    u8 opcode = m_pMemory->Read(opcode_address);
    u8 prefix = m_pMemory->Read(prefix_address);

    switch (prefix)
    {
        case 0xCB:
        {
            Debug("--> ** INVALID CB OP Code (%X) at $%.4X -- %s", opcode, opcode_address, kOPCodeCBNames[opcode]);
            break;
        }
        case 0xED:
        {
            Debug("--> ** INVALID ED OP Code (%X) at $%.4X -- %s", opcode, opcode_address, kOPCodeEDNames[opcode]);
            break;
        }
        default:
        {
            Debug("--> ** INVALID OP Code (%X) at $%.4X -- %s", opcode, opcode_address, kOPCodeNames[opcode]);
        }
    }
#endif
}

void Processor::UndocumentedOPCode()
{
#ifdef GS_DEBUG
    u16 opcode_address = PC.GetValue() - 1;
    u8 opcode = m_pMemory->Read(opcode_address);

    Debug("--> ** UNDOCUMENTED OP Code (%X) at $%.4X -- %s", opcode, opcode_address, kOPCodeNames[opcode]);
#endif
}

void Processor::DisassembleNextOPCode()
{
#ifndef GS_DISABLE_DISASSEMBLER

    CheckBreakpoints();

    u16 address = PC.GetValue();
    GS_Disassembler_Record* record = m_pMemory->GetOrCreateDisassemblerRecord(address);

    assert(IsValidPointer(record));

    int opcode_size = record->size;

    bool changed = (opcode_size == 0);

    if (!changed)
    {
        int maxSize = std::min(opcode_size, 4);
        for (int i = 0; i < maxSize; i++)
        {
            u8 mem_byte = m_pMemory->DebugRetrieve(address + i);
            if (record->opcodes[i] != mem_byte)
            {
                changed = true;
                break;
            }
        }
    }

    if (!changed && record->size != 0)
    {
        if (m_debug_next_irq > 0)
        {
            record->irq = m_debug_next_irq;
            m_debug_next_irq = 0;
        }
        return;
    }

    PopulateDisassemblerRecord(record, address);
#endif
}

void Processor::FormatDisassemblerDataBytes(char* text, size_t text_size, const u8* bytes, int size)
{
    const char* directive = (m_disassembler_syntax == GS_Disassembler_Syntax_WLADX) ? ".db" : "db";

    int pos = snprintf(text, text_size, "{n}%s ", directive);
    for (int i = 0; i < size && pos > 0 && pos < (int)text_size; i++)
        pos += snprintf(text + pos, text_size - pos, "%s{o}$%02X", (i == 0) ? "" : ",", bytes[i]);
}

void Processor::SetDisassemblerOperandText(GS_Disassembler_Record* record, const char* text)
{
    if (!IsValidPointer(text) || (text[0] == 0))
        return;

    const char* match = record->name;
    const char* last_match = NULL;
    while ((match = strstr(match, text)) != NULL)
    {
        last_match = match;
        match++;
    }

    if (IsValidPointer(last_match))
    {
        record->operand_offset = (int)(last_match - record->name);
        record->operand_length = (int)strlen(text);
    }
}

void Processor::SetDisassemblerOperand(GS_Disassembler_Record* record, u16 address, bool is_zp, const char* text)
{
    record->has_operand_address = true;
    record->operand_address = address;
    record->operand_is_zp = is_zp;
    SetDisassemblerOperandText(record, text);
}

void Processor::PopulateDisassemblerRecord(GS_Disassembler_Record* record, u16 address)
{
#ifndef GS_DISABLE_DISASSEMBLER

    record->address = m_pMemory->GetPhysicalAddress(address);
    record->bank = m_pMemory->GetBank(address);
    record->name[0] = 0;
    record->bytes[0] = 0;
    record->segment[0] = 0;
    record->size = 0;
    record->jump = false;
    record->jump_address = 0;
    record->jump_bank = 0;
    record->subroutine = false;
    record->irq = 0;
    record->has_operand_address = false;
    record->operand_address = 0;
    record->operand_is_zp = false;
    record->operand_offset = 0;
    record->operand_length = 0;

    if (m_debug_next_irq > 0)
    {
        record->irq = m_debug_next_irq;
        m_debug_next_irq = 0;
    }

    std::vector<u8> bytes;
    u16 opcode_temp_addr = address;
    u8 opcode_temp = m_pMemory->DebugRetrieve(opcode_temp_addr);
    u8 ddfd_mod = 0;
    int first = 0;

    while ((opcode_temp == 0xDD) || (opcode_temp == 0xFD))
    {
        ddfd_mod = opcode_temp;
        bytes.push_back(opcode_temp);
        opcode_temp_addr++;
        first++;
        opcode_temp = m_pMemory->DebugRetrieve(opcode_temp_addr);
    }

    for (int i = 0; i < 5; i++)
        bytes.push_back(m_pMemory->DebugRetrieve(opcode_temp_addr + i));

    u8 opcode = bytes[first];
    stOPCodeInfo info;

    bool prefixed = false;

    if (opcode == 0xCB)
    {
        prefixed = true;
        if (ddfd_mod == 0xDD)
        {
            opcode = bytes[first + 2];
            info = kOPCodeDDCBNames[opcode];
        }
        else if (ddfd_mod == 0xFD)
        {
            opcode = bytes[first + 2];
            info = kOPCodeFDCBNames[opcode];
        }
        else
        {
            opcode = bytes[first + 1];
            info = kOPCodeCBNames[opcode];
        }
    }
    else if (opcode == 0xED)
    {
        prefixed = true;
        opcode = bytes[first + 1];
        info = kOPCodeEDNames[opcode];
    }
    else
    {
        if (ddfd_mod == 0xDD)
            info = kOPCodeDDNames[opcode];
        else if (ddfd_mod == 0xFD)
            info = kOPCodeFDNames[opcode];
        else
            info = kOPCodeNames[opcode];
    }

    if (first > 0 && bytes[first] == 0xED)
        record->size = info.size + first;
    else
        record->size = info.size + (first > 1 ? (first - 1) : 0);

    int pos = 0;
    for (int i = 0; i < (int)bytes.size(); i++)
    {
        if (i < record->size)
        {
            static const char hex_chars[] = "0123456789ABCDEF";
            u8 byte = bytes[i];
            record->bytes[pos++] = hex_chars[byte >> 4];
            record->bytes[pos++] = hex_chars[byte & 0x0F];
            record->bytes[pos++] = ' ';
        }

        if (i < 7)
            record->opcodes[i] = bytes[i];
    }
    record->bytes[pos] = 0;

    InvalidateOverlappingRecords(address, (u8)record->size);

    int name_first = first + (prefixed ? 1 : 0);
    const char* format = info.name[m_disassembler_syntax];

    switch (info.type)
    {
        case GS_OPCode_Type_Implied:
            strcpy(record->name, format);
            break;
        case GS_OPCode_Type_Index:
            snprintf(record->name, sizeof(record->name), format, (s8)bytes[name_first]);
            break;
        case GS_OPCode_Type_1b:
        {
            snprintf(record->name, sizeof(record->name), format, bytes[name_first + 1]);
            char operand_text[8];
            snprintf(operand_text, sizeof(operand_text), "$%02X", bytes[name_first + 1]);
            SetDisassemblerOperandText(record, operand_text);
            break;
        }
        case GS_OPCode_Type_2b:
        {
            u16 operand = (bytes[name_first + 2] << 8) | bytes[name_first + 1];
            record->has_operand_address = true;
            record->operand_address = operand;
            if (!prefixed && (opcode == 0xC3 || opcode == 0xCD || (opcode & 0xC7) == 0xC2 || (opcode & 0xC7) == 0xC4))
            {
                record->jump = true;
                record->jump_address = operand;
                record->jump_bank = m_pMemory->GetBank(operand);
            }
            snprintf(record->name, sizeof(record->name), format, operand);
            char operand_text[8];
            snprintf(operand_text, sizeof(operand_text), "$%04X", operand);
            SetDisassemblerOperand(record, operand, false, operand_text);
            break;
        }
        case GS_OPCode_Type_Indexed:
            snprintf(record->name, sizeof(record->name), format, (s8)bytes[name_first + 1]);
            break;
        case GS_OPCode_Type_Relative:
        {
            u16 jump_address = address + record->size + (s8)bytes[name_first + 1];
            record->has_operand_address = true;
            record->operand_address = jump_address;
            record->jump = true;
            record->jump_address = jump_address;
            record->jump_bank = m_pMemory->GetBank(jump_address);
            if (m_disassembler_syntax == GS_Disassembler_Syntax_Gearsystem)
            {
                snprintf(record->name, sizeof(record->name), format, jump_address, (s8)bytes[name_first + 1]);
                char operand_text[8];
                snprintf(operand_text, sizeof(operand_text), "$%04X", jump_address);
                SetDisassemblerOperandText(record, operand_text);
            }
            else
            {
                snprintf(record->name, sizeof(record->name), format, bytes[name_first + 1]);
                char operand_text[16];
                if (m_disassembler_syntax == GS_Disassembler_Syntax_TNIASM)
                    snprintf(operand_text, sizeof(operand_text), "($+2+$%02X)", bytes[name_first + 1]);
                else if (m_disassembler_syntax == GS_Disassembler_Syntax_Z88DK)
                    snprintf(operand_text, sizeof(operand_text), "$+2+$%02X", bytes[name_first + 1]);
                else
                    snprintf(operand_text, sizeof(operand_text), "$%02X", bytes[name_first + 1]);
                SetDisassemblerOperandText(record, operand_text);
            }
            break;
        }
        case GS_OPCode_Type_Indexed_1b:
            snprintf(record->name, sizeof(record->name), format, (s8)bytes[name_first + 1], bytes[name_first + 2]);
            break;
        case GS_OPCode_Type_Data:
            if (m_disassembler_syntax == GS_Disassembler_Syntax_Gearsystem)
                strcpy(record->name, format);
            else
                FormatDisassemblerDataBytes(record->name, sizeof(record->name), bytes.data(), record->size);
            break;
        default:
            strcpy(record->name, "PARSE ERROR");
    }

    // Subroutine detection: CALL nn, CALL cc,nn, RST xx
    if (!prefixed)
    {
        // CALL nn (0xCD), CALL cc,nn (0xC4,0xCC,0xD4,0xDC,0xE4,0xEC,0xF4,0xFC)
        if (opcode == 0xCD || (opcode & 0xC7) == 0xC4)
        {
            record->subroutine = true;
        }
        // RST xx (0xC7,0xCF,0xD7,0xDF,0xE7,0xEF,0xF7,0xFF)
        if ((opcode & 0xC7) == 0xC7)
        {
            u16 rst_address = opcode & 0x38;
            record->subroutine = true;
            record->jump = true;
            record->jump_address = rst_address;
            record->jump_bank = m_pMemory->GetBank(rst_address);
        }
    }

    if (record->irq > 0 && record->irq < 4)
    {
        static const char* k_irq_auto_symbol_format[4] = {
            "????_%02X_%04X", "RESET_%02X_%04X", "NMI_%02X_%04X",
            "INT_%02X_%04X"
        };
        snprintf(record->auto_symbol, 64, k_irq_auto_symbol_format[record->irq], record->bank, address);
    }

    if (record->jump)
    {
        GS_Disassembler_Record* target = m_pMemory->GetOrCreateDisassemblerRecord(record->jump_address);
        if (IsValidPointer(target))
        {
            if (record->subroutine)
            {
                snprintf(target->auto_symbol, 64, "SUB_%02X_%04X", record->jump_bank, record->jump_address);
            }
            else if (strncmp(target->auto_symbol, "SUB_", 4) != 0)
            {
                snprintf(target->auto_symbol, 64, "TAG_%02X_%04X", record->jump_bank, record->jump_address);
            }
        }
    }

    // Segment detection
    if (m_pMemory->GetCurrentSlot() == Memory::BiosSlot && address < 0xC000)
    {
        strncpy_fit(record->segment, "BIOS ", sizeof(record->segment));
    }
    else if (address < 0xC000)
    {
        strncpy_fit(record->segment, "ROM  ", sizeof(record->segment));
    }
    else
    {
        strncpy_fit(record->segment, "RAM  ", sizeof(record->segment));
    }

#else
    UNUSED(record);
    UNUSED(address);
#endif
}

void Processor::InvalidateOverlappingRecords(u16 address, u8 opcode_size)
{
#ifndef GS_DISABLE_DISASSEMBLER
    for (int back = 1; back < 7; ++back)
    {
        int prev_start = (int)address - back;
        if (prev_start < 0)
            continue;

        GS_Disassembler_Record* prev = m_pMemory->GetDisassemblerRecord((u16)prev_start);
        if (!IsValidPointer(prev) || prev->size == 0)
            continue;

        int distance = address - prev_start;
        if (prev->size > distance)
        {
            prev->size = 0;
            prev->name[0] = 0;
            prev->bytes[0] = 0;
        }
    }

    if (opcode_size > 1)
    {
        for (int fwd = 1; fwd < opcode_size; ++fwd)
        {
            u16 fwd_addr = address + fwd;
            GS_Disassembler_Record* fwd_record = m_pMemory->GetDisassemblerRecord(fwd_addr);
            if (!IsValidPointer(fwd_record) || fwd_record->size == 0)
                continue;

            fwd_record->size = 0;
            fwd_record->name[0] = 0;
            fwd_record->bytes[0] = 0;
        }
    }
#else
    UNUSED(address);
    UNUSED(opcode_size);
#endif
}

void Processor::DisassembleAhead(int count)
{
    DisassembleAhead(PC.GetValue(), count, 0);
}

void Processor::DisassembleAhead(u16 start_address, int count, int depth)
{
#ifndef GS_DISABLE_DISASSEMBLER
    if (depth > 3)
        return;

    u16 address = start_address;
    int disassembled = 0;

    while (disassembled < count && address < 0xFFFF)
    {
        GS_Disassembler_Record* record = m_pMemory->GetOrCreateDisassemblerRecord(address);

        if (!IsValidPointer(record))
            break;

        int prev_size = record->size;
        bool changed = (prev_size == 0);

        if (!changed)
        {
            int maxSize = std::min(prev_size, 4);
            for (int i = 0; i < maxSize; i++)
            {
                u8 mem_byte = m_pMemory->DebugRetrieve(address + i);
                if (record->opcodes[i] != mem_byte)
                {
                    changed = true;
                    break;
                }
            }
        }

        if (changed || record->size == 0)
        {
            int saved_irq = m_debug_next_irq;
            m_debug_next_irq = 0;
            PopulateDisassemblerRecord(record, address);
            m_debug_next_irq = saved_irq;
        }

        if (record->jump)
        {
            u8 jump_bank = m_pMemory->GetBank(record->jump_address);
            if (jump_bank != 0xFF)
                DisassembleAhead(record->jump_address, count / 2, depth + 1);
        }

        if (record->size == 0)
            break;

        if ((u32)address + record->size > 0xFFFF)
            break;

        address += record->size;
        disassembled++;

        // Stop at unconditional control flow (end of block)
        // RET=0xC9, JP nn=0xC3, JR=0x18, HALT=0x76, JP (HL)=0xE9
        u8 first_byte = record->opcodes[0];
        if (first_byte == 0xC9 || first_byte == 0xC3 || first_byte == 0x18 || first_byte == 0x76 || first_byte == 0xE9)
            break;
        // DD E9=JP (IX), FD E9=JP (IY)
        if ((first_byte == 0xDD || first_byte == 0xFD) && record->size >= 2 && record->opcodes[1] == 0xE9)
            break;
        // RETI=ED4D, RETN=ED45, undocumented RETN*=ED55/5D/65/6D/75/7D
        if (first_byte == 0xED && record->size >= 2)
        {
            u8 second_byte = record->opcodes[1];
            if (second_byte == 0x45 || second_byte == 0x4D ||
                second_byte == 0x55 || second_byte == 0x5D ||
                second_byte == 0x65 || second_byte == 0x6D ||
                second_byte == 0x75 || second_byte == 0x7D)
                break;
        }
    }
#else
    UNUSED(start_address);
    UNUSED(count);
    UNUSED(depth);
#endif
}

void Processor::CheckBreakpoints()
{
#ifndef GS_DISABLE_DISASSEMBLER

    m_cpu_breakpoint_hit = (m_breakpoints_irq_enabled && m_debug_next_irq > 0);
    m_run_to_breakpoint_hit = false;

    if (m_run_to_breakpoint_requested)
    {
        if (PC.GetValue() == m_run_to_breakpoint.address1)
        {
            m_run_to_breakpoint_hit = true;
            m_run_to_breakpoint_requested = false;
            return;
        }
    }

    if (!m_breakpoints_enabled)
        return;

    for (int i = 0; i < (int)m_breakpoints.size(); i++)
    {
        GS_Breakpoint* brk = &m_breakpoints[i];

        if (!brk->enabled)
            continue;
        if (!brk->execute)
            continue;
        if (brk->type != GS_BREAKPOINT_TYPE_ROMRAM)
            continue;

        if (brk->range)
        {
            if (PC.GetValue() >= brk->address1 && PC.GetValue() <= brk->address2)
            {
                m_cpu_breakpoint_hit = true;
                m_run_to_breakpoint_requested = false;
                return;
            }
        }
        else
        {
            if (PC.GetValue() == brk->address1)
            {
                m_cpu_breakpoint_hit = true;
                m_run_to_breakpoint_requested = false;
                return;
            }
        }
    }

#endif
}

bool Processor::BreakpointHit()
{
    return (m_cpu_breakpoint_hit || m_memory_breakpoint_hit);
}

bool Processor::MemoryBreakpointHit()
{
    return m_memory_breakpoint_hit;
}

bool Processor::RunToBreakpointHit()
{
    return m_run_to_breakpoint_hit;
}

bool Processor::Halted()
{
    return m_bHalt;
}

void Processor::EnableBreakpoints(bool enable, bool irqs)
{
    m_breakpoints_enabled = enable;
    m_breakpoints_irq_enabled = irqs;
}

void Processor::ResetBreakpoints()
{
    m_breakpoints.clear();
}

bool Processor::AddBreakpoint(int type, char* text, bool read, bool write, bool execute)
{
    int input_len = (int)strlen(text);
    GS_Breakpoint brk;
    brk.enabled = true;
    brk.type = type;
    brk.address1 = 0;
    brk.address2 = 0;
    brk.range = false;
    brk.read = read;
    brk.write = write;
    brk.execute = execute;

    if (!read && !write && !execute)
        return false;

    if ((input_len == 9) && (text[4] == '-'))
    {
        // format: AAAA-BBBB
        if (parse_hex_string(text, 4, &brk.address1) &&
            parse_hex_string(text + 5, 4, &brk.address2))
        {
            brk.range = true;
        }
        else
        {
            return false;
        }
    }
    else if ((input_len > 0) && (input_len <= 4))
    {
        // format: AAAA
        if (!parse_hex_string(text, input_len, &brk.address1))
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    u16 max_address = 0xFFFF;
    if (type == GS_BREAKPOINT_TYPE_VRAM)
        max_address = 0x3FFF;
    else if (type == GS_BREAKPOINT_TYPE_VDP_REGISTER)
        max_address = 0x000A;
    else if (type == GS_BREAKPOINT_TYPE_CRAM)
        max_address = 0x003F;

    if (brk.address1 > max_address)
        return false;
    if (brk.range && (brk.address2 > max_address))
        return false;

    bool found = false;

    for (long unsigned int b = 0; b < m_breakpoints.size(); b++)
    {
        GS_Breakpoint* item = &m_breakpoints[b];

        if (item->type != brk.type)
            continue;

        if (brk.range)
        {
            if (item->range && (item->address1 == brk.address1) && (item->address2 == brk.address2))
            {
                found = true;
                break;
            }
        }
        else
        {
            if (!item->range && (item->address1 == brk.address1))
            {
                found = true;
                break;
            }
        }
    }

    if (!found)
        m_breakpoints.push_back(brk);

    return true;
}

bool Processor::AddBreakpoint(u16 address)
{
    char text[6];
    snprintf(text, 6, "%04X", address);
    return AddBreakpoint(GS_BREAKPOINT_TYPE_ROMRAM, text, false, false, true);
}

void Processor::AddRunToBreakpoint(u16 address)
{
    m_run_to_breakpoint.enabled = true;
    m_run_to_breakpoint.type = GS_BREAKPOINT_TYPE_ROMRAM;
    m_run_to_breakpoint.address1 = address;
    m_run_to_breakpoint.address2 = 0;
    m_run_to_breakpoint.range = false;
    m_run_to_breakpoint.read = false;
    m_run_to_breakpoint.write = false;
    m_run_to_breakpoint.execute = true;
    m_run_to_breakpoint_requested = true;
}

void Processor::RemoveBreakpoint(int type, u16 address)
{
    for (long unsigned int b = 0; b < m_breakpoints.size(); b++)
    {
        GS_Breakpoint* item = &m_breakpoints[b];

        if (!item->range && (item->address1 == address) && (item->type == type))
        {
            m_breakpoints.erase(m_breakpoints.begin() + b);
            break;
        }
    }
}

bool Processor::IsBreakpoint(int type, u16 address)
{
    for (long unsigned int b = 0; b < m_breakpoints.size(); b++)
    {
        GS_Breakpoint* item = &m_breakpoints[b];

        if (!item->range && (item->address1 == address) && (item->type == type))
        {
            return true;
        }
    }

    return false;
}

void Processor::ClearDisassemblerCallStack()
{
    while(!m_disassembler_call_stack.empty())
        m_disassembler_call_stack.pop();
}

void Processor::CheckMemoryBreakpoints(int type, u16 address, bool read)
{
#ifndef GS_DISABLE_DISASSEMBLER

    if (!m_breakpoints_enabled)
        return;

    for (int i = 0; i < (int)m_breakpoints.size(); i++)
    {
        GS_Breakpoint* brk = &m_breakpoints[i];

        if (!brk->enabled)
            continue;
        if (brk->type != type)
            continue;
        if (read && !brk->read)
            continue;
        if (!read && !brk->write)
            continue;

        if (brk->range)
        {
            if (address >= brk->address1 && address <= brk->address2)
            {
                m_memory_breakpoint_hit = true;
                m_run_to_breakpoint_requested = false;
                return;
            }
        }
        else
        {
            if (address == brk->address1)
            {
                m_memory_breakpoint_hit = true;
                m_run_to_breakpoint_requested = false;
                return;
            }
        }
    }
#else
    UNUSED(type);
    UNUSED(address);
    UNUSED(read);
#endif
}

void Processor::SaveState(std::ostream& stream)
{
    using namespace std;

    u16 af = AF.GetValue();
    u16 bc = BC.GetValue();
    u16 de = DE.GetValue();
    u16 hl = HL.GetValue();
    u16 af2 = AF2.GetValue();
    u16 bc2 = BC2.GetValue();
    u16 de2 = DE2.GetValue();
    u16 hl2 = HL2.GetValue();
    u16 sp = SP.GetValue();
    u16 pc = PC.GetValue();
    u16 ix = IX.GetValue();
    u16 iy = IY.GetValue();
    u16 wz = WZ.GetValue();
    u8 i = I;
    u8 r = R;

    stream.write(reinterpret_cast<const char*> (&af), sizeof(af));
    stream.write(reinterpret_cast<const char*> (&bc), sizeof(bc));
    stream.write(reinterpret_cast<const char*> (&de), sizeof(de));
    stream.write(reinterpret_cast<const char*> (&hl), sizeof(hl));
    stream.write(reinterpret_cast<const char*> (&af2), sizeof(af2));
    stream.write(reinterpret_cast<const char*> (&bc2), sizeof(bc2));
    stream.write(reinterpret_cast<const char*> (&de2), sizeof(de2));
    stream.write(reinterpret_cast<const char*> (&hl2), sizeof(hl2));
    stream.write(reinterpret_cast<const char*> (&sp), sizeof(sp));
    stream.write(reinterpret_cast<const char*> (&pc), sizeof(pc));
    stream.write(reinterpret_cast<const char*> (&ix), sizeof(ix));
    stream.write(reinterpret_cast<const char*> (&iy), sizeof(iy));
    stream.write(reinterpret_cast<const char*> (&wz), sizeof(wz));
    stream.write(reinterpret_cast<const char*> (&i), sizeof(i));
    stream.write(reinterpret_cast<const char*> (&r), sizeof(r));

    stream.write(reinterpret_cast<const char*> (&m_bIFF1), sizeof(m_bIFF1));
    stream.write(reinterpret_cast<const char*> (&m_bIFF2), sizeof(m_bIFF2));
    stream.write(reinterpret_cast<const char*> (&m_bHalt), sizeof(m_bHalt));
    stream.write(reinterpret_cast<const char*> (&m_bBranchTaken), sizeof(m_bBranchTaken));
    stream.write(reinterpret_cast<const char*> (&m_iTStates), sizeof(m_iTStates));
    stream.write(reinterpret_cast<const char*> (&m_bAfterEI), sizeof(m_bAfterEI));
    stream.write(reinterpret_cast<const char*> (&m_iInterruptMode), sizeof(m_iInterruptMode));
    stream.write(reinterpret_cast<const char*> (&m_CurrentPrefix), sizeof(m_CurrentPrefix));
    stream.write(reinterpret_cast<const char*> (&m_bINTRequested), sizeof(m_bINTRequested));
    stream.write(reinterpret_cast<const char*> (&m_bNMIRequested), sizeof(m_bNMIRequested));
    stream.write(reinterpret_cast<const char*> (&m_bPrefixedCBOpcode), sizeof(m_bPrefixedCBOpcode));
    stream.write(reinterpret_cast<const char*> (&m_PrefixedCBValue), sizeof(m_PrefixedCBValue));
    stream.write(reinterpret_cast<const char*> (&m_bInputLastCycle), sizeof(m_bInputLastCycle));
    stream.write(reinterpret_cast<const char*> (&m_Q), sizeof(m_Q));
    stream.write(reinterpret_cast<const char*> (&m_QTemp), sizeof(m_QTemp));
}

void Processor::LoadState(std::istream& stream, int version)
{
    using namespace std;

    u16 af, bc, de, hl, af2, bc2, de2, hl2, sp, pc, ix, iy, wz;
    u8 i, r;

    stream.read(reinterpret_cast<char*> (&af), sizeof(af));
    stream.read(reinterpret_cast<char*> (&bc), sizeof(bc));
    stream.read(reinterpret_cast<char*> (&de), sizeof(de));
    stream.read(reinterpret_cast<char*> (&hl), sizeof(hl));
    stream.read(reinterpret_cast<char*> (&af2), sizeof(af2));
    stream.read(reinterpret_cast<char*> (&bc2), sizeof(bc2));
    stream.read(reinterpret_cast<char*> (&de2), sizeof(de2));
    stream.read(reinterpret_cast<char*> (&hl2), sizeof(hl2));
    stream.read(reinterpret_cast<char*> (&sp), sizeof(sp));
    stream.read(reinterpret_cast<char*> (&pc), sizeof(pc));
    stream.read(reinterpret_cast<char*> (&ix), sizeof(ix));
    stream.read(reinterpret_cast<char*> (&iy), sizeof(iy));
    stream.read(reinterpret_cast<char*> (&wz), sizeof(wz));
    stream.read(reinterpret_cast<char*> (&i), sizeof(i));
    stream.read(reinterpret_cast<char*> (&r), sizeof(r));

    AF.SetValue(af);
    BC.SetValue(bc);
    DE.SetValue(de);
    HL.SetValue(hl);
    AF2.SetValue(af2);
    BC2.SetValue(bc2);
    DE2.SetValue(de2);
    HL2.SetValue(hl2);
    SP.SetValue(sp);
    PC.SetValue(pc);
    IX.SetValue(ix);
    IY.SetValue(iy);
    WZ.SetValue(wz);
    I = i;
    R = r;

    stream.read(reinterpret_cast<char*> (&m_bIFF1), sizeof(m_bIFF1));
    stream.read(reinterpret_cast<char*> (&m_bIFF2), sizeof(m_bIFF2));
    stream.read(reinterpret_cast<char*> (&m_bHalt), sizeof(m_bHalt));
    stream.read(reinterpret_cast<char*> (&m_bBranchTaken), sizeof(m_bBranchTaken));
    stream.read(reinterpret_cast<char*> (&m_iTStates), sizeof(m_iTStates));
    stream.read(reinterpret_cast<char*> (&m_bAfterEI), sizeof(m_bAfterEI));
    stream.read(reinterpret_cast<char*> (&m_iInterruptMode), sizeof(m_iInterruptMode));
    stream.read(reinterpret_cast<char*> (&m_CurrentPrefix), sizeof(m_CurrentPrefix));
    stream.read(reinterpret_cast<char*> (&m_bINTRequested), sizeof(m_bINTRequested));
    stream.read(reinterpret_cast<char*> (&m_bNMIRequested), sizeof(m_bNMIRequested));
    stream.read(reinterpret_cast<char*> (&m_bPrefixedCBOpcode), sizeof(m_bPrefixedCBOpcode));
    stream.read(reinterpret_cast<char*> (&m_PrefixedCBValue), sizeof(m_PrefixedCBValue));
    stream.read(reinterpret_cast<char*> (&m_bInputLastCycle), sizeof(m_bInputLastCycle));

    if (version >= 106)
    {
        stream.read(reinterpret_cast<char*> (&m_Q), sizeof(m_Q));
        stream.read(reinterpret_cast<char*> (&m_QTemp), sizeof(m_QTemp));
    }
    else
    {
        m_Q = 0;
        m_QTemp = 0;
    }
}

void Processor::SetProActionReplayCheat(const char* szCheat)
{
    std::string code(szCheat);
    for (std::string::iterator p = code.begin(); code.end() != p; ++p)
        *p = toupper(*p);

    if ((code.length() == 8) || (code.length() == 9))
    {
        int offset = 0;
        if (code.length() == 8)
        {
            offset = 1;
        }

        ProActionReplayCode par;

        par.value = (AsHex(code[7-offset]) << 4 | AsHex(code[8-offset])) & 0xFF;
        par.address = ((AsHex(code[2]) << 12) | (AsHex(code[3]) << 8) | (AsHex(code[5-offset]) << 4) | AsHex(code[6-offset])) & 0xFFFF;

        m_ProActionReplayList.push_back(par);
    }
}

void Processor::ClearProActionReplayCheats()
{
    m_ProActionReplayList.clear();
}

void Processor::UpdateProActionReplay()
{
    std::list<ProActionReplayCode>::iterator it;

    for (it = m_ProActionReplayList.begin(); it != m_ProActionReplayList.end(); it++)
    {
        m_pMemory->Write(it->address, it->value);
    }
}

Processor::ProcessorState* Processor::GetState()
{
    return &m_ProcessorState;
}


