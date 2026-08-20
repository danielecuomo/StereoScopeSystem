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

#include "YM2413.h"

YM2413::YM2413()
{
    InitPointer(m_pBuffer);
    m_iCycleCounter = 0;
    m_iSampleCounter = 0;
    m_iBufferIndex = 0;
    m_ElapsedCycles = 0;
    m_iClockRate = 0;
    m_RegisterF2 = 0;
    m_CurrentSample = 0;
    m_bEnabled = false;
    m_iSampleRateFactor = 0;
}

YM2413::~YM2413()
{
    SafeDeleteArray(m_pBuffer);
}

void YM2413::Init(int clockRate)
{
    m_pBuffer = new s16[GS_AUDIO_BUFFER_SIZE];
    YM2413Init();
    Reset(clockRate);
}

void YM2413::Reset(int clockRate)
{
    m_iClockRate = clockRate;
    m_iSampleRateFactor = (int)(((s64)GS_AUDIO_SAMPLE_RATE * (1 << kYM2413SampleAccuracy) + (m_iClockRate / 2)) / m_iClockRate);
    m_ElapsedCycles = 0;
    m_CurrentSample = 0;
    m_iCycleCounter = 0;
    m_iSampleCounter = 0;
    m_iBufferIndex = 0;
    m_RegisterF2 = 0;
    m_CurrentSample = 0;
    m_bEnabled = false;

    YM2413ResetChip();

    for (int i = 0; i < GS_AUDIO_BUFFER_SIZE; i++)
    {
        m_pBuffer[i] = 0;
    }
}

void YM2413::Write(u8 port, u8 value)
{
    if (port & 0x01)
    {
        Sync();
    }

    YM2413Write(port, value);
}

u8 YM2413::Read()
{
    return YM2413Read();
}

void YM2413::Tick(unsigned int clockCycles)
{
    m_ElapsedCycles += clockCycles;
}

int YM2413::EndFrame(s16* pSampleBuffer)
{
    // The YM2413 is normally disabled for SMS cartridges. The old path still
    // iterated once per master clock to manufacture silence, which is needless
    // CPU work. Preserve the exact sample-counter/buffer-index progression but
    // collapse the disabled case to O(number of generated samples).
    if (!m_bEnabled)
    {
        const int scale = (1 << kYM2413SampleAccuracy);
        const int samples = (int)(((s64)m_iSampleCounter + (s64)m_ElapsedCycles * m_iSampleRateFactor) / scale);
        m_iSampleCounter = (int)(((s64)m_iSampleCounter + (s64)m_ElapsedCycles * m_iSampleRateFactor) % scale);

        int ret = m_iBufferIndex;
        if (pSampleBuffer && samples > 0)
        {
            int remaining = samples * 2;
            int index = m_iBufferIndex;
            while (remaining > 0)
            {
                const int chunk = (remaining < (GS_AUDIO_BUFFER_SIZE - index)) ? remaining : (GS_AUDIO_BUFFER_SIZE - index);
                memset(m_pBuffer + index, 0, sizeof(s16) * chunk);
                remaining -= chunk;
                index += chunk;
                if (index >= GS_AUDIO_BUFFER_SIZE) index = 0;
            }
        }
        m_iBufferIndex += samples * 2;
        while (m_iBufferIndex >= GS_AUDIO_BUFFER_SIZE) m_iBufferIndex -= GS_AUDIO_BUFFER_SIZE;
        m_ElapsedCycles = 0;
        if (pSampleBuffer && ret > 0)
            memcpy(pSampleBuffer, m_pBuffer, sizeof(s16) * ret);
        return ret;
    }

    Sync();

    int ret = 0;

    if (IsValidPointer(pSampleBuffer))
    {
        ret = m_iBufferIndex;

        for (int i = 0; i < m_iBufferIndex; i++)
        {
            pSampleBuffer[i] = m_pBuffer[i];
        }
    }

    m_iBufferIndex = 0;

    return ret;
}

void YM2413::Enable(bool bEnabled)
{
    if (m_bEnabled == bEnabled)
        return;

    Sync();
    m_bEnabled = bEnabled;
}

void YM2413::Sync()
{
    const int scale = (1 << kYM2413SampleAccuracy);
    int remaining = m_ElapsedCycles;

    // The YM2413 clock is the SMS master clock divided by 72, while the
    // output sample clock is derived independently from the master clock.
    // The old implementation visited every master-clock cycle. On ARM11
    // that means roughly 3.58 million iterations per NTSC frame for an
    // enabled YM2413, even though only ~50k YM/sample events are required.
    // That CPU load is large enough to starve NDSP and produces the audible
    // gaps seen by SegaScope/YM2413 games.
    //
    // Advance directly to the next YM update or output-sample event. This
    // preserves the event ordering of the old cycle-by-cycle implementation:
    // when both events land on the same master cycle, YM2413Update() happens
    // first and the newly generated value is emitted as that sample.
    while (remaining > 0)
    {
        const int cyclesToYm = m_bEnabled ? (72 - m_iCycleCounter) : remaining + 1;
        const int cyclesToSample =
            (m_iSampleCounter >= scale) ? 1 :
            (scale - m_iSampleCounter + m_iSampleRateFactor - 1) / m_iSampleRateFactor;

        const int step = std::min(remaining, std::min(cyclesToYm, cyclesToSample));

        if (m_bEnabled)
        {
            m_iCycleCounter += step;
        }

        m_iSampleCounter += step * m_iSampleRateFactor;
        remaining -= step;

        if (m_bEnabled && m_iCycleCounter >= 72)
        {
            m_iCycleCounter -= 72;
            m_CurrentSample = YM2413Update();
        }

        if (m_iSampleCounter >= scale)
        {
            m_iSampleCounter -= scale;

            s16 sample = m_bEnabled ? m_CurrentSample : 0;
            m_pBuffer[m_iBufferIndex] = sample;
            m_pBuffer[m_iBufferIndex + 1] = sample;
            m_iBufferIndex += 2;

            if (m_iBufferIndex >= GS_AUDIO_BUFFER_SIZE)
            {
                Debug("YM2413 Audio buffer overflow");
                m_iBufferIndex = 0;
            }
        }
    }

    m_ElapsedCycles = 0;
}

void YM2413::SaveState(std::ostream& stream)
{
    stream.write(reinterpret_cast<const char*>(&m_iCycleCounter), sizeof(int));
    stream.write(reinterpret_cast<const char*>(&m_iSampleCounter), sizeof(int));
    stream.write(reinterpret_cast<const char*>(&m_iBufferIndex), sizeof(int));
    stream.write(reinterpret_cast<const char*>(&m_ElapsedCycles), sizeof(int));
    stream.write(reinterpret_cast<const char*>(&m_iClockRate), sizeof(int));
    stream.write(reinterpret_cast<const char*>(&m_RegisterF2), sizeof(u8));
    stream.write(reinterpret_cast<const char*>(&m_CurrentSample), sizeof(s16));
    stream.write(reinterpret_cast<const char*>(&m_bEnabled), sizeof(bool));
    stream.write(reinterpret_cast<const char*>(&m_iSampleRateFactor), sizeof(int));
    stream.write(reinterpret_cast<const char*>(m_pBuffer), sizeof(s16) * GS_AUDIO_BUFFER_SIZE);

    unsigned char* context = YM2413GetContextPtr();
    unsigned int contex_size = YM2413GetContextSize();
    stream.write(reinterpret_cast<const char*>(context), contex_size);
}

void YM2413::LoadState(std::istream& stream)
{
    stream.read(reinterpret_cast<char*>(&m_iCycleCounter), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iSampleCounter), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iBufferIndex), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_ElapsedCycles), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iClockRate), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_RegisterF2), sizeof(u8));
    stream.read(reinterpret_cast<char*>(&m_CurrentSample), sizeof(s16));
    stream.read(reinterpret_cast<char*>(&m_bEnabled), sizeof(bool));
    stream.read(reinterpret_cast<char*>(&m_iSampleRateFactor), sizeof(int));
    stream.read(reinterpret_cast<char*>(m_pBuffer), sizeof(s16) * GS_AUDIO_BUFFER_SIZE);

    unsigned char* context = YM2413GetContextPtr();
    unsigned int context_size = YM2413GetContextSize();
    stream.read(reinterpret_cast<char*>(context), context_size);

    m_iSampleRateFactor = (int)(((s64)GS_AUDIO_SAMPLE_RATE * (1 << kYM2413SampleAccuracy) + (m_iClockRate / 2)) / m_iClockRate);
}

void YM2413::LoadStateV1(std::istream& stream)
{
    stream.read(reinterpret_cast<char*>(&m_iCycleCounter), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iSampleCounter), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iBufferIndex), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_ElapsedCycles), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_iClockRate), sizeof(int));
    stream.read(reinterpret_cast<char*>(&m_RegisterF2), sizeof(u8));
    stream.read(reinterpret_cast<char*>(&m_CurrentSample), sizeof(s16));
    stream.read(reinterpret_cast<char*>(&m_bEnabled), sizeof(bool));
    stream.read(reinterpret_cast<char*>(&m_iSampleRateFactor), sizeof(int));
    stream.seekg(sizeof(s16) * GS_AUDIO_BUFFER_SIZE_V1, std::ios::cur);
    memset(m_pBuffer, 0, sizeof(s16) * GS_AUDIO_BUFFER_SIZE);

    unsigned char* context = YM2413GetContextPtr();
    unsigned int context_size = YM2413GetContextSize();
    stream.read(reinterpret_cast<char*>(context), context_size);

    m_iSampleRateFactor = (int)(((s64)GS_AUDIO_SAMPLE_RATE * (1 << kYM2413SampleAccuracy) + (m_iClockRate / 2)) / m_iClockRate);
}
