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

#include "SmsIOPorts.h"
#include "MissileDefense3DPatcher.h"

SmsIOPorts::SmsIOPorts(Audio* pAudio, Video* pVideo, Input* pInput, Cartridge* pCartridge, Memory* pMemory, Processor* pProcessor)
{
    m_pAudio = pAudio;
    m_pVideo = pVideo;
    m_pInput = pInput;
    m_pCartridge = pCartridge;
    m_pMemory = pMemory;
    m_pProcessor = pProcessor;
    InitPointer(m_pTraceLogger);
    Reset();
}

SmsIOPorts::~SmsIOPorts()
{
}

void SmsIOPorts::SetTraceLogger(TraceLogger* pTraceLogger)
{
    m_pTraceLogger = pTraceLogger;
}

void SmsIOPorts::Reset()
{
    m_Port3F = 0xFF;
    ResetMissileDefenseShot();
}

void SmsIOPorts::ResetMissileDefenseShot()
{
    m_md3dShotActive = false;
    m_md3dPollHigh = true;
    m_md3dSample = 0;
    m_md3dH = 0;
    m_md3dV = 0;
}

void SmsIOPorts::SaveState(std::ostream& stream)
{
    using namespace std;

    stream.write(reinterpret_cast<const char*> (&m_Port3F), sizeof(m_Port3F));
    stream.write(reinterpret_cast<const char*> (&m_md3dShotActive), sizeof(m_md3dShotActive));
    stream.write(reinterpret_cast<const char*> (&m_md3dPollHigh), sizeof(m_md3dPollHigh));
    stream.write(reinterpret_cast<const char*> (&m_md3dSample), sizeof(m_md3dSample));
    stream.write(reinterpret_cast<const char*> (&m_md3dH), sizeof(m_md3dH));
    stream.write(reinterpret_cast<const char*> (&m_md3dV), sizeof(m_md3dV));
}

void SmsIOPorts::LoadState(std::istream& stream)
{
    using namespace std;

    stream.read(reinterpret_cast<char*> (&m_Port3F), sizeof(m_Port3F));
    stream.read(reinterpret_cast<char*> (&m_md3dShotActive), sizeof(m_md3dShotActive));
    stream.read(reinterpret_cast<char*> (&m_md3dPollHigh), sizeof(m_md3dPollHigh));
    stream.read(reinterpret_cast<char*> (&m_md3dSample), sizeof(m_md3dSample));
    stream.read(reinterpret_cast<char*> (&m_md3dH), sizeof(m_md3dH));
    stream.read(reinterpret_cast<char*> (&m_md3dV), sizeof(m_md3dV));
}
