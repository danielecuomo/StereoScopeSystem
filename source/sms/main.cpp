#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

#include "GearsystemCore.h"
#include "Processor.h"
#include "Input.h"
#include "definitions.h"
#include "log.h"
#include "menu_cartridge_data.h"

bool g_mcp_stdio_mode = false;

static constexpr int FB_W = 256;
static constexpr int FB_H = 192;
static constexpr int TEX_W = 256;
static constexpr int TEX_H = 256;
static constexpr int GS_FB_W = GS_RESOLUTION_MAX_WIDTH_WITH_OVERSCAN;
static constexpr int AUDIO_BUFFERS = 8;
static constexpr int AUDIO_SAMPLES_MAX = GS_AUDIO_BUFFER_SIZE;
static constexpr int AUDIO_FIFO_SAMPLES = 32768;

static constexpr int VIDEO_TEXTURE_SLOTS = 3;
static C3D_Tex gameTex[VIDEO_TEXTURE_SLOTS * 2];
static Tex3DS_SubTexture gameSubTex[VIDEO_TEXTURE_SLOTS * 2];
static C2D_Image gameImage[VIDEO_TEXTURE_SLOTS * 2];
// Each eye owns its own triple-buffer cursor.  The two eyes are produced on
// alternating emulator frames, so a single shared cursor would pair a new
// left image with an unrelated/stale right image (visible as flicker).
static int videoTextureSlot[2] = { 0, 0 };
static u16* uploadBuffer = nullptr;
static u8* frameBuffer = nullptr;

static ndspWaveBuf audioWave[AUDIO_BUFFERS];
static s16* audioBuffers[AUDIO_BUFFERS] = {};
static int audioIndex = 0;
static s16* audioFifo = nullptr;
static int audioFifoHead = 0;
static int audioFifoTail = 0;
static int audioFifoCount = 0;
static unsigned long long audioBufferWaits = 0;
static bool audioInitialized = false;
static bool textureInitialized = false;
static bool stereoActive = false;
static int previousGlassesEye = -1;
static bool display3DEnabled = false;
static C3D_RenderTarget* menuTarget = nullptr;
static C3D_Tex menuIconTex;
static Tex3DS_SubTexture menuIconSubTex;
static C2D_Image menuIconImage;
static u16* menuIconUploadBuffer = nullptr;
static bool menuIconInitialized = false;
static C2D_TextBuf gameInstructionsTextBuf = nullptr;
static C2D_Text gameInstructionsText;

struct GameShortcut
{
    const char* title;
    u16 productCode;
    const char* romPath;
};

static const GameShortcut kSegaScopeGames[8] =
{
    { "Blade Eagle 3-D",       0xEDFB, nullptr },
    { "Line of Fire",          0xDB85, nullptr },
    { "Maze Hunter 3-D",       0x9387, nullptr },
    { "Missile Defense 3-D",   0x154A, nullptr },
    { "Out Run 3-D",            0x4358, nullptr },
    { "Poseidon Wars 3-D",     0x3E00, nullptr },
    { "Space Harrier 3-D",     0x4786, nullptr },
    { "Zaxxon 3-D",            0x2767, nullptr }
};

static std::string joinPath(const std::string& base, const std::string& name)
{
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

static bool isSmsFile(const char* name)
{
    if (!name) return false;
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".sms") == 0;
}

static bool readSmsProductCode(const std::string& path, u16& productCode)
{
    FILE* rom = fopen(path.c_str(), "rb");
    if (!rom)
        return false;

    // The Sega header is normally at one of these offsets. Some ROM dumps
    // carry a 512-byte copier/header prefix, so check both layouts.
    static const long headerOffsets[] =
    {
        0x7FF0, 0x1FF0, 0x3FF0,
        0x81F0, 0x21F0, 0x41F0
    };

    unsigned char header[0x10];
    bool found = false;

    for (size_t i = 0; i < sizeof(headerOffsets) / sizeof(headerOffsets[0]); ++i)
    {
        if (fseek(rom, headerOffsets[i], SEEK_SET) != 0)
            continue;

        if (fread(header, 1, sizeof(header), rom) != sizeof(header))
            continue;

        if (memcmp(header, "TMR SEGA", 8) == 0)
        {
            // Sega SMS/GG product code occupies bytes 0x0A-0x0B of
            // the 16-byte cartridge header.
            productCode = (static_cast<u16>(header[0x0A]) << 8) |
                          static_cast<u16>(header[0x0B]);
            found = true;
            break;
        }
    }

    fclose(rom);
    return found;
}

static bool shortcutMatches(const GameShortcut& game, const std::string& path)
{
    u16 productCode = 0;
    if (!readSmsProductCode(path, productCode))
        return false;

    // The menu title is independent of the filename. Identification is based
    // exclusively on the product number stored in the ROM header.
    return productCode == game.productCode;
}

static void scanShortcutDirectory(const std::string& path, std::vector<std::string>& found)
{
    DIR* dir = opendir(path.c_str());
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;
        if (!isSmsFile(ent->d_name))
            continue;

        const std::string romPath = joinPath(path, ent->d_name);

        for (int i = 0; i < 8; ++i)
        {
            if (found[i].empty() && shortcutMatches(kSegaScopeGames[i], romPath))
            {
                found[i] = romPath;
                break;
            }
        }
    }

    closedir(dir);
}

static std::vector<std::string> findShortcutRoms()
{
    // The ROM directory is fixed by the application layout. Do not scan the
    // SD card or search recursively: this makes the menu open immediately.
    static const char* romDirectory = "sdmc:/3ds/sms_roms";
    std::vector<std::string> found(8);
    scanShortcutDirectory(romDirectory, found);
    return found;
}

static const char* shortcutTitleForRom(const std::string& path)
{
    u16 productCode = 0;
    if (readSmsProductCode(path, productCode))
    {
        for (const GameShortcut& game : kSegaScopeGames)
        {
            if (game.productCode == productCode)
                return game.title;
        }
    }

    return path.c_str();
}

struct FrameDiagnostics
{
    u64 emuMs = 0;
    u64 videoMs = 0;
    u64 audioMs = 0;
    u64 uploadMs = 0;
    u64 presentMs = 0;
    u64 totalMs = 0;
    u64 frames = 0;
    u64 clockCycles = 0;
    u64 instructions = 0;
};

#if GS_PERF_DIAGNOSTICS
static void printFrameDiagnostics(const FrameDiagnostics& d, GS_Region region, const Processor* processor)
{
    const double framesPerSecond = d.totalMs ? (1000.0 * (double)d.frames / (double)d.totalMs) : 0.0;
    const double avgEmu = d.frames ? (double)d.emuMs / (double)d.frames : 0.0;
    const double avgVideo = d.frames ? (double)d.videoMs / (double)d.frames : 0.0;
    const double avgAudio = d.frames ? (double)d.audioMs / (double)d.frames : 0.0;
    const double avgUpload = d.frames ? (double)d.uploadMs / (double)d.frames : 0.0;
    const double avgPresent = d.frames ? (double)d.presentMs / (double)d.frames : 0.0;
    const double avgTotal = d.frames ? (double)d.totalMs / (double)d.frames : 0.0;
    const double avgClock = d.frames ? (double)d.clockCycles / (double)d.frames : 0.0;
    const double avgInstructions = d.frames ? (double)d.instructions / (double)d.frames : 0.0;
    const double instPerSecond = d.totalMs ? (1000.0 * (double)d.instructions / (double)d.totalMs) : 0.0;

    printf("\x1b[10;1HDEBUG TIMING (1s window)\n");
    printf("Region: %-4s FPS: %6.2f Clock/frame: %7.0f\n",
           region == Region_PAL ? "PAL" : "NTSC", framesPerSecond, avgClock);
    printf("Z80: %7.0f ins/frame  %6.3f MIPS\n", avgInstructions, instPerSecond / 1000000.0);
    printf("EMU: %6.2f ms VIDEO: %6.2f ms AUDIO: %6.2f ms\n", avgEmu, avgVideo, avgAudio);
    printf("UPLOAD: %5.2f ms  PRESENT: %5.2f ms  TOTAL: %6.2f ms\n",
           avgUpload, avgPresent, avgTotal);

    const u32* h = processor->GetOpcodeHistogram();
    const u32* cb = processor->GetOpcodeCBHistogram();
    const u32* ed = processor->GetOpcodeEDHistogram();
    u32 best[6] = {0,0,0,0,0,0};
    u8 bestOp[6] = {0,0,0,0,0,0};
    for (int op = 0; op < 256; ++op)
    {
        u32 v = h[op];
        for (int k = 0; k < 6; ++k)
        {
            if (v > best[k])
            {
                for (int j = 5; j > k; --j) { best[j] = best[j-1]; bestOp[j] = bestOp[j-1]; }
                best[k] = v; bestOp[k] = (u8)op;
                break;
            }
        }
    }
    printf("HOT OP: %02X=%lu %02X=%lu %02X=%lu %02X=%lu %02X=%lu %02X=%lu\n",
           bestOp[0], (unsigned long)best[0], bestOp[1], (unsigned long)best[1], bestOp[2], (unsigned long)best[2],
           bestOp[3], (unsigned long)best[3], bestOp[4], (unsigned long)best[4], bestOp[5], (unsigned long)best[5]);

    u32 bestCb = 0, bestCbOp = 0, bestEd = 0, bestEdOp = 0;
    for (int op = 0; op < 256; ++op)
    {
        if (cb[op] > bestCb) { bestCb = cb[op]; bestCbOp = op; }
        if (ed[op] > bestEd) { bestEd = ed[op]; bestEdOp = op; }
    }
    printf("HOT CB: %02lX=%lu   HOT ED: %02lX=%lu\n", (unsigned long)bestCbOp, (unsigned long)bestCb, (unsigned long)bestEdOp, (unsigned long)bestEd);
    printf("\x1b[K\n");
}
#endif

static void initMenuIcon()
{
    if (!C3D_TexInit(&menuIconTex, 64, 128, GPU_RGB565))
        return;

    menuIconUploadBuffer = (u16*)linearAlloc(sizeof(u16) * 64 * 128);
    if (!menuIconUploadBuffer)
    {
        C3D_TexDelete(&menuIconTex);
        return;
    }

    // C3D textures use 8x8 tiled/swizzled blocks. Keep the source artwork
    // in a simple linear RGB565 array and swizzle it once at startup.
    static const u8 swizzle[64] = {
        0,1,2,3,8,9,10,11,
        4,5,6,7,12,13,14,15,
        16,17,18,19,24,25,26,27,
        20,21,22,23,28,29,30,31,
        32,33,34,35,40,41,42,43,
        36,37,38,39,44,45,46,47,
        48,49,50,51,56,57,58,59,
        52,53,54,55,60,61,62,63
    };

    u16* dst = menuIconUploadBuffer;
    for (int ty = 0; ty < 128; ty += 8)
    {
        for (int tx = 0; tx < 64; tx += 8)
        {
            for (int i = 0; i < 64; ++i)
            {
                const int p = swizzle[i];
                const int y = ty + (p >> 3);
                const int x = tx + (p & 7);
                *dst++ = kMenuCartridgePixels[y * 64 + x];
            }
        }
    }

    C3D_TexUpload(&menuIconTex, menuIconUploadBuffer);
    C3D_TexSetFilter(&menuIconTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexFlush(&menuIconTex);

    menuIconSubTex.width = 64;
    menuIconSubTex.height = 47;
    menuIconSubTex.left = 0.0f;
    menuIconSubTex.top = 1.0f;
    menuIconSubTex.right = 64.0f / 64.0f;
    menuIconSubTex.bottom = 1.0f - 47.0f / 128.0f;
    menuIconImage.tex = &menuIconTex;
    menuIconImage.subtex = &menuIconSubTex;
    menuIconInitialized = true;
}

static void freeMenuIcon()
{
    if (menuIconUploadBuffer)
    {
        linearFree(menuIconUploadBuffer);
        menuIconUploadBuffer = nullptr;
    }
    if (menuIconInitialized)
    {
        C3D_TexDelete(&menuIconTex);
        menuIconInitialized = false;
    }
}

static void setGameInstructions(const char* gameTitle, bool missileDefense3D)
{
    if (!gameInstructionsTextBuf)
        return;

    C2D_TextBufClear(gameInstructionsTextBuf);

    char text[768];
    if (missileDefense3D)
    {
        snprintf(text, sizeof(text),
                 "%s\n\nCONTROLS\nD-PAD  Move\nA  Fire / Button 1\nB  Button 2\nTOUCH SCREEN  Aim\nSTART  Start\nSELECT  Game selection",
                 gameTitle ? gameTitle : "GAME");
    }
    else
    {
        snprintf(text, sizeof(text),
                 "%s\n\nCONTROLS\nD-PAD  Move\nA  Button 1\nB  Button 2\nSTART  Start\nSELECT  Game selection",
                 gameTitle ? gameTitle : "GAME");
    }

    C2D_TextParse(&gameInstructionsText, gameInstructionsTextBuf, text);
    C2D_TextOptimize(&gameInstructionsText);
}

static void drawGameInstructions()
{
    if (!menuTarget || !gameInstructionsTextBuf)
        return;

    C2D_TargetClear(menuTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(menuTarget);
    C2D_DrawText(&gameInstructionsText, C2D_AlignCenter | C2D_WithColor,
                 160.0f, 20.0f, 0.0f, 0.65f, 0.65f,
                 C2D_Color32(255, 255, 255, 255));
}

// Tiny 3x5 pixel font. It is deliberately rendered as rectangles so the
// game names remain genuinely pixel/8-bit styled on the 3DS system font.
static const u8* glyph3x5(char c)
{
    static const u8 blank[5] = {0,0,0,0,0};
    static const u8 glyphs[][5] = {
        /* A */ {0x2,0x5,0x7,0x5,0x5}, /* B */ {0x6,0x5,0x6,0x5,0x6},
        /* C */ {0x3,0x4,0x4,0x4,0x3}, /* D */ {0x6,0x5,0x5,0x5,0x6},
        /* E */ {0x7,0x4,0x6,0x4,0x7}, /* F */ {0x7,0x4,0x6,0x4,0x4},
        /* G */ {0x3,0x4,0x5,0x5,0x3}, /* H */ {0x5,0x5,0x7,0x5,0x5},
        /* I */ {0x7,0x2,0x2,0x2,0x7}, /* J */ {0x1,0x1,0x1,0x5,0x2},
        /* K */ {0x5,0x5,0x6,0x5,0x5}, /* L */ {0x4,0x4,0x4,0x4,0x7},
        /* M */ {0x5,0x7,0x7,0x5,0x5}, /* N */ {0x5,0x7,0x7,0x7,0x5},
        /* O */ {0x2,0x5,0x5,0x5,0x2}, /* P */ {0x6,0x5,0x6,0x4,0x4},
        /* Q */ {0x2,0x5,0x5,0x3,0x1}, /* R */ {0x6,0x5,0x6,0x5,0x5},
        /* S */ {0x3,0x4,0x2,0x1,0x6}, /* T */ {0x7,0x2,0x2,0x2,0x2},
        /* U */ {0x5,0x5,0x5,0x5,0x2}, /* V */ {0x5,0x5,0x5,0x5,0x2},
        /* W */ {0x5,0x5,0x7,0x7,0x5}, /* X */ {0x5,0x5,0x2,0x5,0x5},
        /* Y */ {0x5,0x5,0x2,0x2,0x2}, /* Z */ {0x7,0x1,0x2,0x4,0x7},
        /* 0 */ {0x2,0x5,0x5,0x5,0x2}, /* 1 */ {0x2,0x6,0x2,0x2,0x7},
        /* 2 */ {0x6,0x1,0x2,0x4,0x7}, /* 3 */ {0x6,0x1,0x2,0x1,0x6},
        /* - */ {0x0,0x0,0x7,0x0,0x0}, /* . */ {0x0,0x0,0x0,0x0,0x2}
    };
    if (c >= 'A' && c <= 'Z') return glyphs[c - 'A'];
    if (c >= '0' && c <= '3') return glyphs[26 + (c - '0')];
    if (c == '-') return glyphs[30];
    if (c == '.') return glyphs[31];
    return blank;
}

static std::string menuTitle(const char* title)
{
    std::string s = title ? title : "";
    for (char& c : s)
        c = (char)std::toupper((unsigned char)c);

    constexpr size_t maxChars = 13;
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

static void drawPixelTitle(const char* title, int x, int y, int width)
{
    const std::string text = menuTitle(title);
    constexpr int pixel = 1;
    constexpr int charW = 3;
    constexpr int charStep = 4;
    const int totalW = (int)text.size() * charStep - 1;
    int startX = x + (width - totalW) / 2;
    // Red label occupies approximately y=2..12 in the 64x47 scaled cartridge.
    const int startY = y + 4;

    for (size_t n = 0; n < text.size(); ++n)
    {
        const u8* g = glyph3x5(text[n]);
        const int gx = startX + (int)n * charStep;
        for (int gy = 0; gy < 5; ++gy)
        {
            for (int col = 0; col < 3; ++col)
            {
                if (g[gy] & (1 << (2 - col)))
                    C2D_DrawRectSolid((float)(gx + col * pixel), (float)(startY + gy * pixel),
                                      0.0f, (float)pixel, (float)pixel,
                                      C2D_Color32(255, 255, 255, 255));
            }
        }
    }
}

static bool chooseRom(std::string& selected)
{
    const std::vector<std::string> roms = findShortcutRoms();
    int selectedIndex = 0;

    // C2D screen targets use the normal 320x240 logical coordinate system on
    // the bottom display. The physical framebuffer is rotated internally by
    // the 3DS graphics pipeline; touch coordinates are also 320x240.
    // Cartridge dimensions are derived from the supplied icon: 1182x871.
    // Scaled to 64x47 on the native 320x240 bottom screen.
    static constexpr int tileW = 64;
    static constexpr int tileH = 47;
    static constexpr int gapX = 12;
    static constexpr int gapY = 12;
    static constexpr int left = 14;
    static constexpr int top = 67;

    auto tileRect = [](int index, int& x, int& y)
    {
        const int col = index % 4;
        const int row = index / 4;
        x = left + col * (tileW + gapX);
        y = top + row * (tileH + gapY);
    };

    auto drawMenu = [&]()
    {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(menuTarget, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(menuTarget);

        for (int i = 0; i < 8; ++i)
        {
            int x, y;
            tileRect(i, x, y);

            if (menuIconInitialized)
                C2D_DrawImageAt(menuIconImage, (float)x, (float)y, 0.0f, nullptr, 1.0f, 1.0f);

            // Darken unavailable games while keeping the cartridge silhouette.
            if (roms[i].empty())
                C2D_DrawRectSolid((float)x, (float)y, 0.1f, (float)tileW, (float)tileH,
                                  C2D_Color32(0, 0, 0, 150));

            if (selectedIndex == i)
            {
                C2D_DrawRectSolid((float)x, (float)y, 0.2f, (float)tileW, 1.0f,
                                  C2D_Color32(255, 255, 255, 255));
                C2D_DrawRectSolid((float)x, (float)(y + tileH - 1), 0.2f, (float)tileW, 1.0f,
                                  C2D_Color32(255, 255, 255, 255));
                C2D_DrawRectSolid((float)x, (float)y, 0.2f, 1.0f, (float)tileH,
                                  C2D_Color32(255, 255, 255, 255));
                C2D_DrawRectSolid((float)(x + tileW - 1), (float)y, 0.2f, 1.0f, (float)tileH,
                                  C2D_Color32(255, 255, 255, 255));
            }

            // The title is intentionally drawn over the red label of the
            // cartridge rather than stored in eight separate image files.
            drawPixelTitle(kSegaScopeGames[i].title, x, y, tileW);
        }

        C2D_Flush();
        C3D_FrameEnd(0);
    };

    drawMenu();

    while (aptMainLoop())
    {
        hidScanInput();
        const u32 down = hidKeysDown();

        if (down & KEY_LEFT)
            selectedIndex = (selectedIndex % 4 + 3) % 4 + (selectedIndex / 4) * 4;
        else if (down & KEY_RIGHT)
            selectedIndex = (selectedIndex % 4 + 1) % 4 + (selectedIndex / 4) * 4;
        else if (down & KEY_UP)
            selectedIndex = (selectedIndex + 4) % 8;
        else if (down & KEY_DOWN)
            selectedIndex = (selectedIndex + 4) % 8;

        bool activate = (down & KEY_A) != 0;

        if (down & KEY_TOUCH)
        {
            touchPosition touch;
            hidTouchRead(&touch);
            for (int i = 0; i < 8; ++i)
            {
                int x, y;
                tileRect(i, x, y);
                if (touch.px >= x && touch.px < x + tileW &&
                    touch.py >= y && touch.py < y + tileH)
                {
                    selectedIndex = i;
                    activate = true;
                    break;
                }
            }
        }

        if (activate && !roms[selectedIndex].empty())
        {
            selected = roms[selectedIndex];
            return true;
        }

        if (down & KEY_B)
            break;

        drawMenu();
        gspWaitForVBlank();
    }

    return false;
}

static void initAudio()
{
    if (ndspInit())
        return;
    audioInitialized = true;

    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, GS_AUDIO_SAMPLE_RATE);

    float mix[12] = {};
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    audioFifo = (s16*)linearAlloc(sizeof(s16) * AUDIO_FIFO_SAMPLES);
    if (!audioFifo)
    {
        printf("Failed to allocate audio FIFO.\n");
        ndspExit();
        audioInitialized = false;
        return;
    }
    memset(audioFifo, 0, sizeof(s16) * AUDIO_FIFO_SAMPLES);

    for (int i = 0; i < AUDIO_BUFFERS; ++i)
    {
        audioBuffers[i] = (s16*)linearAlloc(sizeof(s16) * AUDIO_SAMPLES_MAX);
        if (!audioBuffers[i])
        {
            printf("Failed to allocate audio buffer %d.\n", i);
            for (int j = 0; j < i; ++j)
            {
                linearFree(audioBuffers[j]);
                audioBuffers[j] = nullptr;
            }
            linearFree(audioFifo);
            audioFifo = nullptr;
            ndspExit();
            audioInitialized = false;
            return;
        }
        memset(audioBuffers[i], 0, sizeof(s16) * AUDIO_SAMPLES_MAX);
        memset(&audioWave[i], 0, sizeof(audioWave[i]));
        audioWave[i].data_pcm16 = audioBuffers[i];
        audioWave[i].nsamples = AUDIO_SAMPLES_MAX / 2;
        DSP_FlushDataCache(audioBuffers[i], sizeof(s16) * AUDIO_SAMPLES_MAX);
    }
}

static bool audioWaveFree(const ndspWaveBuf& wave)
{
    return wave.status == NDSP_WBUF_FREE || wave.status == NDSP_WBUF_DONE;
}

static void pumpAudio()
{
    if (!audioInitialized || !audioFifo || audioFifoCount <= 0)
        return;

    // Feed NDSP in fixed-size chunks rather than one chunk per emulated frame.
    // A 60 Hz SMS frame produces ~1470 interleaved samples at 44.1 kHz, which
    // does not divide evenly into an NDSP buffer.  Keeping a FIFO here prevents
    // those fractional frame boundaries from becoming audible gaps.
    for (int n = 0; n < AUDIO_BUFFERS && audioFifoCount >= AUDIO_SAMPLES_MAX; ++n)
    {
        ndspWaveBuf& wave = audioWave[audioIndex];
        if (!audioWaveFree(wave))
            break;

        int chunk = std::min(audioFifoCount, AUDIO_SAMPLES_MAX);
        chunk &= ~1; // stereo PCM16: always submit complete L/R pairs
        if (chunk <= 0)
            break;

        int first = std::min(chunk, AUDIO_FIFO_SAMPLES - audioFifoHead);
        memcpy(audioBuffers[audioIndex], audioFifo + audioFifoHead,
               sizeof(s16) * first);
        if (first < chunk)
        {
            memcpy(audioBuffers[audioIndex] + first, audioFifo,
                   sizeof(s16) * (chunk - first));
        }
        audioFifoHead = (audioFifoHead + chunk) % AUDIO_FIFO_SAMPLES;
        audioFifoCount -= chunk;

        wave.nsamples = chunk / 2;
        DSP_FlushDataCache(audioBuffers[audioIndex], sizeof(s16) * chunk);
        ndspChnWaveBufAdd(0, &wave);
        audioIndex = (audioIndex + 1) % AUDIO_BUFFERS;
    }
}

static void pushAudio(const s16* samples, int count)
{
    if (!audioInitialized || !samples || count <= 0)
        return;

    count = std::min(count, AUDIO_SAMPLES_MAX);
    count &= ~1;
    if (count <= 0)
        return;

    // Normally the FIFO has tens of milliseconds of headroom.  If the DSP
    // really falls behind for an extended period, wait only until enough room
    // exists to preserve the audio stream; never overwrite/drop old samples.
    while (AUDIO_FIFO_SAMPLES - audioFifoCount < count)
    {
        pumpAudio();
        if (AUDIO_FIFO_SAMPLES - audioFifoCount >= count)
            break;
        ++audioBufferWaits;
        svcSleepThread(1000000LL);
    }

    int first = std::min(count, AUDIO_FIFO_SAMPLES - audioFifoTail);
    memcpy(audioFifo + audioFifoTail, samples, sizeof(s16) * first);
    if (first < count)
    {
        memcpy(audioFifo, samples + first, sizeof(s16) * (count - first));
    }
    audioFifoTail = (audioFifoTail + count) % AUDIO_FIFO_SAMPLES;
    audioFifoCount += count;

    pumpAudio();
}

static u64 getFramePeriodTicks(GS_Region region)
{
    const u64 frameCycles = static_cast<u64>(GS_CYCLES_PER_LINE) *
        static_cast<u64>(region == Region_PAL ? GS_LINES_PER_FRAME_PAL : GS_LINES_PER_FRAME_NTSC);
    const u64 masterClock = static_cast<u64>(
        region == Region_PAL ? GS_MASTER_CLOCK_PAL : GS_MASTER_CLOCK_NTSC);

    // Pace against the same master-clock/frame geometry used by the core,
    // but in the 3DS high-resolution system-tick domain.  This avoids the
    // old integer-millisecond 17 ms NTSC period, which ran the producer
    // slower than the 44.1 kHz NDSP consumer.
    return (static_cast<u64>(SYSCLOCK_ARM11) * frameCycles + masterClock / 2) / masterClock;
}

static void paceFrame(u64& nextFrameTick, GS_Region region)
{
    const u64 framePeriodTicks = getFramePeriodTicks(region);
    const u64 now = svcGetSystemTick();

    if (nextFrameTick == 0 || now > nextFrameTick + framePeriodTicks)
    {
        nextFrameTick = now + framePeriodTicks;
        return;
    }

    while (true)
    {
        const u64 current = svcGetSystemTick();
        if (current >= nextFrameTick)
            break;

        const u64 remainingTicks = nextFrameTick - current;
        const u64 sleepNs = (remainingTicks * 1000000000ULL) / SYSCLOCK_ARM11;
        if (sleepNs > 1000000ULL)
            svcSleepThread(static_cast<s64>(sleepNs - 500000ULL));
        else
            break;
    }

    while (svcGetSystemTick() < nextFrameTick) { }
    nextFrameTick += framePeriodTicks;
}

static void updateTexture(C3D_Tex& tex)
{
    // Directly gather the SMS framebuffer into the PICA200 8x8 tiled order.
    // This replaces the old full-frame copy + per-tile temporary + second copy.
    static const u8 swizzle[64] = {
         0,  1,  8,  9,  2,  3, 10, 11,
        16, 17, 24, 25, 18, 19, 26, 27,
         4,  5, 12, 13,  6,  7, 14, 15,
        20, 21, 28, 29, 22, 23, 30, 31,
        32, 33, 40, 41, 34, 35, 42, 43,
        48, 49, 56, 57, 50, 51, 58, 59,
        36, 37, 44, 45, 38, 39, 46, 47,
        52, 53, 60, 61, 54, 55, 62, 63
    };

    const u16* src = reinterpret_cast<const u16*>(frameBuffer);
    u16* dst = uploadBuffer;

    // Only the 256x192 SMS framebuffer is rebuilt each frame. The lower 64
    // texture rows are initialized once when uploadBuffer is allocated.
    for (int ty = 0; ty < FB_H; ty += 8)
    {
        for (int tx = 0; tx < TEX_W; tx += 8)
        {
            for (int i = 0; i < 64; ++i)
            {
                const int p = swizzle[i];
                const int y = ty + (p >> 3);
                const int x = tx + (p & 7);
                dst[i] = src[y * FB_W + x];
            }
            dst += 64;
        }
    }

    C3D_TexUpload(&tex, uploadBuffer);
    C3D_TexFlush(&tex);
}

struct TouchPhaserState
{
    bool valid;
    int x;
    int y;
};

static int g_touchPhaserX = 128;
static int g_touchPhaserY = 96;

static void drawPhaserCrosshair(float cx, float cy, float depth)
{
    const u32 color = C2D_Color32(255, 255, 255, 255);
    C2D_DrawRectSolid(cx - 8.0f, cy - 1.0f, depth, 16.0f, 2.0f, color);
    C2D_DrawRectSolid(cx - 1.0f, cy - 8.0f, depth, 2.0f, 16.0f, color);
}

// Put the virtual crosshair slightly in front of the game plane. On the
// 3DS this is achieved by giving the crosshair crossed stereo disparity: the
// left-eye image moves right and the right-eye image moves left.
static constexpr float kCrosshairStereoParallax = 8.0f;

static bool renderFrame(C3D_RenderTarget* topLeft, C3D_RenderTarget* topRight,
                         bool missileDefense3D, const TouchPhaserState& touch)
{
    // Non-blocking presentation keeps the emulator CPU from waiting on the
    // display.  Each eye has an independent triple-buffer cursor because the
    // SegaScope alternates eyes on successive emulator frames.
    if (!C3D_FrameBegin(C3D_FRAME_NONBLOCK))
        return false;

    const int leftSlot = videoTextureSlot[0];
    const int rightSlot = videoTextureSlot[1];

    C2D_TargetClear(topLeft, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(topLeft);
    C2D_DrawImageAt(gameImage[leftSlot * 2], 72.0f, 24.0f, 0.5f, nullptr, 1.0f, 1.0f);
    if (missileDefense3D && touch.valid)
        drawPhaserCrosshair(72.0f + static_cast<float>(touch.x) +
                                (stereoActive ? kCrosshairStereoParallax * 0.5f : 0.0f),
                            24.0f + static_cast<float>(touch.y), 0.6f);

    if (stereoActive)
    {
        C2D_TargetClear(topRight, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(topRight);
        C2D_DrawImageAt(gameImage[rightSlot * 2 + 1], 72.0f, 24.0f, 0.5f, nullptr, 1.0f, 1.0f);
        if (missileDefense3D && touch.valid)
            drawPhaserCrosshair(72.0f + static_cast<float>(touch.x) -
                                    kCrosshairStereoParallax * 0.5f,
                                24.0f + static_cast<float>(touch.y), 0.6f);
    }

    // The bottom screen belongs to the launcher while a ROM is not running.
    // Once a game starts, replace the launcher tiles with the controls for
    // the active game so the player always has a useful second-screen UI.
    drawGameInstructions();
    C2D_Flush();

    C3D_FrameEnd(0);
    return true;
}

static int glassesEyeFromRegistry(GearsystemCore& core)
{
    // Gearsystem's SegaScope emulation uses bit 0 of the glasses registry:
    // 1 = left eye, 0 = right eye.  This is the same convention used by
    // GearsystemCore::RenderFrameBuffer().
    return (core.GetInput()->GetGlassesRegistry() & 0x01) ? 0 : 1;
}

static void updateStereoDetection(int eye)
{
    if (previousGlassesEye != -1 && previousGlassesEye != eye)
    {
        stereoActive = true;
        if (!display3DEnabled)
        {
            gfxSet3D(true);
            display3DEnabled = true;
        }
    }

    previousGlassesEye = eye;
}

static void updateEyeTexture(int eye)
{
    const int slot = videoTextureSlot[eye];
    updateTexture(gameTex[slot * 2 + eye]);
}

static bool loadBundledRom(std::string& path)
{
    FILE* f = fopen("romfs:/filename.txt", "rb");
    if (!f) return false;

    char name[512] = {};
    if (!fgets(name, sizeof(name), f))
    {
        fclose(f);
        return false;
    }
    fclose(f);

    size_t len = strlen(name);
    while (len && (name[len - 1] == '\r' || name[len - 1] == '\n' || name[len - 1] == ' '))
        name[--len] = '\0';

    if (!len) return false;

    path = "romfs:/";
    path += name;

    // filename.txt is optional. Do not treat a missing bundled ROM as a
    // successfully selected ROM; otherwise the loader tries to open the
    // placeholder path and never falls back to the SD card browser.
    if (!isSmsFile(name))
        return false;

    FILE* rom = fopen(path.c_str(), "rb");
    if (!rom)
        return false;
    fclose(rom);
    return true;
}

static TouchPhaserState bindInputs(GearsystemCore& core, bool missileDefense3D)
{
    hidScanInput();
    const u32 held = hidKeysHeld();
    TouchPhaserState state = { false, 128, 96 };

    auto setKey = [&](u32 mask, GS_Keys key)
    {
        if (held & mask) core.KeyPressed(Joypad_1, key);
        else core.KeyReleased(Joypad_1, key);
    };

    setKey(KEY_UP, Key_Up);
    setKey(KEY_DOWN, Key_Down);
    setKey(KEY_LEFT, Key_Left);
    setKey(KEY_RIGHT, Key_Right);

    // Missile Defense 3-D only: touchscreen selects the Light Phaser aim
    // point; A remains the trigger. All other games retain the original
    // input path unchanged.
    if (missileDefense3D)
    {
        if (held & KEY_TOUCH)
        {
            touchPosition touch;
            hidTouchRead(&touch);
            int x = static_cast<int>(touch.px) - 32;
            int y = static_cast<int>(touch.py) - 24;
            x = CLAMP(x, 0, FB_W - 1);
            y = CLAMP(y, 0, FB_H - 1);
            state = { true, x, y };
            core.SetPhaser(x, y);
        }
        // Keep the last aimed position after lifting the finger.
        if (state.valid)
        {
            g_touchPhaserX = state.x;
            g_touchPhaserY = state.y;
        }
        core.SetPhaser(g_touchPhaserX, g_touchPhaserY);
        setKey(KEY_A, Key_1);
    }
    else
    {
        setKey(KEY_A, Key_1);
    }
    setKey(KEY_B, Key_2);
    setKey(KEY_START, Key_Start);
    return state;
}


static bool isMissileDefense3D(GearsystemCore& core)
{
    Cartridge* cartridge = core.GetCartridge();
    if (!cartridge || !cartridge->IsValidROM())
        return false;

    const u8* rom = cartridge->GetROM();
    const int romSize = cartridge->GetROMSize();
    const int headers[] = { 0x7FF0, 0x1FF0, 0x3FF0 };
    for (int header : headers)
    {
        if (header + 0x0C > romSize) continue;
        if (memcmp(rom + header, "TMR SEGA", 8) != 0) continue;
        const u16 productCode = (static_cast<u16>(rom[header + 0x0A]) << 8) | rom[header + 0x0B];
        if (productCode == 0x154A) return true;
    }
    return false;
}

static bool isLineOfFire(GearsystemCore& core)
{
    Cartridge* cartridge = core.GetCartridge();
    if (!cartridge || !cartridge->IsValidROM())
        return false;

    const u8* rom = cartridge->GetROM();
    const int romSize = cartridge->GetROMSize();

    // SMS header locations supported by Gearsystem.
    const int headerLocations[] = { 0x7FF0, 0x1FF0, 0x3FF0 };

    for (int header : headerLocations)
    {
        if (header + 0x10 <= romSize &&
            rom[header]     == 'T' &&
            rom[header + 1] == 'M' &&
            rom[header + 2] == 'R' &&
            rom[header + 3] == ' ' &&
            rom[header + 4] == 'S' &&
            rom[header + 5] == 'E' &&
            rom[header + 6] == 'G' &&
            rom[header + 7] == 'A')
        {
            // Product code is stored in the two bytes immediately after
            // the "TMR SEGA" signature and two reserved bytes.
            const u16 productCode =
                (static_cast<u16>(rom[header + 0x0A]) << 8) |
                static_cast<u16>(rom[header + 0x0B]);

            // Line of Fire: product code DB85.
            return productCode == 0xDB85;
        }
    }

    return false;
}

static void enableLineOfFire3D(GearsystemCore& core)
{
    Cartridge* cartridge = core.GetCartridge();
    if (!cartridge)
        return;

    u8* rom = cartridge->GetROM();
    const int romSize = cartridge->GetROMSize();

    // Line of Fire's 3-D boot check is at ROM offset 0x03BC.
    // Replacing JP Z (0xCA) with JP NZ (0xC2) bypasses the
    // requirement to hold buttons 1+2 during power-on.
    if (romSize > 0x03BC && rom[0x03BC] == 0xCA)
    {
        rom[0x03BC] = 0xC2;
        core.ResetROM();
        printf("Line of Fire detected (Product Code DB85): 3-D enabled automatically.\n");
    }
}

int main(int argc, char* argv[])
{
    // These variables must be initialized before any possible goto cleanup;
    // otherwise C++ rejects the jump as crossing their initialization.
    bool firstRomSelection = true;
    std::string romPath;
    C3D_RenderTarget* topLeft = nullptr;
    C3D_RenderTarget* topRight = nullptr;

    gfxInitDefault();
    APT_SetAppCpuTimeLimit(89);
    osSetSpeedupEnable(true);
    romfsInit();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    gameInstructionsTextBuf = C2D_TextBufNew(1024);
    if (!gameInstructionsTextBuf)
    {
        printf("Failed to allocate game instruction text buffer.\n");
        goto cleanup;
    }

    gfxSet3D(false);
    topLeft = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    topRight = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    // Use Citro2D's native screen target for the bottom display. This keeps
    // the logical 320x240 coordinate system aligned with hidTouchRead().
    // The previous custom target exposed the physical 240x320 framebuffer
    // dimensions directly, which caused the menu to repeat/crop on hardware.
    menuTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!menuTarget)
    {
        printf("Failed to create bottom-screen target.\n");
        goto cleanup;
    }

    for (int i = 0; i < VIDEO_TEXTURE_SLOTS * 2; ++i)
    {
        if (!C3D_TexInit(&gameTex[i], TEX_W, TEX_H, GPU_RGB565))
        {
            printf("Failed to allocate video textures.\n");
            goto cleanup;
        }
        C3D_TexSetFilter(&gameTex[i], GPU_NEAREST, GPU_NEAREST);
    }

    textureInitialized = true;
    uploadBuffer = (u16*)linearAlloc(sizeof(u16) * TEX_W * TEX_H);
    if (uploadBuffer)
    {
        // The SMS framebuffer occupies only the first 192 rows. Keep the
        // unused 64 rows stable instead of clearing them on every frame.
        memset(uploadBuffer + (FB_H * TEX_W), 0,
               sizeof(u16) * TEX_W * (TEX_H - FB_H));
    }
    frameBuffer = (u8*)linearAlloc(sizeof(u16) * GS_RESOLUTION_MAX_WIDTH_WITH_OVERSCAN * GS_RESOLUTION_MAX_HEIGHT_WITH_OVERSCAN);

    for (int slot = 0; slot < VIDEO_TEXTURE_SLOTS; ++slot)
    {
        for (int eye = 0; eye < 2; ++eye)
        {
            const int index = slot * 2 + eye;
            gameSubTex[index].width = FB_W;
            gameSubTex[index].height = FB_H;
            gameSubTex[index].left = 0.0f;
            gameSubTex[index].top = 1.0f;
            gameSubTex[index].right = 1.0f;
            gameSubTex[index].bottom = 1.0f - (float)FB_H / (float)TEX_H;
            gameImage[index].tex = &gameTex[index];
            gameImage[index].subtex = &gameSubTex[index];
        }
    }

    initMenuIcon();

    // Audio is shared by all ROM sessions. Initialize it once so returning
    // to the browser with SELECT does not tear down/recreate NDSP.
    initAudio();

    while (aptMainLoop())
    {
        bool haveRom = false;

        if (firstRomSelection && argc > 1 && argv[1] && isSmsFile(argv[1]))
        {
            romPath = argv[1];
            haveRom = true;
        }
        else if (firstRomSelection && loadBundledRom(romPath))
        {
            haveRom = true;
        }
        else
        {
            // After SELECT, always return to the SD card browser so another
            // ROM can be chosen instead of exiting the application.
            haveRom = chooseRom(romPath);
        }
        firstRomSelection = false;

        if (!haveRom)
            break;

        printf("\x1b[2J\x1b[HLoading:\n%s\n", romPath.c_str());

        {
            GearsystemCore core;
            core.Init(GS_PIXEL_RGB565);

            if (!core.LoadROM(romPath.c_str()))
            {
                printf("\nFailed to load SMS ROM.\n");
                printf("Press START to return to the ROM browser.\n");
                while (aptMainLoop())
                {
                    hidScanInput();
                    if (hidKeysDown() & KEY_START) break;
                    gspWaitForVBlank();
                }
                continue;
            }

            if (isLineOfFire(core))
                enableLineOfFire3D(core);

            const bool missileDefense3D = isMissileDefense3D(core);
            if (missileDefense3D)
            {
                core.EnablePhaser(true);
                g_touchPhaserX = 128;
                g_touchPhaserY = 96;
                core.SetPhaser(g_touchPhaserX, g_touchPhaserY);
                printf("Missile Defense 3-D detected (Product Code 8001).\n");
                printf("Touch lower screen to aim; press A to fire.\n");
            }

            setGameInstructions(shortcutTitleForRom(romPath), missileDefense3D);

            core.LoadRam();

            GS_RuntimeInfo info = {};
            core.GetRuntimeInfo(info);
            if (info.screen_width != FB_W || info.screen_height != FB_H)
            {
                printf("\nUnsupported SMS video mode: %dx%d\n", info.screen_width, info.screen_height);
                printf("This build expects the standard 256x192 mode.\n");
                printf("Press START to return to the ROM browser.\n");
                while (aptMainLoop())
                {
                    hidScanInput();
                    if (hidKeysDown() & KEY_START) break;
                    gspWaitForVBlank();
                }
                continue;
            }

            GS_RuntimeInfo runtimeInfo = {};
            core.GetRuntimeInfo(runtimeInfo);

            s16 samples[AUDIO_SAMPLES_MAX] = {};

            // Reset per-ROM stereo detection when a new cartridge starts.
            stereoActive = false;
            previousGlassesEye = -1;
            videoTextureSlot[0] = 0;
            videoTextureSlot[1] = 0;
            audioFifoHead = 0;
            audioFifoTail = 0;
            audioFifoCount = 0;
            display3DEnabled = false;
            gfxSet3D(false);

            consoleClear();
            printf("Red Viper SMS\n\n");
            printf("A/B   = SMS buttons 1/2\n");
            printf("START = Start\n");
            printf("D-Pad = Direction\n");
            printf("SELECT = ROM browser\n\n");
            printf("%s\n", core.GetCartridge()->GetFileName());

            bool returnToBrowser = false;
            u64 nextFrameTick = 0;
            TouchPhaserState touchPhaser = { false, 128, 96 };
            while (aptMainLoop())
            {
                hidScanInput();
                if (hidKeysDown() & KEY_SELECT)
                {
                    returnToBrowser = true;
                    break;
                }

                touchPhaser = bindInputs(core, missileDefense3D);

                int sampleCount = 0;
                core.RunToVBlank(nullptr, samples, &sampleCount, nullptr);
                core.GetVideo()->Render16bit(
                    core.GetVideo()->GetFrameBuffer(),
                    frameBuffer,
                    GS_PIXEL_RGB565,
                    FB_W * FB_H,
                    false);
                pushAudio(samples, sampleCount);

                const int eye = glassesEyeFromRegistry(core);
                updateStereoDetection(eye);
                if (!stereoActive)
                {
                    updateEyeTexture(0);
                }
                else
                {
                    updateEyeTexture(eye);
                }

                if (renderFrame(topLeft, topRight, missileDefense3D, touchPhaser))
                    videoTextureSlot[eye] = (videoTextureSlot[eye] + 1) % VIDEO_TEXTURE_SLOTS;

                // Pace from the emulated master clock using the 3DS
                // high-resolution system tick.  NTSC is not 17 ms/frame;
                // using integer milliseconds makes the audio producer run
                // too slowly and eventually starves NDSP.
                paceFrame(nextFrameTick, runtimeInfo.region);
            }

            // Persist the current cartridge before returning to the browser.
            core.SaveRam();

            if (!returnToBrowser)
                break;
        }
    }

cleanup:
    freeMenuIcon();
    if (uploadBuffer) linearFree(uploadBuffer);
    if (frameBuffer) linearFree(frameBuffer);

    for (int i = 0; i < AUDIO_BUFFERS; ++i)
    {
        if (audioBuffers[i])
            linearFree(audioBuffers[i]);
    }
    if (audioFifo)
        linearFree(audioFifo);
    if (audioInitialized) ndspExit();

    if (textureInitialized)
    {
        for (int i = 0; i < VIDEO_TEXTURE_SLOTS * 2; ++i)
            C3D_TexDelete(&gameTex[i]);
    }
    if (menuTarget) C3D_RenderTargetDelete(menuTarget);
    menuTarget = nullptr;
    if (gameInstructionsTextBuf)
    {
        C2D_TextBufDelete(gameInstructionsTextBuf);
        gameInstructionsTextBuf = nullptr;
    }
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}
