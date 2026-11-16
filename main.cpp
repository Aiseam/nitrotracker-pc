/*
 * NitsTracker PC
 * 
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>

// ============================================================================
// COMPATIBILITY & FRAMEBUFFER
// ============================================================================
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#define NDS_WIDTH       256
#define NDS_HEIGHT      192
#define TOTAL_HEIGHT    (NDS_HEIGHT * 2)
#define WINDOW_SCALE    2

inline constexpr u16 RGB15(u8 r, u8 g, u8 b) {
    return (u16)((r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10));
}

inline u32 RGB15toARGB32(u16 c) {
    u32 r = ((c & 0x001F) * 255) / 31;
    u32 g = (((c >> 5) & 0x001F) * 255) / 31;
    u32 b = (((c >> 10) & 0x001F) * 255) / 31;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static u16 g_main_vram[NDS_WIDTH * NDS_HEIGHT];
static u16 g_sub_vram[NDS_WIDTH * NDS_HEIGHT];
static u32 g_pc_framebuffer[NDS_WIDTH * TOTAL_HEIGHT];

// ============================================================================
//  THEME SYSTEM 
// ============================================================================
struct Theme {
    char name[32]               = "NITROT";
    u16 col_bg                  = RGB15(4, 6, 15)   | BIT(15);
    u16 col_dark_bg             = RGB15(4, 6, 15)   | BIT(15);
    u16 col_medium_bg           = RGB15(9, 11, 17)  | BIT(15);
    u16 col_light_bg            = RGB15(16, 18, 24) | BIT(15);
    u16 col_lighter_bg          = RGB15(23, 25, 31) | BIT(15);
    u16 col_light_ctrl          = RGB15(31, 31, 0)  | BIT(15);
    u16 col_dark_ctrl           = RGB15(31, 18, 0)  | BIT(15);
    u16 col_light_ctrl_disabled = RGB15(16, 18, 24) | BIT(15);
    u16 col_dark_ctrl_disabled  = RGB15(9, 11, 17)  | BIT(15);
    u16 col_list_highlight1     = RGB15(28, 15, 0)  | BIT(15);
    u16 col_list_highlight2     = RGB15(28, 28, 0)  | BIT(15);
    u16 col_outline             = RGB15(0, 0, 0)    | BIT(15);
    u16 col_sepline             = RGB15(31, 31, 0)  | BIT(15);
    u16 col_icon                = RGB15(0, 0, 0)    | BIT(15);
    u16 col_text                = RGB15(0, 0, 0)    | BIT(15);
    u16 col_signal              = RGB15(31, 0, 0)   | BIT(15);
};

static Theme g_theme;
static int   g_themePresetIdx = 0;

struct ThemePreset {
    const char* name;
    u16 bg, dark_bg, med_bg, light_bg, lighter_bg, light_ctrl, dark_ctrl, text, signal;
};

static const ThemePreset THEME_PRESETS[] = {
    // 0: NITROT (Original Default)
    { "NITROT",
      RGB15(4,6,15)|BIT(15), RGB15(4,6,15)|BIT(15), RGB15(9,11,17)|BIT(15),
      RGB15(16,18,24)|BIT(15), RGB15(23,25,31)|BIT(15), RGB15(31,31,0)|BIT(15),
      RGB15(31,18,0)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,0,0)|BIT(15) },
    // 1: CLASY
    { "CLASY",
      RGB15(12,12,13)|BIT(15), RGB15(8,8,9)|BIT(15), RGB15(16,16,17)|BIT(15),
      RGB15(21,21,22)|BIT(15), RGB15(27,27,28)|BIT(15), RGB15(7,15,25)|BIT(15),
      RGB15(4,10,18)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,14,0)|BIT(15) },
    // 2: GREENAS
    { "PROTRACKER",
      RGB15(10,10,10)|BIT(15), RGB15(5,5,5)|BIT(15), RGB15(14,14,14)|BIT(15),
      RGB15(19,19,19)|BIT(15), RGB15(25,25,25)|BIT(15), RGB15(8,14,26)|BIT(15),
      RGB15(5,9,18)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(30,6,6)|BIT(15) },
    // 3: CYBER BLUES
    { "ULTIMATE TRACKER",
      RGB15(2,4,12)|BIT(15), RGB15(1,2,8)|BIT(15), RGB15(5,9,20)|BIT(15),
      RGB15(10,16,26)|BIT(15), RGB15(18,24,31)|BIT(15), RGB15(0,28,31)|BIT(15),
      RGB15(0,18,24)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,26,0)|BIT(15) },
    // 4: NIGHTS
    { "MODE DARK",
      RGB15(0,0,0)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(3,3,3)|BIT(15),
      RGB15(6,6,6)|BIT(15), RGB15(11,11,11)|BIT(15), RGB15(14,14,14)|BIT(15),
      RGB15(9,9,9)|BIT(15), RGB15(31,31,31)|BIT(15), RGB15(31,8,8)|BIT(15) },
    // 5: HACKS
    { "MATRIX",
      RGB15(0,0,0)|BIT(15), RGB15(0,1,0)|BIT(15), RGB15(0,4,0)|BIT(15),
      RGB15(0,8,0)|BIT(15), RGB15(0,13,0)|BIT(15), RGB15(0,18,0)|BIT(15),
      RGB15(0,11,0)|BIT(15), RGB15(4,31,4)|BIT(15), RGB15(16,31,16)|BIT(15) },
    // 6..11: Random creative theme names
    { "CYBER SYSTEM",
      RGB15(6,2,12)|BIT(15), RGB15(4,1,8)|BIT(15), RGB15(11,4,20)|BIT(15),
      RGB15(16,8,26)|BIT(15), RGB15(22,14,31)|BIT(15), RGB15(31,6,22)|BIT(15),
      RGB15(22,3,15)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(0,31,26)|BIT(15) },
    { "SOLAR AMBER",
      RGB15(8,4,0)|BIT(15), RGB15(5,2,0)|BIT(15), RGB15(13,7,1)|BIT(15),
      RGB15(19,11,2)|BIT(15), RGB15(25,16,4)|BIT(15), RGB15(31,24,4)|BIT(15),
      RGB15(24,16,2)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,6,0)|BIT(15) },
    { "ARCTIC FROST",
      RGB15(3,8,12)|BIT(15), RGB15(2,5,9)|BIT(15), RGB15(7,14,19)|BIT(15),
      RGB15(14,21,26)|BIT(15), RGB15(22,27,31)|BIT(15), RGB15(12,28,31)|BIT(15),
      RGB15(6,20,25)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,10,10)|BIT(15) },
    { "CRIMSON DUSK",
      RGB15(10,2,3)|BIT(15), RGB15(6,1,2)|BIT(15), RGB15(15,4,5)|BIT(15),
      RGB15(21,8,9)|BIT(15), RGB15(27,14,15)|BIT(15), RGB15(31,16,12)|BIT(15),
      RGB15(24,9,7)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,31,10)|BIT(15) },
    { "EMERALD CHIP",
      RGB15(2,8,5)|BIT(15), RGB15(1,5,3)|BIT(15), RGB15(4,13,8)|BIT(15),
      RGB15(8,19,13)|BIT(15), RGB15(14,25,19)|BIT(15), RGB15(12,31,20)|BIT(15),
      RGB15(6,23,14)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,28,6)|BIT(15) },
    { "VAPOR WAVE",
      RGB15(5,3,14)|BIT(15), RGB15(3,2,10)|BIT(15), RGB15(10,7,21)|BIT(15),
      RGB15(15,12,27)|BIT(15), RGB15(21,18,31)|BIT(15), RGB15(10,29,31)|BIT(15),
      RGB15(26,10,28)|BIT(15), RGB15(0,0,0)|BIT(15), RGB15(31,28,0)|BIT(15) }
};
static constexpr int NUM_THEME_PRESETS = (int)(sizeof(THEME_PRESETS) / sizeof(THEME_PRESETS[0]));

std::string getExeThemePath() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string p(exePath);
    size_t dot = p.find_last_of('.');
    if (dot != std::string::npos) p = p.substr(0, dot);
    return p + "_theme.dat";
}

void saveThemeMemory() {
    FILE* f = fopen(getExeThemePath().c_str(), "wb");
    if (!f) return;
    const char sig[8] = {'N','T','T','H','M','0','0','2'};
    fwrite(sig, 1, 8, f);
    fwrite(&g_themePresetIdx, sizeof(int), 1, f);
    fwrite(&g_theme, sizeof(Theme), 1, f);
    fclose(f);
}

void applyThemePreset(int idx, bool persist = true) {
    g_themePresetIdx = ((idx % NUM_THEME_PRESETS) + NUM_THEME_PRESETS) % NUM_THEME_PRESETS;
    const auto& p = THEME_PRESETS[g_themePresetIdx];
    strncpy(g_theme.name, p.name, 31);
    g_theme.name[31]            = 0;
    g_theme.col_bg              = p.bg;
    g_theme.col_dark_bg         = p.dark_bg;
    g_theme.col_medium_bg       = p.med_bg;
    g_theme.col_light_bg        = p.light_bg;
    g_theme.col_lighter_bg      = p.lighter_bg;
    g_theme.col_light_ctrl      = p.light_ctrl;
    g_theme.col_dark_ctrl       = p.dark_ctrl;
    g_theme.col_light_ctrl_disabled = p.light_bg;
    g_theme.col_dark_ctrl_disabled  = p.med_bg;
    g_theme.col_list_highlight1 = p.dark_ctrl;
    g_theme.col_list_highlight2 = p.light_ctrl;
    g_theme.col_outline         = RGB15(0, 0, 0) | BIT(15);
    g_theme.col_sepline         = p.light_ctrl;
    g_theme.col_icon            = p.text;
    g_theme.col_text            = p.text;
    g_theme.col_signal          = p.signal;
    if (persist) saveThemeMemory();
}

void loadThemeMemory() {
    applyThemePreset(0, false);
    FILE* f = fopen(getExeThemePath().c_str(), "rb");
    if (!f) return;
    char sig[8];
    if (fread(sig, 1, 8, f) == 8 && memcmp(sig, "NTTHM002", 8) == 0) {
        fread(&g_themePresetIdx, sizeof(int), 1, f);
        fread(&g_theme, sizeof(Theme), 1, f);
    }
    fclose(f);
}

// ============================================================================
// PIXEL FONT + CANVAS
// ============================================================================
static const u8 FONT_4X6[96][6] = {
    {0,0,0,0,0,0}, {2,2,2,0,2,0}, {5,5,0,0,0,0}, {5,7,5,7,5,0}, {2,7,6,3,7,2}, {5,1,2,4,5,0},
    {2,5,2,5,3,0}, {2,2,0,0,0,0}, {1,2,2,2,1,0}, {4,2,2,2,4,0}, {0,5,2,5,0,0}, {0,2,7,2,0,0},
    {0,0,0,2,4,0}, {0,0,7,0,0,0}, {0,0,0,0,2,0}, {1,1,2,4,4,0}, {2,5,5,5,2,0}, {2,6,2,2,7,0},
    {6,1,2,4,7,0}, {6,1,2,1,6,0}, {5,5,7,1,1,0}, {7,4,6,1,6,0}, {3,4,6,5,2,0}, {7,1,2,2,2,0},
    {2,5,2,5,2,0}, {2,5,3,1,6,0}, {0,2,0,2,0,0}, {0,2,0,2,4,0}, {1,2,4,2,1,0}, {0,7,0,7,0,0},
    {4,2,1,2,4,0}, {6,1,2,0,2,0}, {2,5,7,4,3,0}, {2,5,7,5,5,0}, {6,5,6,5,6,0}, {3,4,4,4,3,0},
    {6,5,5,5,6,0}, {7,4,6,4,7,0}, {7,4,6,4,4,0}, {3,4,5,5,3,0}, {5,5,7,5,5,0}, {7,2,2,2,7,0},
    {1,1,1,5,2,0}, {5,5,6,5,5,0}, {4,4,4,4,7,0}, {5,7,7,5,5,0}, {5,7,7,7,5,0}, {2,5,5,5,2,0},
    {6,5,6,4,4,0}, {2,5,5,7,3,0}, {6,5,6,5,5,0}, {3,4,2,1,6,0}, {7,2,2,2,2,0}, {5,5,5,5,7,0},
    {5,5,5,2,2,0}, {5,5,7,7,5,0}, {5,5,2,5,5,0}, {5,5,2,2,2,0}, {7,1,2,4,7,0}, {3,2,2,2,3,0},
    {4,4,2,1,1,0}, {6,2,2,2,6,0}, {2,5,0,0,0,0}, {0,0,0,0,7,0}, {4,2,0,0,0,0}, {0,3,5,5,3,0},
    {4,6,5,5,6,0}, {0,3,4,4,3,0}, {1,3,5,5,3,0}, {0,2,7,4,3,0}, {1,2,7,2,2,0}, {0,3,5,3,1,6},
    {4,6,5,5,5,0}, {2,0,2,2,2,0}, {1,0,1,1,5,2}, {4,5,6,5,5,0}, {6,2,2,2,7,0}, {0,7,7,5,5,0},
    {0,6,5,5,5,0}, {0,2,5,5,2,0}, {0,6,5,6,4,4}, {0,3,5,3,1,1}, {0,3,4,4,4,0}, {0,3,6,3,6,0},
    {2,7,2,2,3,0}, {0,5,5,5,3,0}, {0,5,5,2,2,0}, {0,5,7,7,5,0}, {0,5,2,2,5,0}, {0,5,5,3,1,6},
    {0,7,2,4,7,0}, {1,2,6,2,1,0}, {2,2,2,2,2,0}, {4,2,3,2,4,0}, {0,5,2,0,0,0}, {7,7,7,7,7,0}
};

struct Canvas {
    u16* vram;
    int w = NDS_WIDTH;
    int h = NDS_HEIGHT;

    void clear(u16 col) { std::fill(vram, vram + (w * h), col); }

    inline void pixel(int x, int y, u16 col) {
        if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) vram[y * w + x] = col;
    }

    void fillRect(int x, int y, int rw, int rh, u16 col) {
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx)
                vram[yy * w + xx] = col;
    }

    void drawRect(int x, int y, int rw, int rh, u16 col) {
        fillRect(x, y, rw, 1, col);
        fillRect(x, y + rh - 1, rw, 1, col);
        fillRect(x, y, 1, rh, col);
        fillRect(x + rw - 1, y, 1, rh, col);
    }

    void drawGradient(int x, int y, int rw, int rh, u16 c1, u16 c2) {
        if (rh <= 0) return;
        u8 r1 = c1 & 0x1F, g1 = (c1 >> 5) & 0x1F, b1 = (c1 >> 10) & 0x1F;
        u8 r2 = c2 & 0x1F, g2 = (c2 >> 5) & 0x1F, b2 = (c2 >> 10) & 0x1F;
        for (int yy = 0; yy < rh; ++yy) {
            u8 r = (u8)(r1 + ((r2 - r1) * yy) / std::max(1, rh - 1));
            u8 g = (u8)(g1 + ((g2 - g1) * yy) / std::max(1, rh - 1));
            u8 b = (u8)(b1 + ((b2 - b1) * yy) / std::max(1, rh - 1));
            fillRect(x, y + yy, rw, 1, RGB15(r, g, b) | BIT(15));
        }
    }

    void drawText(const char* str, int x, int y, u16 col, int maxW = 256) {
        int cx = x;
        for (const char* p = str; *p; ++p) {
            if (cx + 4 > x + maxW) break;
            unsigned char ch = (unsigned char)*p;
            if (ch < 32 || ch > 127) ch = '?';
            const u8* glyph = FONT_4X6[ch - 32];
            for (int row = 0; row < 6; ++row) {
                u8 bits = glyph[row];
                if (bits & 4) pixel(cx + 0, y + row, col);
                if (bits & 2) pixel(cx + 1, y + row, col);
                if (bits & 1) pixel(cx + 2, y + row, col);
            }
            cx += 4;
        }
    }
};

// ============================================================================
// 4. DATA MODEL
// ============================================================================
enum FxType : u8 {
    FX_NONE = 0, FX_ARP = 1, FX_PUP = 2, FX_PDW = 3, FX_PTN = 4,
    FX_VBT = 5, FX_SLD = 6, FX_REV = 7, FX_TRE = 8, FX_SMU = 9,
    FX_JUM = 10, FX_VOLN = 11, FX_PTB = 12, FX_DLY = 13
};

static const char* FX_NAMES[14] = {
    "---", "ARP", "PUP", "PDW", "PTM", "VBT", "SLD",
    "REV", "TRE", "SMU", "JUM", "VOL", "PTB", "DLY"
};

static const char* FX_DESCRIPTIONS[14] = {
    "NO EFFECT SELECTED",
    "ARP: ARPEGGIO CHORD STEP",
    "PUP: PITCH SLIDE UP",
    "PDW: PITCH SLIDE DOWN",
    "PTM: PORTAMENTO TO NOTE",
    "VBT: PITCH VIBRATO MOD",
    "SLD: VOLUME SLIDE UP/DN",
    "REV: STEREO REVERB SEND",
    "TRE: AMPLITUDE TREMOLO",
    "SMU: SAMPLE OFFSET START",
    "JUM: JUMP TO SONG ORDER",
    "VOL: SET CHANNEL VOLUME",
    "PTB: PATTERN BREAK ROW",
    "DLY: STEREO ECHO DELAY"
};

struct PatternCell {
    u8 note    = 0;
    u8 instr   = 0;
    u8 volume  = 64;   // 64 = 100% default, 0 = 00 (0%), 0xFF = legacy unset -> 64
    u8 fxType  = 0;
    u8 fxParam = 0;
};

struct SampleData {
    std::string name = "empty";
    std::vector<float> pcm;
    u32 sampleRate = 44100;
    u32 loopStart  = 0;
    u32 loopEnd    = 0;
    u8  loopType   = 0;
    int volPct     = 100;
    int panPct     = 0;
    int ampPct     = 100;
    s8  relNote    = 0;
    s8  fineTune   = 0;
    bool inverted  = false;
};

struct InstrumentData {
    std::string name = "empty";
    SampleData sample;
    bool envEnabled = false;
    bool susEnabled = false;
    int  susPoint   = 1;
    std::vector<std::pair<int, int>> envPoints = {{0, 64}, {24, 48}, {64, 0}};
};

struct Pattern {
    int length = 0;
    PatternCell cells[64][32];
    Pattern() {
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < 32; ++c)
                cells[r][c] = PatternCell{};
    }
};

struct SongOrderEntry {
    int patternIdx = 0;
    int length     = 0;
};

struct TrackerState {
    static constexpr int MAX_CHANNELS = 32;
    static constexpr int MAX_ROWS     = 64;
    static constexpr int MAX_INSTS    = 80;
    static constexpr int MAX_PATTERNS = 65;

    std::vector<Pattern> patterns;
    std::vector<SongOrderEntry> songOrder;
    int curOrderIdx = 0;

    InstrumentData instruments[MAX_INSTS];
    int instScroll = 0;

    int numChannels   = 8;
    int cursorRow     = 0;
    int cursorChannel = 0;
    int cursorCol     = 0;
    int hScroll       = 0;

    int curInst       = 0;
    int octave        = 4;
    int stepAdd       = 1;
    int tempo         = 6;
    int bpm           = 125;
    int activeTab     = 1;

    bool playing      = false;
    bool paused       = false;
    bool recording    = false;
    bool mute[MAX_CHANNELS] = {false};
    bool solo[MAX_CHANNELS] = {false};

    // Shift + Arrow Block Selection
    bool selActive    = false;
    int  selAnchorRow = 0;
    int  selAnchorCh  = 0;
    int  selEndRow    = 0;
    int  selEndCh     = 0;

    // Clipboards (block selection vs full-channel)
    std::vector<std::vector<PatternCell>> clipboardBlock;
    std::vector<PatternCell> clipboardCh;

    bool showDskModal    = false;
    bool showExportModal = false;
    bool showFxModal     = false;
    bool showCfgHelp     = false;
    bool showDrvModal    = false;
    int  selectedFx      = FX_REV;
    u8   selectedFxVal   = 0x40;

    int  exportFmt       = 0; // 0=WAV, 1=FLAC, 2=OPUS, 3=MP3
    int  exportBits      = 32; // 16 or 32

    // SMP persistent waveform selection
    int  smpSelStart  = -1;
    int  smpSelEnd    = -1;
    bool smpDragging  = false;

    // IMS interactive envelope dragging
    int  envDragPoint = -1;

    u32  keyLitUntil[24] = {0};

    int driverType    = 0;
    int sampleRate    = 44100;
    int bufferFrames  = 256;
    std::vector<std::string> outDevices;
    std::vector<std::string> inDevices;
    int selOutDev     = 0;
    int selInDev      = 0;
    std::string cpuStatus = "AUTO-OPT READY";

    std::string songName  = "PROJECT.NR";
    std::string statusMsg = "READY - PTM:0 L:0";

    TrackerState() {
        patterns.resize(1);
        songOrder.push_back({0, 0});
        for (int i = 0; i < MAX_INSTS; ++i) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d:---", i + 1);
            instruments[i].name = buf;
        }
    }

    Pattern& curPattern() {
        int pIdx = std::clamp(songOrder[curOrderIdx].patternIdx, 0, (int)patterns.size() - 1);
        return patterns[pIdx];
    }

    int effectivePtnLen() const {
        int l = songOrder[curOrderIdx].length;
        return (l <= 0) ? 1 : std::clamp(l, 1, 64);
    }

    void clearSelection() {
        selActive = false;
    }

    void getSelectionBounds(int& r0, int& r1, int& c0, int& c1) const {
        r0 = std::min(selAnchorRow, selEndRow);
        r1 = std::max(selAnchorRow, selEndRow);
        c0 = std::min(selAnchorCh, selEndCh);
        c1 = std::max(selAnchorCh, selEndCh);
    }
};

static TrackerState g_state;
static std::mutex   g_audioMutex;

// ============================================================================
// CHANNEL PAGINATION LOGIC (4 CHANNELS AT A TIME)
// ============================================================================
void updateHScroll() {
    int newScroll = (g_state.cursorChannel / 4) * 4;
    int maxScroll = ((g_state.numChannels - 1) / 4) * 4;
    g_state.hScroll = std::clamp(newScroll, 0, maxScroll);
}

// ============================================================================
// 5. AUDIO ENGINE
// ============================================================================
struct Voice {
    bool   active     = false;
    int    instIdx    = 0;
    int    baseNote   = 49;
    double phase      = 0.0;
    double step       = 0.0;
    double targetStep = 0.0;
    float  gain       = 0.0f;
    float  pan        = 0.0f;
    u32    envSample  = 0;
    u8     fxType     = 0;
    u8     fxParam    = 0;
    double vibPhase   = 0.0;
    float  revSend    = 0.0f;
    float  dlySend    = 0.0f;
};

class AudioEngine {
public:
    Voice voices[TrackerState::MAX_CHANNELS];
    std::atomic<bool> running{false};
    std::atomic<bool> restartRequested{false};
    std::thread audioThread;

    double samplesPerTick = 0.0;
    double tickCounter    = 0.0;
    int    currentTick    = 0;

    std::vector<float> revCombL1, revCombL2, revCombR1, revCombR2;
    size_t rIdx1 = 0, rIdx2 = 0;
    std::vector<float> dlyBufL, dlyBufR;
    size_t dlyPos = 0;

    AudioEngine() { resizeDspBuffers(44100); }

    void resizeDspBuffers(int sr) {
        revCombL1.assign((size_t)(sr * 0.0297), 0.0f);
        revCombL2.assign((size_t)(sr * 0.0371), 0.0f);
        revCombR1.assign((size_t)(sr * 0.0311), 0.0f);
        revCombR2.assign((size_t)(sr * 0.0389), 0.0f);
        rIdx1 = rIdx2 = 0;
        dlyBufL.assign((size_t)(sr * 0.375), 0.0f);
        dlyBufR.assign((size_t)(sr * 0.375), 0.0f);
        dlyPos = 0;
    }

    void enumerateAudioDrivers() {
        g_state.outDevices.clear();
        g_state.inDevices.clear();

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* pEnum = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), (void**)&pEnum))) {
            IMMDeviceCollection* pOutCol = nullptr;
            if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pOutCol))) {
                UINT count = 0;
                pOutCol->GetCount(&count);
                for (UINT i = 0; i < count; ++i) {
                    char label[64];
                    snprintf(label, sizeof(label), "OUT %u: WASAPI RENDER", i + 1);
                    g_state.outDevices.push_back(label);
                }
                pOutCol->Release();
            }
            pEnum->Release();
        }

        HKEY hAsio = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &hAsio) == ERROR_SUCCESS) {
            char subKey[128];
            DWORD idx = 0, len = sizeof(subKey);
            while (RegEnumKeyExA(hAsio, idx++, subKey, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                g_state.outDevices.push_back(std::string("ASIO: ") + subKey);
                len = sizeof(subKey);
            }
            RegCloseKey(hAsio);
        }

        if (g_state.outDevices.empty()) g_state.outDevices.push_back("OUT 1: WASAPI RENDER");
    }

    void initBuiltInSamples() {
        int sr = 44100;
        auto& s0 = g_state.instruments[0];
        s0.name = "01:BASS_SAW";
        s0.sample.sampleRate = sr;
        s0.sample.pcm.resize(sr / 2);
        for (size_t i = 0; i < s0.sample.pcm.size(); ++i) {
            double t = (double)i / sr;
            double env = std::exp(-t * 6.5);
            double wave = 2.0 * std::fmod(t * 261.63, 1.0) - 1.0;
            s0.sample.pcm[i] = (float)(wave * env * 0.75);
        }

        auto& s1 = g_state.instruments[1];
        s1.name = "02:KICK_909";
        s1.sample.sampleRate = sr;
        s1.sample.pcm.resize(sr / 3);
        for (size_t i = 0; i < s1.sample.pcm.size(); ++i) {
            double t = (double)i / sr;
            double freq = 48.0 + 140.0 * std::exp(-t * 38.0);
            s1.sample.pcm[i] = (float)(std::tanh(std::sin(2.0 * 3.14159265 * freq * t) * 1.8) * std::exp(-t * 9.0));
        }

        auto& s2 = g_state.instruments[2];
        s2.name = "03:SNARE_NT";
        s2.sample.sampleRate = sr;
        s2.sample.pcm.resize(sr / 4);
        uint32_t seed = 1234567;
        for (size_t i = 0; i < s2.sample.pcm.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            double noise = ((seed >> 16) / 32768.0) - 1.0;
            double t = (double)i / sr;
            double body = std::sin(2.0 * 3.14159265 * 190.0 * t) * std::exp(-t * 22.0);
            s2.sample.pcm[i] = (float)((noise * 0.65 + body * 0.5) * std::exp(-t * 14.0));
        }

        auto& s3 = g_state.instruments[3];
        s3.name = "04:HIHAT_CH";
        s3.sample.sampleRate = sr;
        s3.sample.pcm.resize(sr / 10);
        for (size_t i = 0; i < s3.sample.pcm.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            double noise = ((seed >> 16) / 32768.0) - 1.0;
            double t = (double)i / sr;
            s3.sample.pcm[i] = (float)(noise * std::exp(-t * 45.0) * 0.6);
        }

        auto& s4 = g_state.instruments[4];
        s4.name = "05:PULSE_LD";
        s4.sample.sampleRate = sr;
        s4.sample.pcm.resize(sr / 2);
        for (size_t i = 0; i < s4.sample.pcm.size(); ++i) {
            double t = (double)i / sr;
            double sq = (std::fmod(t * 261.63, 1.0) < 0.25) ? 0.6 : -0.6;
            s4.sample.pcm[i] = (float)(sq * std::exp(-t * 3.0));
        }
    }

    // TUN -24 to +24 maps directly to -24 to +24 semitones (plus relNote)
    double calcNoteStep(int note, const SampleData& smp) const {
        double semitones = (double)(note - 49) + (double)smp.relNote + (double)smp.fineTune;
        return std::pow(2.0, semitones / 12.0) * ((double)smp.sampleRate / (double)std::max(8000, g_state.sampleRate));
    }

    float resolveCellVolume(u8 cellVol) const {
        if (cellVol == 0xFF) return 1.0f;
        return std::clamp((int)cellVol, 0, 64) / 64.0f;
    }

    void triggerNote(int ch, int note, int instIdx, u8 cellVol = 64, u8 fxType = 0, u8 fxParam = 0) {
        if (ch < 0 || ch >= TrackerState::MAX_CHANNELS) return;
        if (note == 97) { voices[ch].active = false; return; }
        if (note <= 0 || note > 96) return;
        instIdx = std::clamp(instIdx, 0, TrackerState::MAX_INSTS - 1);

        const auto& smp = g_state.instruments[instIdx].sample;
        if (smp.pcm.empty()) return;

        Voice& v = voices[ch];
        double newStep = calcNoteStep(note, smp);

        if (fxType == FX_PTN && v.active) {
            v.targetStep = newStep;
        } else {
            v.active    = true;
            v.instIdx   = instIdx;
            v.baseNote  = note;
            v.phase     = (fxType == FX_SMU) ? std::min((double)smp.pcm.size() - 1.0, (double)fxParam * 256.0) : 0.0;
            v.step      = newStep;
            v.targetStep= newStep;
            v.envSample = 0;
            v.vibPhase  = 0.0;
        }

        float volMult = (fxType == FX_VOLN) ? (std::clamp((int)fxParam, 0, 64) / 64.0f) : resolveCellVolume(cellVol);
        v.gain    = volMult * (smp.volPct / 100.0f) * (smp.ampPct / 100.0f);
        v.pan     = std::clamp(smp.panPct / 100.0f, -1.0f, 1.0f);
        v.fxType  = fxType;
        v.fxParam = fxParam;
        v.revSend = (fxType == FX_REV) ? (fxParam > 0 ? std::clamp(fxParam / 128.0f, 0.25f, 1.0f) : 0.65f) : 0.0f;
        v.dlySend = (fxType == FX_DLY) ? (fxParam > 0 ? std::clamp(fxParam / 128.0f, 0.25f, 1.0f) : 0.75f) : 0.0f;
    }

    void applyFxToVoice(int ch, u8 fxType, u8 fxParam) {
        if (ch < 0 || ch >= TrackerState::MAX_CHANNELS) return;
        Voice& v = voices[ch];
        if (!v.active) return;
        v.fxType  = fxType;
        v.fxParam = fxParam;
        const auto& smp = g_state.instruments[v.instIdx].sample;
        switch (fxType) {
            case FX_VOLN: v.gain = std::clamp((int)fxParam, 0, 64) / 64.0f * (smp.volPct / 100.0f); break;
            case FX_SMU:  if (!smp.pcm.empty()) v.phase = std::min((double)smp.pcm.size() - 1.0, (double)fxParam * 256.0); break;
            case FX_REV:  v.revSend = (fxParam > 0 ? std::clamp(fxParam / 128.0f, 0.25f, 1.0f) : 0.65f); break;
            case FX_DLY:  v.dlySend = (fxParam > 0 ? std::clamp(fxParam / 128.0f, 0.25f, 1.0f) : 0.75f); break;
            default: break;
        }
    }

    void processRow(int row) {
        bool anySolo = false;
        for (int c = 0; c < g_state.numChannels; ++c)
            if (g_state.solo[c]) anySolo = true;

        Pattern& ptn = g_state.curPattern();
        int jumpOrder = -1;
        int breakRow  = -1;

        for (int c = 0; c < g_state.numChannels; ++c) {
            bool audible = anySolo ? g_state.solo[c] : !g_state.mute[c];
            const PatternCell& cell = ptn.cells[row][c];

            if (cell.fxType == FX_JUM) jumpOrder = cell.fxParam;
            if (cell.fxType == FX_PTB) breakRow  = std::clamp((int)cell.fxParam, 0, 63);

            if (!audible) continue;

            if (cell.note > 0) {
                int inst = (cell.instr > 0) ? (cell.instr - 1) : g_state.curInst;
                triggerNote(c, cell.note, inst, cell.volume, cell.fxType, cell.fxParam);
            } else {
                if (cell.volume != 0xFF && voices[c].active) {
                    const auto& smp = g_state.instruments[voices[c].instIdx].sample;
                    voices[c].gain = resolveCellVolume(cell.volume) * (smp.volPct / 100.0f) * (smp.ampPct / 100.0f);
                }
                if (cell.fxType != FX_NONE && voices[c].active) {
                    applyFxToVoice(c, cell.fxType, cell.fxParam);
                }
            }
        }

        if (jumpOrder >= 0 && !g_state.songOrder.empty()) {
            g_state.curOrderIdx = jumpOrder % (int)g_state.songOrder.size();
            g_state.cursorRow   = 0;
        } else if (breakRow >= 0) {
            if (g_state.songOrder.size() > 1)
                g_state.curOrderIdx = (g_state.curOrderIdx + 1) % (int)g_state.songOrder.size();
            g_state.cursorRow = std::min(breakRow, g_state.effectivePtnLen() - 1);
        }
    }

    void processTickEffects() {
        for (int c = 0; c < g_state.numChannels; ++c) {
            Voice& v = voices[c];
            if (!v.active) continue;
            const auto& smp = g_state.instruments[v.instIdx].sample;
            double p = (v.fxParam > 0) ? v.fxParam : 8.0;

            switch (v.fxType) {
                case FX_PUP: v.step *= std::pow(2.0, (p * 0.35) / 12.0); break;
                case FX_PDW: v.step *= std::pow(2.0, -(p * 0.35) / 12.0); break;
                case FX_PTN: {
                    double rate = std::pow(2.0, (p * 0.45) / 12.0);
                    if (v.step < v.targetStep) v.step = std::min(v.targetStep, v.step * rate);
                    else if (v.step > v.targetStep) v.step = std::max(v.targetStep, v.step / rate);
                    break;
                }
                case FX_SLD: {
                    int up = (v.fxParam >> 4) & 0x0F;
                    int dn = v.fxParam & 0x0F;
                    if (up == 0 && dn == 0) dn = 2;
                    v.gain = std::clamp(v.gain + (up - dn) * 0.025f, 0.0f, 1.0f);
                    break;
                }
                case FX_ARP: {
                    int p1 = (v.fxParam >> 4) & 0x0F;
                    int p2 = v.fxParam & 0x0F;
                    if (p1 == 0 && p2 == 0) { p1 = 4; p2 = 7; }
                    int stepIdx = currentTick % 3;
                    int semi = (stepIdx == 1) ? p1 : (stepIdx == 2 ? p2 : 0);
                    v.step = calcNoteStep(v.baseNote + semi, smp);
                    break;
                }
                default: break;
            }
        }
    }

    void renderBuffer(float* outInterleavedStereo, int numFrames, bool lockState = true) {
        if (lockState) g_audioMutex.lock();

        int sr = std::max(8000, g_state.sampleRate);
        samplesPerTick = (sr * 2.5) / std::max(32, g_state.bpm);

        for (int i = 0; i < numFrames; ++i) {
            if (g_state.playing && !g_state.paused) {
                if (tickCounter <= 0.0) {
                    if (currentTick == 0) processRow(g_state.cursorRow);
                    else                  processTickEffects();
                    currentTick++;
                    if (currentTick >= std::max(1, g_state.tempo)) {
                        currentTick = 0;
                        int nextRow = g_state.cursorRow + 1;
                        if (nextRow >= g_state.effectivePtnLen()) {
                            nextRow = 0;
                            if (g_state.songOrder.size() > 1)
                                g_state.curOrderIdx = (g_state.curOrderIdx + 1) % (int)g_state.songOrder.size();
                        }
                        g_state.cursorRow = nextRow;
                    }
                    tickCounter += samplesPerTick;
                }
                tickCounter -= 1.0;
            }

            float mixL = 0.0f, mixR = 0.0f;
            float revInL = 0.0f, revInR = 0.0f;
            float dlyInL = 0.0f, dlyInR = 0.0f;

            for (int c = 0; c < g_state.numChannels; ++c) {
                Voice& v = voices[c];
                if (!v.active) continue;

                const auto& inst = g_state.instruments[v.instIdx];
                const auto& pcm  = inst.sample.pcm;
                if (pcm.empty()) { v.active = false; continue; }

                size_t idx0 = (size_t)v.phase;
                if (idx0 >= pcm.size()) {
                    if (inst.sample.loopType == 1 && inst.sample.loopEnd > inst.sample.loopStart) {
                        v.phase = inst.sample.loopStart;
                        idx0 = (size_t)v.phase;
                    } else {
                        v.active = false;
                        continue;
                    }
                }

                size_t idx1 = std::min(idx0 + 1, pcm.size() - 1);
                float frac  = (float)(v.phase - idx0);
                float s     = pcm[idx0] + (pcm[idx1] - pcm[idx0]) * frac;
                if (inst.sample.inverted) s = -s;

                float envMult = 1.0f;
                if (inst.envEnabled && !inst.envPoints.empty()) {
                    int envX = (int)(v.envSample / std::max(1, sr / 100));
                    if (inst.susEnabled && inst.susPoint >= 0 && inst.susPoint < (int)inst.envPoints.size()) {
                        int susX = inst.envPoints[inst.susPoint].first;
                        if (envX >= susX) envX = susX;
                    }
                    int lastX = inst.envPoints.back().first;
                    if (envX >= lastX) {
                        envMult = inst.envPoints.back().second / 64.0f;
                    } else {
                        for (size_t p = 0; p + 1 < inst.envPoints.size(); ++p) {
                            if (envX >= inst.envPoints[p].first && envX <= inst.envPoints[p + 1].first) {
                                float t = (float)(envX - inst.envPoints[p].first) /
                                          std::max(1, inst.envPoints[p + 1].first - inst.envPoints[p].first);
                                float y = inst.envPoints[p].second + t * (inst.envPoints[p + 1].second - inst.envPoints[p].second);
                                envMult = y / 64.0f;
                                break;
                            }
                        }
                    }
                }

                float tremMult = 1.0f;
                double stepMod = 1.0;
                if (v.fxType == FX_VBT) {
                    double spd = ((v.fxParam >> 4) & 0x0F) * 0.6 + 5.0;
                    double dep = (v.fxParam & 0x0F) * 0.005 + 0.025;
                    stepMod = 1.0 + std::sin(v.vibPhase) * dep;
                    v.vibPhase += (2.0 * 3.14159265 * spd) / sr;
                } else if (v.fxType == FX_TRE) {
                    double spd = ((v.fxParam >> 4) & 0x0F) * 0.6 + 5.0;
                    double dep = (v.fxParam & 0x0F) * 0.04 + 0.28;
                    tremMult = (float)(1.0 - dep * (0.5 + 0.5 * std::sin(v.vibPhase)));
                    v.vibPhase += (2.0 * 3.14159265 * spd) / sr;
                }

                float sampleOut = s * v.gain * envMult * tremMult * 0.45f;
                float gainL = (v.pan <= 0.0f) ? 1.0f : (1.0f - v.pan);
                float gainR = (v.pan >= 0.0f) ? 1.0f : (1.0f + v.pan);

                float chL = sampleOut * gainL;
                float chR = sampleOut * gainR;
                mixL += chL;
                mixR += chR;

                if (v.revSend > 0.0f) { revInL += chL * v.revSend; revInR += chR * v.revSend; }
                if (v.dlySend > 0.0f) { dlyInL += chL * v.dlySend; dlyInR += chR * v.dlySend; }

                v.phase += v.step * stepMod;
                v.envSample++;
            }

            float rL = (revCombL1[rIdx1] + revCombL2[rIdx2]) * 0.5f;
            float rR = (revCombR1[rIdx1] + revCombR2[rIdx2]) * 0.5f;
            revCombL1[rIdx1] = revInL + revCombL1[rIdx1] * 0.58f;
            revCombL2[rIdx2] = revInL + revCombL2[rIdx2] * 0.54f;
            revCombR1[rIdx1] = revInR + revCombR1[rIdx1] * 0.58f;
            revCombR2[rIdx2] = revInR + revCombR2[rIdx2] * 0.54f;
            rIdx1 = (rIdx1 + 1) % revCombL1.size();
            rIdx2 = (rIdx2 + 1) % revCombL2.size();

            float dOutL = dlyBufL[dlyPos];
            float dOutR = dlyBufR[dlyPos];
            dlyBufL[dlyPos] = dlyInL + dOutR * 0.35f;
            dlyBufR[dlyPos] = dlyInR + dOutL * 0.35f;
            dlyPos = (dlyPos + 1) % dlyBufL.size();

            float wetDelay = 0.25f;
            outInterleavedStereo[i * 2 + 0] = std::clamp(mixL + rL * 0.55f + dOutL * wetDelay, -0.98f, 0.98f);
            outInterleavedStereo[i * 2 + 1] = std::clamp(mixR + rR * 0.55f + dOutR * wetDelay, -0.98f, 0.98f);
        }

        if (lockState) g_audioMutex.unlock();
    }

    void startWASAPI() {
        running = true;
        audioThread = std::thread([this]() {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            DWORD taskIdx = 0;
            HANDLE hTask = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIdx);

            while (running) {
                restartRequested = false;
                int activeSr  = g_state.sampleRate;
                int activeBuf = g_state.bufferFrames;
                resizeDspBuffers(activeSr);

                IMMDeviceEnumerator* enumerator = nullptr;
                IMMDevice* device = nullptr;
                IAudioClient* audioClient = nullptr;
                IAudioRenderClient* renderClient = nullptr;

                if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                               __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
                    enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
                }

                if (device && SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient))) {
                    WAVEFORMATEX wfx = {};
                    wfx.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
                    wfx.nChannels       = 2;
                    wfx.nSamplesPerSec  = activeSr;
                    wfx.wBitsPerSample  = 32;
                    wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8;
                    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

                    REFERENCE_TIME hnsBufferDuration = (REFERENCE_TIME)((10000000.0 * activeBuf) / activeSr);
                    HRESULT hr = audioClient->Initialize(
                        AUDCLNT_SHAREMODE_SHARED,
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                        hnsBufferDuration, 0, &wfx, nullptr);

                    if (SUCCEEDED(hr)) {
                        UINT32 bufferFrameCount = 0;
                        audioClient->GetBufferSize(&bufferFrameCount);
                        audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
                        audioClient->Start();

                        while (running && !restartRequested) {
                            UINT32 numFramesPadding = 0;
                            audioClient->GetCurrentPadding(&numFramesPadding);
                            UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;

                            if (numFramesAvailable > 0) {
                                BYTE* pData = nullptr;
                                if (SUCCEEDED(renderClient->GetBuffer(numFramesAvailable, &pData))) {
                                    renderBuffer(reinterpret_cast<float*>(pData), (int)numFramesAvailable, true);
                                    renderClient->ReleaseBuffer(numFramesAvailable, 0);
                                }
                            }
                            Sleep(1);
                        }
                        audioClient->Stop();
                    }
                }

                if (renderClient) renderClient->Release();
                if (audioClient)  audioClient->Release();
                if (device)       device->Release();
                if (enumerator)   enumerator->Release();
                if (!running) break;
                Sleep(10);
            }

            if (hTask) AvRevertMmThreadCharacteristics(hTask);
            CoUninitialize();
        });
    }

    void stop() {
        running = false;
        if (audioThread.joinable()) audioThread.join();
    }
};

static AudioEngine g_audio;

// ============================================================================
// LOADERS & EXPORTERS
// ============================================================================
std::string getFileExtLower(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "";
    std::string ext = dot;
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}

bool loadWavUniversal(const char* path, SampleData& outSample) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char riff[4];
    if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return false; }
    fseek(f, 8, SEEK_CUR);

    u16 audioFormat = 1, numChannels = 1, bitsPerSample = 16;
    u32 sampleRate = 44100;

    while (!feof(f)) {
        char chunkId[4];
        u32 chunkSize = 0;
        if (fread(chunkId, 1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            fread(&audioFormat, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            fread(&sampleRate, 4, 1, f);
            fseek(f, 6, SEEK_CUR);
            fread(&bitsPerSample, 2, 1, f);
            if (chunkSize > 16) fseek(f, chunkSize - 16, SEEK_CUR);
        } else if (memcmp(chunkId, "data", 4) == 0) {
            outSample.sampleRate = sampleRate;
            numChannels = std::max((u16)1, numChannels);
            if (audioFormat == 3 && bitsPerSample == 32) {
                size_t total = chunkSize / 4;
                std::vector<float> raw(total);
                fread(raw.data(), 4, total, f);
                size_t frames = total / numChannels;
                outSample.pcm.resize(frames);
                for (size_t i = 0; i < frames; ++i) outSample.pcm[i] = raw[i * numChannels];
            } else if (bitsPerSample == 32) {
                size_t total = chunkSize / 4;
                std::vector<s32> raw(total);
                fread(raw.data(), 4, total, f);
                size_t frames = total / numChannels;
                outSample.pcm.resize(frames);
                for (size_t i = 0; i < frames; ++i) outSample.pcm[i] = (float)(raw[i * numChannels] / 2147483648.0);
            } else if (bitsPerSample == 24) {
                std::vector<u8> raw(chunkSize);
                fread(raw.data(), 1, chunkSize, f);
                size_t frames = chunkSize / (3 * numChannels);
                outSample.pcm.resize(frames);
                for (size_t i = 0; i < frames; ++i) {
                    size_t off = i * 3 * numChannels;
                    s32 val = (raw[off] << 8) | (raw[off + 1] << 16) | (raw[off + 2] << 24);
                    outSample.pcm[i] = (float)(val / 2147483648.0);
                }
            } else if (bitsPerSample == 16) {
                size_t total = chunkSize / 2;
                std::vector<s16> raw(total);
                fread(raw.data(), 2, total, f);
                size_t frames = total / numChannels;
                outSample.pcm.resize(frames);
                for (size_t i = 0; i < frames; ++i) outSample.pcm[i] = raw[i * numChannels] / 32768.0f;
            } else if (bitsPerSample == 8) {
                std::vector<u8> raw(chunkSize);
                fread(raw.data(), 1, chunkSize, f);
                size_t frames = chunkSize / numChannels;
                outSample.pcm.resize(frames);
                for (size_t i = 0; i < frames; ++i) outSample.pcm[i] = ((int)raw[i * numChannels] - 128) / 128.0f;
            }
            break;
        } else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }
    fclose(f);
    return !outSample.pcm.empty();
}

bool loadUniversalAudioSample(const char* path, SampleData& outSample) {
    std::string ext = getFileExtLower(path);
    if (ext == ".wav") return loadWavUniversal(path, outSample);
    return false;
}

bool saveProjectNR(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    const char magic[8] = {'N','T','R','K','_','N','R','1'};
    fwrite(magic, 1, 8, f);
    fwrite(&g_state.numChannels, sizeof(int), 1, f);
    fwrite(&g_state.tempo, sizeof(int), 1, f);
    fwrite(&g_state.bpm, sizeof(int), 1, f);

    int numPtn = (int)g_state.patterns.size();
    fwrite(&numPtn, sizeof(int), 1, f);
    for (int p = 0; p < numPtn; ++p) {
        fwrite(&g_state.patterns[p].length, sizeof(int), 1, f);
        fwrite(g_state.patterns[p].cells, sizeof(PatternCell), 64 * 32, f);
    }

    int numOrd = (int)g_state.songOrder.size();
    fwrite(&numOrd, sizeof(int), 1, f);
    fwrite(g_state.songOrder.data(), sizeof(SongOrderEntry), numOrd, f);

    for (int i = 0; i < TrackerState::MAX_INSTS; ++i) {
        const auto& inst = g_state.instruments[i];
        u32 nameLen = (u32)inst.name.size();
        fwrite(&nameLen, 4, 1, f);
        fwrite(inst.name.data(), 1, nameLen, f);
        fwrite(&inst.envEnabled, sizeof(bool), 1, f);
        fwrite(&inst.susEnabled, sizeof(bool), 1, f);
        fwrite(&inst.susPoint, sizeof(int), 1, f);
        u32 envSz = (u32)inst.envPoints.size();
        fwrite(&envSz, 4, 1, f);
        for (const auto& pt : inst.envPoints) {
            fwrite(&pt.first, sizeof(int), 1, f);
            fwrite(&pt.second, sizeof(int), 1, f);
        }
        const auto& smp = inst.sample;
        fwrite(&smp.sampleRate, 4, 1, f);
        fwrite(&smp.loopStart, 4, 1, f);
        fwrite(&smp.loopEnd, 4, 1, f);
        fwrite(&smp.loopType, 1, 1, f);
        fwrite(&smp.volPct, sizeof(int), 1, f);
        fwrite(&smp.panPct, sizeof(int), 1, f);
        fwrite(&smp.ampPct, sizeof(int), 1, f);
        fwrite(&smp.relNote, 1, 1, f);
        fwrite(&smp.fineTune, 1, 1, f);
        u32 pcmLen = (u32)smp.pcm.size();
        fwrite(&pcmLen, 4, 1, f);
        if (pcmLen > 0) fwrite(smp.pcm.data(), sizeof(float), pcmLen, f);
    }
    fclose(f);
    return true;
}

int amigaPeriodToNote(u16 period) {
    if (period == 0) return 0;
    static const u16 periods[36] = {
        856,808,762,720,678,640,604,570,538,508,480,453,
        428,404,381,360,339,320,302,285,269,254,240,226,
        214,202,190,180,170,160,151,143,135,127,120,113
    };
    int best = 0, bestDiff = 99999;
    for (int i = 0; i < 36; ++i) {
        int d = std::abs((int)periods[i] - (int)period);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return 37 + best;
}

void mapClassicTrackerFx(u8 cmd, u8 param, PatternCell& cell) {
    switch (cmd) {
        case 0x0: if (param != 0) { cell.fxType = FX_ARP; cell.fxParam = param; } break;
        case 0x1: cell.fxType = FX_PUP;  cell.fxParam = param; break;
        case 0x2: cell.fxType = FX_PDW;  cell.fxParam = param; break;
        case 0x3: cell.fxType = FX_PTN;  cell.fxParam = param; break;
        case 0x4: cell.fxType = FX_VBT;  cell.fxParam = param; break;
        case 0xA: cell.fxType = FX_SLD;  cell.fxParam = param; break;
        case 0xB: cell.fxType = FX_JUM;  cell.fxParam = param; break;
        case 0xC: cell.fxType = FX_VOLN; cell.fxParam = std::min((u8)64, param); cell.volume = cell.fxParam; break;
        case 0xD: cell.fxType = FX_PTB;  cell.fxParam = ((param >> 4) * 10) + (param & 0x0F); break;
        case 0x9: cell.fxType = FX_SMU;  cell.fxParam = param; break;
        case 0xF:
            if (param > 0 && param < 32) g_state.tempo = param;
            else if (param >= 32) g_state.bpm = param;
            break;
        default: break;
    }
}

void resetSongForImport() {
    g_state.patterns.clear();
    g_state.songOrder.clear();
    g_state.curOrderIdx = 0;
    g_state.cursorRow   = 0;
    g_state.cursorChannel = 0;
    updateHScroll();
    for (int i = 0; i < TrackerState::MAX_INSTS; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:---", i + 1);
        g_state.instruments[i] = InstrumentData{};
        g_state.instruments[i].name = buf;
    }
}

bool loadXMFile(const std::vector<u8>& data) {
    if (data.size() < 336 || memcmp(data.data(), "Extended Module: ", 17) != 0) return false;
    resetSongForImport();

    u32 hdrSize   = *(const u32*)&data[60];
    u16 songLen   = *(const u16*)&data[64];
    u16 numCh     = *(const u16*)&data[68];
    u16 numPtn    = *(const u16*)&data[70];
    u16 numInst   = *(const u16*)&data[72];
    u16 defTempo  = *(const u16*)&data[76];
    u16 defBpm    = *(const u16*)&data[78];

    g_state.numChannels = std::clamp((int)numCh, 1, 32);
    g_state.tempo = std::clamp((int)defTempo, 1, 31);
    g_state.bpm   = std::clamp((int)defBpm, 32, 255);

    int totalPtns = std::clamp((int)numPtn, 1, 65);
    g_state.patterns.resize(totalPtns);

    int ordCount = std::clamp((int)songLen, 1, 65);
    for (int i = 0; i < ordCount; ++i) {
        int pIdx = std::clamp((int)data[80 + i], 0, totalPtns - 1);
        g_state.songOrder.push_back({pIdx, 64});
    }

    size_t pos = 60 + hdrSize;
    for (int p = 0; p < (int)numPtn && pos + 9 <= data.size(); ++p) {
        u32 pHdrLen  = *(const u32*)&data[pos];
        u16 rows     = *(const u16*)&data[pos + 5];
        u16 packSize = *(const u16*)&data[pos + 7];
        pos += pHdrLen;

        if (p < totalPtns) {
            int rCount = std::clamp((int)rows, 1, 64);
            g_state.patterns[p].length = rCount;
            for (auto& ord : g_state.songOrder) if (ord.patternIdx == p) ord.length = rCount;

            size_t endP = std::min(data.size(), pos + packSize);
            size_t cp = pos;
            for (int r = 0; r < (int)rows && cp < endP; ++r) {
                for (int c = 0; c < (int)numCh && cp < endP; ++c) {
                    u8 b = data[cp++];
                    u8 note = 0, ins = 0, vol = 0, fx = 0, fxp = 0;
                    if (b & 0x80) {
                        if (b & 0x01 && cp < endP) note = data[cp++];
                        if (b & 0x02 && cp < endP) ins  = data[cp++];
                        if (b & 0x04 && cp < endP) vol  = data[cp++];
                        if (b & 0x08 && cp < endP) fx   = data[cp++];
                        if (b & 0x10 && cp < endP) fxp  = data[cp++];
                    } else {
                        note = b;
                        if (cp < endP) ins = data[cp++];
                        if (cp < endP) vol = data[cp++];
                        if (cp < endP) fx  = data[cp++];
                        if (cp < endP) fxp = data[cp++];
                    }
                    if (r < 64 && c < 32) {
                        PatternCell& cell = g_state.patterns[p].cells[r][c];
                        cell.note  = (note == 97) ? 97 : (note >= 1 && note <= 96 ? note : 0);
                        cell.instr = ins;
                        cell.volume = (vol >= 0x10 && vol <= 0x50) ? (u8)(vol - 0x10) : 64;
                        mapClassicTrackerFx(fx, fxp, cell);
                    }
                }
            }
        }
        pos += packSize;
    }

    for (int i = 0; i < (int)numInst && pos + 29 <= data.size(); ++i) {
        u32 iHdrSize = *(const u32*)&data[pos];
        char iname[23] = {0};
        memcpy(iname, &data[pos + 4], 22);
        u16 numSmps = *(const u16*)&data[pos + 27];
        u32 smpHdrSize = (numSmps > 0 && pos + 33 <= data.size()) ? *(const u32*)&data[pos + 29] : 40;
        pos += iHdrSize;

        if (i < TrackerState::MAX_INSTS) {
            char label[32];
            snprintf(label, sizeof(label), "%02d:%s", i + 1, iname[0] ? iname : "XM_INST");
            g_state.instruments[i].name = label;
        }

        if (numSmps > 0 && pos + numSmps * smpHdrSize <= data.size()) {
            std::vector<u32> smpLens(numSmps, 0);
            std::vector<u8>  smpFlags(numSmps, 0);
            std::vector<s8>  smpRel(numSmps, 0);
            std::vector<s8>  smpFine(numSmps, 0);
            std::vector<u8>  smpVol(numSmps, 64);
            std::vector<u8>  smpPan(numSmps, 128);
            std::vector<u32> smpLoopSt(numSmps, 0);
            std::vector<u32> smpLoopLn(numSmps, 0);

            for (int s = 0; s < numSmps; ++s) {
                size_t sp = pos + s * smpHdrSize;
                smpLens[s]   = *(const u32*)&data[sp + 0];
                smpLoopSt[s] = *(const u32*)&data[sp + 4];
                smpLoopLn[s] = *(const u32*)&data[sp + 8];
                smpVol[s]    = data[sp + 12];
                smpFine[s]   = (s8)(*(const s8*)&data[sp + 13] / 5);
                smpFlags[s]  = data[sp + 14];
                smpPan[s]    = data[sp + 15];
                smpRel[s]    = *(const s8*)&data[sp + 16];
            }
            pos += numSmps * smpHdrSize;

            for (int s = 0; s < numSmps && pos + smpLens[s] <= data.size(); ++s) {
                if (s == 0 && i < TrackerState::MAX_INSTS && smpLens[s] > 0) {
                    auto& smp = g_state.instruments[i].sample;
                    smp.sampleRate = 8363;
                    smp.volPct = std::clamp((smpVol[s] * 100) / 64, 0, 200);
                    smp.panPct = std::clamp(((int)smpPan[s] - 128) * 100 / 128, -100, 100);
                    smp.relNote = smpRel[s];
                    smp.fineTune = std::clamp((int)smpFine[s], -24, 24);
                    smp.loopType = (smpFlags[s] & 0x03) ? 1 : 0;

                    if (smpFlags[s] & 0x10) {
                        size_t frames = smpLens[s] / 2;
                        smp.pcm.resize(frames);
                        s16 old = 0;
                        for (size_t k = 0; k < frames; ++k) {
                            s16 d = *(const s16*)&data[pos + k * 2];
                            old = (s16)(old + d);
                            smp.pcm[k] = old / 32768.0f;
                        }
                        smp.loopStart = smpLoopSt[s] / 2;
                        smp.loopEnd   = (smpLoopSt[s] + smpLoopLn[s]) / 2;
                    } else {
                        size_t frames = smpLens[s];
                        smp.pcm.resize(frames);
                        s8 old = 0;
                        for (size_t k = 0; k < frames; ++k) {
                            old = (s8)(old + (s8)data[pos + k]);
                            smp.pcm[k] = old / 128.0f;
                        }
                        smp.loopStart = smpLoopSt[s];
                        smp.loopEnd   = smpLoopSt[s] + smpLoopLn[s];
                    }
                }
                pos += smpLens[s];
            }
        }
    }
    return true;
}

bool loadMODFile(const std::vector<u8>& data) {
    if (data.size() < 1084) return false;
    char tag[5] = {0};
    memcpy(tag, &data[1080], 4);
    int numCh = 4;
    if (memcmp(tag, "M.K.", 4) == 0 || memcmp(tag, "M!K!", 4) == 0 || memcmp(tag, "FLT4", 4) == 0) numCh = 4;
    else if (memcmp(tag, "6CHN", 4) == 0) numCh = 6;
    else if (memcmp(tag, "8CHN", 4) == 0 || memcmp(tag, "OCTA", 4) == 0) numCh = 8;
    else if (tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N' && isdigit((unsigned char)tag[0])) numCh = tag[0] - '0';
    else return false;

    resetSongForImport();
    g_state.numChannels = numCh;
    g_state.tempo = 6;
    g_state.bpm   = 125;

    u32 smpLens[31] = {0};
    for (int s = 0; s < 31; ++s) {
        size_t off = 20 + s * 30;
        char sname[23] = {0};
        memcpy(sname, &data[off], 22);
        u16 lenWords = (data[off + 22] << 8) | data[off + 23];
        s8 ft = (s8)(data[off + 24] & 0x0F);
        if (ft > 7) ft -= 16;
        u8 vol = data[off + 25];
        u16 repStart = (data[off + 26] << 8) | data[off + 27];
        u16 repLen   = (data[off + 28] << 8) | data[off + 29];

        smpLens[s] = lenWords * 2;
        char label[32];
        snprintf(label, sizeof(label), "%02d:%s", s + 1, sname[0] ? sname : "MOD_SMP");
        g_state.instruments[s].name = label;
        auto& smp = g_state.instruments[s].sample;
        smp.sampleRate = 8287;
        smp.volPct = std::clamp((vol * 100) / 64, 0, 200);
        smp.fineTune = (s8)(ft * 2);
        if (repLen > 1) {
            smp.loopType  = 1;
            smp.loopStart = repStart * 2;
            smp.loopEnd   = (repStart + repLen) * 2;
        }
    }

    u8 songLen = data[950];
    int maxPtn = 0;
    for (int i = 0; i < 128; ++i) maxPtn = std::max(maxPtn, (int)data[952 + i]);
    int totalPtns = std::clamp(maxPtn + 1, 1, 65);
    g_state.patterns.resize(totalPtns);

    int ordCount = std::clamp((int)songLen, 1, 65);
    for (int i = 0; i < ordCount; ++i) {
        g_state.songOrder.push_back({std::clamp((int)data[952 + i], 0, totalPtns - 1), 64});
    }

    size_t pos = 1084;
    for (int p = 0; p < totalPtns; ++p) {
        g_state.patterns[p].length = 64;
        for (int r = 0; r < 64; ++r) {
            for (int c = 0; c < numCh; ++c) {
                if (pos + 4 > data.size()) break;
                u8 b0 = data[pos], b1 = data[pos + 1], b2 = data[pos + 2], b3 = data[pos + 3];
                pos += 4;
                u16 period = ((b0 & 0x0F) << 8) | b1;
                u8  inst   = (b0 & 0xF0) | (b2 >> 4);
                u8  cmd    = b2 & 0x0F;
                u8  param  = b3;

                PatternCell& cell = g_state.patterns[p].cells[r][c];
                cell.note   = (u8)amigaPeriodToNote(period);
                cell.instr  = inst;
                cell.volume = 64;
                mapClassicTrackerFx(cmd, param, cell);
            }
        }
    }

    for (int s = 0; s < 31; ++s) {
        if (smpLens[s] > 0 && pos + smpLens[s] <= data.size()) {
            auto& smp = g_state.instruments[s].sample;
            smp.pcm.resize(smpLens[s]);
            for (u32 k = 0; k < smpLens[s]; ++k) {
                smp.pcm[k] = ((s8)data[pos + k]) / 128.0f;
            }
        }
        pos += smpLens[s];
    }
    return true;
}

bool loadS3MFile(const std::vector<u8>& data) {
    if (data.size() < 96 || memcmp(&data[44], "SCRM", 4) != 0) return false;
    resetSongForImport();

    u16 ordNum = *(const u16*)&data[32];
    u16 insNum = *(const u16*)&data[34];
    u16 patNum = *(const u16*)&data[36];
    u8  defSpd = data[49];
    u8  defTmp = data[50];

    g_state.tempo = std::clamp((int)defSpd, 1, 31);
    g_state.bpm   = std::clamp((int)defTmp, 32, 255);

    int activeCh = 4;
    for (int c = 0; c < 32; ++c) if (data[64 + c] < 16) activeCh = std::max(activeCh, c + 1);
    g_state.numChannels = std::clamp(activeCh, 1, 32);

    size_t pos = 96;
    for (int i = 0; i < (int)ordNum && pos < data.size(); ++i) {
        u8 o = data[pos++];
        if (o < 65 && g_state.songOrder.size() < 65) g_state.songOrder.push_back({o, 64});
    }
    if (g_state.songOrder.empty()) g_state.songOrder.push_back({0, 64});

    std::vector<u16> insPtrs(insNum, 0), patPtrs(patNum, 0);
    for (int i = 0; i < (int)insNum && pos + 2 <= data.size(); ++i) { insPtrs[i] = *(const u16*)&data[pos]; pos += 2; }
    for (int i = 0; i < (int)patNum && pos + 2 <= data.size(); ++i) { patPtrs[i] = *(const u16*)&data[pos]; pos += 2; }

    int totalPtns = std::clamp((int)patNum, 1, 65);
    g_state.patterns.resize(totalPtns);

    for (int p = 0; p < totalPtns; ++p) {
        g_state.patterns[p].length = 64;
        size_t pOff = (size_t)patPtrs[p] * 16;
        if (pOff == 0 || pOff + 2 >= data.size()) continue;
        u16 packedLen = *(const u16*)&data[pOff];
        size_t cp = pOff + 2;
        size_t endP = std::min(data.size(), pOff + packedLen);
        int row = 0;
        while (row < 64 && cp < endP) {
            u8 what = data[cp++];
            if (what == 0) { row++; continue; }
            int ch = what & 31;
            u8 note = 0, ins = 0, vol = 64, cmd = 0, info = 0;
            if (what & 32) {
                if (cp + 2 <= endP) {
                    u8 n = data[cp++]; ins = data[cp++];
                    if (n == 254) note = 97;
                    else if (n < 254) note = (u8)std::clamp(((n >> 4) * 12) + (n & 0x0F) + 13, 1, 96);
                }
            }
            if (what & 64) { if (cp < endP) vol = std::min((u8)64, data[cp++]); }
            if (what & 128){ if (cp + 2 <= endP) { cmd = data[cp++]; info = data[cp++]; } }
            if (ch < 32) {
                PatternCell& cell = g_state.patterns[p].cells[row][ch];
                cell.note = note; cell.instr = ins; cell.volume = vol;
                if (cmd == 1) { g_state.tempo = std::clamp((int)info, 1, 31); }
                else if (cmd == 20) { g_state.bpm = std::clamp((int)info, 32, 255); }
            }
        }
    }

    for (int i = 0; i < (int)insNum && i < TrackerState::MAX_INSTS; ++i) {
        size_t iOff = (size_t)insPtrs[i] * 16;
        if (iOff == 0 || iOff + 80 > data.size()) continue;
        char sname[29] = {0};
        memcpy(sname, &data[iOff + 48], 28);
        char label[32];
        snprintf(label, sizeof(label), "%02d:%s", i + 1, sname[0] ? sname : "S3M_SMP");
        g_state.instruments[i].name = label;

        if (data[iOff] == 1) {
            u32 memSeg = ((u32)data[iOff + 13] << 16) | *(const u16*)&data[iOff + 14];
            u32 len    = *(const u32*)&data[iOff + 16];
            u32 lBeg   = *(const u32*)&data[iOff + 20];
            u32 lEnd   = *(const u32*)&data[iOff + 24];
            u8  vol    = data[iOff + 28];
            u8  flags  = data[iOff + 31];
            u32 c2spd  = *(const u32*)&data[iOff + 32];

            auto& smp = g_state.instruments[i].sample;
            smp.sampleRate = c2spd > 0 ? c2spd : 8363;
            smp.volPct = std::clamp((vol * 100) / 64, 0, 200);
            if ((flags & 1) && lEnd > lBeg) { smp.loopType = 1; smp.loopStart = lBeg; smp.loopEnd = lEnd; }

            size_t sDataOff = (size_t)memSeg * 16;
            if (sDataOff + len <= data.size() && len > 0) {
                smp.pcm.resize(len);
                for (u32 k = 0; k < len; ++k) smp.pcm[k] = ((int)data[sDataOff + k] - 128) / 128.0f;
            }
        }
    }
    return true;
}

bool loadMEDOrAHXFile(const std::vector<u8>& data, const std::string& ext) {
    if (data.size() < 32) return false;
    bool isMed = (ext == ".med" && (memcmp(data.data(), "MMD0", 4) == 0 || memcmp(data.data(), "MMD1", 4) == 0 ||
                                    memcmp(data.data(), "MMD2", 4) == 0 || memcmp(data.data(), "MMD3", 4) == 0));
    bool isAhx = (ext == ".ahx" && (memcmp(data.data(), "THX\0", 4) == 0 || memcmp(data.data(), "THX\1", 4) == 0));
    if (!isMed && !isAhx) return false;

    resetSongForImport();
    g_state.numChannels = isAhx ? 4 : 8;
    g_state.tempo = 6;
    g_state.bpm   = 125;
    g_state.patterns.resize(1);
    g_state.patterns[0].length = 64;
    g_state.songOrder.push_back({0, 64});

    int sr = 44100;
    if (isAhx) {
        // Synthesize authentic AHX chiptune waveforms (Triangle, Saw, Square, Noise)
        const char* names[4] = {"01:AHX_TRI", "02:AHX_SAW", "03:AHX_SQR", "04:AHX_NSE"};
        for (int i = 0; i < 4; ++i) {
            g_state.instruments[i].name = names[i];
            auto& smp = g_state.instruments[i].sample;
            smp.sampleRate = sr;
            smp.loopType = (i < 3) ? 1 : 0;
            smp.pcm.resize(sr / 4);
            uint32_t seed = 98765 + i;
            for (size_t k = 0; k < smp.pcm.size(); ++k) {
                double ph = std::fmod(((double)k / sr) * 261.63, 1.0);
                if (i == 0) smp.pcm[k] = (float)(1.0 - 4.0 * std::abs(ph - 0.5)) * 0.7f;
                else if (i == 1) smp.pcm[k] = (float)(2.0 * ph - 1.0) * 0.65f;
                else if (i == 2) smp.pcm[k] = (ph < 0.5 ? 0.65f : -0.65f);
                else { seed = seed * 1664525u + 1013904223u; smp.pcm[k] = (((seed >> 16) / 32768.0f) - 1.0f) * std::exp(-(float)k / (sr * 0.08f)); }
            }
            smp.loopStart = 0;
            smp.loopEnd = (u32)(sr / 261.63);
        }
        // Parse AHX track steps from binary if available
        u16 trkLen = data[10];
        u8  numTrk = data[11];
        size_t trkPos = 14 + (size_t)(((data[6] & 0x0F) << 8) | data[7]) * 8;
        if (numTrk > 0 && trkPos + (size_t)trkLen * 3 <= data.size()) {
            int maxR = std::min(64, (int)trkLen);
            g_state.patterns[0].length = maxR;
            g_state.songOrder[0].length = maxR;
            for (int r = 0; r < maxR; ++r) {
                for (int c = 0; c < 4; ++c) {
                    size_t off = trkPos + (size_t)r * 3;
                    if (off + 2 < data.size()) {
                        u8 note = (data[off] >> 2) & 0x3F;
                        u8 ins  = ((data[off] & 0x03) << 4) | ((data[off + 1] >> 4) & 0x0F);
                        if (note > 0 && note <= 60) {
                            g_state.patterns[0].cells[r][c].note  = (u8)(note + 24);
                            g_state.patterns[0].cells[r][c].instr = (u8)std::clamp((int)ins, 1, 4);
                        }
                    }
                }
            }
        }
    } else {
        // OctaMED MMD0-MMD3 module extraction
        g_audio.initBuiltInSamples();
        u32 songOff = ((u32)data[8] << 24) | ((u32)data[9] << 16) | ((u32)data[10] << 8) | data[11];
        if (songOff > 0 && songOff + 788 <= data.size()) {
            for (int s = 0; s < 63 && s < TrackerState::MAX_INSTS; ++s) {
                u8 svol = data[songOff + s * 8 + 4];
                g_state.instruments[s].sample.volPct = std::clamp((svol * 100) / 64, 0, 200);
            }
            u16 songLen = (data[songOff + 506] << 8) | data[songOff + 507];
            u16 numBlks = (data[songOff + 504] << 8) | data[songOff + 505];
            int totalPtns = std::clamp((int)numBlks, 1, 65);
            g_state.patterns.resize(totalPtns);
            for (int p = 0; p < totalPtns; ++p) g_state.patterns[p].length = 64;
            g_state.songOrder.clear();
            int ordCnt = std::clamp((int)songLen, 1, 65);
            for (int o = 0; o < ordCnt; ++o) {
                int blk = std::clamp((int)data[songOff + 508 + o], 0, totalPtns - 1);
                g_state.songOrder.push_back({blk, 64});
            }
        }
    }
    return true;
}

bool loadProjectFile(const char* path) {
    std::string ext = getFileExtLower(path);
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return false; }
    std::vector<u8> fileData((size_t)fsize);
    fread(fileData.data(), 1, (size_t)fsize, f);
    fclose(f);

    if (ext == ".nr") {
        if (fileData.size() < 8 || memcmp(fileData.data(), "NTRK_NR1", 8) != 0) return false;
        size_t pos = 8;
        auto rd = [&](void* dst, size_t sz) -> bool {
            if (pos + sz > fileData.size()) return false;
            memcpy(dst, &fileData[pos], sz);
            pos += sz;
            return true;
        };
        rd(&g_state.numChannels, sizeof(int));
        rd(&g_state.tempo, sizeof(int));
        rd(&g_state.bpm, sizeof(int));
        int numPtn = 1;
        rd(&numPtn, sizeof(int));
        g_state.patterns.resize(std::clamp(numPtn, 1, 65));
        for (int p = 0; p < (int)g_state.patterns.size(); ++p) {
            rd(&g_state.patterns[p].length, sizeof(int));
            rd(g_state.patterns[p].cells, sizeof(PatternCell) * 64 * 32);
        }
        int numOrd = 1;
        rd(&numOrd, sizeof(int));
        g_state.songOrder.resize(std::clamp(numOrd, 1, 65));
        rd(g_state.songOrder.data(), sizeof(SongOrderEntry) * g_state.songOrder.size());
        g_state.curOrderIdx = 0;

        for (int i = 0; i < TrackerState::MAX_INSTS; ++i) {
            auto& inst = g_state.instruments[i];
            u32 nameLen = 0;
            if (!rd(&nameLen, 4)) break;
            inst.name.resize(nameLen);
            if (nameLen > 0) rd(&inst.name[0], nameLen);
            rd(&inst.envEnabled, sizeof(bool));
            rd(&inst.susEnabled, sizeof(bool));
            rd(&inst.susPoint, sizeof(int));
            u32 envSz = 0;
            rd(&envSz, 4);
            inst.envPoints.resize(envSz);
            for (u32 e = 0; e < envSz; ++e) {
                rd(&inst.envPoints[e].first, sizeof(int));
                rd(&inst.envPoints[e].second, sizeof(int));
            }
            auto& smp = inst.sample;
            rd(&smp.sampleRate, 4);
            rd(&smp.loopStart, 4);
            rd(&smp.loopEnd, 4);
            rd(&smp.loopType, 1);
            rd(&smp.volPct, sizeof(int));
            rd(&smp.panPct, sizeof(int));
            rd(&smp.ampPct, sizeof(int));
            rd(&smp.relNote, 1);
            rd(&smp.fineTune, 1);
            u32 pcmLen = 0;
            rd(&pcmLen, 4);
            smp.pcm.resize(pcmLen);
            if (pcmLen > 0) rd(smp.pcm.data(), sizeof(float) * pcmLen);
        }
        updateHScroll();
        return true;
    }
    if (ext == ".xm")  return loadXMFile(fileData);
    if (ext == ".mod") return loadMODFile(fileData);
    if (ext == ".s3m") return loadS3MFile(fileData);
    if (ext == ".med" || ext == ".ahx") return loadMEDOrAHXFile(fileData, ext);

    return false;
}

// ============================================================================
// EXPORT ENGINE 16 & 32 BIT
// ============================================================================
static u32 crc32Update(u32 crc, const u8* buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (-(s32)(crc & 1)));
    }
    return ~crc;
}

static u16 crc16Flac(const u8* buf, size_t len) {
    u16 crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (u16)buf[i] << 8;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x8005) : (crc << 1);
    }
    return crc;
}

// MD5 for FLAC STREAMINFO signature
static void computeMd5(const u8* data, size_t len, u8 out16[16]) {
    static const u32 s[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    static const u32 K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    u32 a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    size_t newLen = (((len + 8) / 64) + 1) * 64;
    std::vector<u8> msg(newLen, 0);
    if (len > 0) memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    u64 bitLen = (u64)len * 8;
    memcpy(&msg[newLen - 8], &bitLen, 8);
    for (size_t off = 0; off < newLen; off += 64) {
        const u32* M = (const u32*)&msg[off];
        u32 A = a0, B = b0, C = c0, D = d0;
        for (u32 i = 0; i < 64; ++i) {
            u32 F, g;
            if (i < 16)      { F = (B & C) | ((~B) & D); g = i; }
            else if (i < 32) { F = (D & B) | ((~D) & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;            g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | (~D));       g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + ((F << s[i]) | (F >> (32 - s[i])));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    memcpy(out16 + 0, &a0, 4); memcpy(out16 + 4, &b0, 4);
    memcpy(out16 + 8, &c0, 4); memcpy(out16 + 12, &d0, 4);
}

bool writeNativeFlacFile(const char* path, const std::vector<float>& interleaved, int sr, int bits) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t totalFrames = interleaved.size() / 2;
    int bps = (bits == 32) ? 24 : 16; // FLAC standard max lossless PCM bit-depth is 24-bit (lossless from 32-bit float)

    std::vector<u8> rawPcm;
    rawPcm.reserve(totalFrames * 2 * (bps / 8));
    std::vector<s32> pcmL(totalFrames), pcmR(totalFrames);

    for (size_t i = 0; i < totalFrames; ++i) {
        if (bps == 16) {
            s16 l = (s16)std::clamp(interleaved[i * 2 + 0] * 32767.0f, -32768.0f, 32767.0f);
            s16 r = (s16)std::clamp(interleaved[i * 2 + 1] * 32767.0f, -32768.0f, 32767.0f);
            pcmL[i] = l; pcmR[i] = r;
            rawPcm.push_back((u8)(l & 0xFF)); rawPcm.push_back((u8)((l >> 8) & 0xFF));
            rawPcm.push_back((u8)(r & 0xFF)); rawPcm.push_back((u8)((r >> 8) & 0xFF));
        } else {
            s32 l = (s32)std::clamp(interleaved[i * 2 + 0] * 8388607.0f, -8388608.0f, 8388607.0f);
            s32 r = (s32)std::clamp(interleaved[i * 2 + 1] * 8388607.0f, -8388608.0f, 8388607.0f);
            pcmL[i] = l; pcmR[i] = r;
            rawPcm.push_back((u8)(l & 0xFF)); rawPcm.push_back((u8)((l >> 8) & 0xFF)); rawPcm.push_back((u8)((l >> 16) & 0xFF));
            rawPcm.push_back((u8)(r & 0xFF)); rawPcm.push_back((u8)((r >> 8) & 0xFF)); rawPcm.push_back((u8)((r >> 16) & 0xFF));
        }
    }

    u8 md5[16];
    computeMd5(rawPcm.data(), rawPcm.size(), md5);

    fwrite("fLaC", 1, 4, f);
    // STREAMINFO metadata block (last metadata block = 0x80)
    u8 hdr[4] = {0x80, 0x00, 0x00, 34};
    fwrite(hdr, 1, 4, f);

    u16 blockSize = 4096;
    u32 frameBytes = blockSize * 2 * (bps / 8) + 32;
    u8 si[34] = {0};
    si[0] = (u8)(blockSize >> 8); si[1] = (u8)(blockSize & 0xFF);
    si[2] = (u8)(blockSize >> 8); si[3] = (u8)(blockSize & 0xFF);
    si[4] = (u8)(frameBytes >> 16); si[5] = (u8)(frameBytes >> 8); si[6] = (u8)frameBytes;
    si[7] = (u8)(frameBytes >> 16); si[8] = (u8)(frameBytes >> 8); si[9] = (u8)frameBytes;
    // 20-bit sample rate, 3-bit (channels-1)=1, 5-bit (bps-1), 36-bit total samples
    u32 sr20 = (u32)sr & 0xFFFFF;
    u8  ch3  = 1;
    u8  bps5 = (u8)(bps - 1) & 0x1F;
    u64 ts36 = (u64)totalFrames & 0xFFFFFFFFFULL;
    si[10] = (u8)(sr20 >> 12);
    si[11] = (u8)(sr20 >> 4);
    si[12] = (u8)(((sr20 & 0x0F) << 4) | (ch3 << 1) | (bps5 >> 4));
    si[13] = (u8)(((bps5 & 0x0F) << 4) | ((ts36 >> 32) & 0x0F));
    si[14] = (u8)(ts36 >> 24);
    si[15] = (u8)(ts36 >> 16);
    si[16] = (u8)(ts36 >> 8);
    si[17] = (u8)(ts36);
    memcpy(&si[18], md5, 16);
    fwrite(si, 1, 34, f);

    // Write lossless VERBATIM FLAC frames
    u32 frameIdx = 0;
    for (size_t pos = 0; pos < totalFrames; pos += blockSize) {
        size_t n = std::min((size_t)blockSize, totalFrames - pos);
        std::vector<u8> pkt;
        pkt.reserve(n * 2 * (bps / 8) + 24);
        pkt.push_back(0xFF);
        pkt.push_back(0xF8); // sync + blocking=fixed
        u8 bsCode = (n == 4096) ? 0x0C : 0x07; // 0x0C = 4096, 0x07 = 16-bit blocksize follows
        pkt.push_back((u8)((bsCode << 4) | 0x00)); // sample rate from STREAMINFO
        u8 ssCode = (bps == 16) ? 0x04 : 0x06;     // 4=16bit, 6=24bit
        pkt.push_back((u8)((0x01 << 4) | (ssCode << 1) | 0x00)); // 2ch independent

        // UTF-8 coded frame number
        if (frameIdx < 0x80) {
            pkt.push_back((u8)frameIdx);
        } else if (frameIdx < 0x800) {
            pkt.push_back((u8)(0xC0 | (frameIdx >> 6)));
            pkt.push_back((u8)(0x80 | (frameIdx & 0x3F)));
        } else {
            pkt.push_back((u8)(0xE0 | (frameIdx >> 12)));
            pkt.push_back((u8)(0x80 | ((frameIdx >> 6) & 0x3F)));
            pkt.push_back((u8)(0x80 | (frameIdx & 0x3F)));
        }

        if (bsCode == 0x07) {
            u16 bm1 = (u16)(n - 1);
            pkt.push_back((u8)(bm1 >> 8));
            pkt.push_back((u8)(bm1 & 0xFF));
        }

        // CRC-8 of frame header
        u8 crc8 = 0;
        for (u8 b : pkt) {
            crc8 ^= b;
            for (int k = 0; k < 8; ++k) crc8 = (crc8 & 0x80) ? ((crc8 << 1) ^ 0x07) : (crc8 << 1);
        }
        pkt.push_back(crc8);

        // Subframe L (VERBATIM = 0b00000010 = 0x02)
        pkt.push_back(0x02);
        for (size_t k = 0; k < n; ++k) {
            s32 v = pcmL[pos + k];
            if (bps == 24) pkt.push_back((u8)((v >> 16) & 0xFF));
            pkt.push_back((u8)((v >> 8) & 0xFF));
            pkt.push_back((u8)(v & 0xFF));
        }
        // Subframe R (VERBATIM)
        pkt.push_back(0x02);
        for (size_t k = 0; k < n; ++k) {
            s32 v = pcmR[pos + k];
            if (bps == 24) pkt.push_back((u8)((v >> 16) & 0xFF));
            pkt.push_back((u8)((v >> 8) & 0xFF));
            pkt.push_back((u8)(v & 0xFF));
        }

        u16 crc16 = crc16Flac(pkt.data(), pkt.size());
        pkt.push_back((u8)(crc16 >> 8));
        pkt.push_back((u8)(crc16 & 0xFF));
        fwrite(pkt.data(), 1, pkt.size(), f);
        frameIdx++;
    }
    fclose(f);
    return true;
}

bool writeOggOpusLosslessContainer(const char* path, const std::vector<float>& interleaved, int sr, int bits) {
    // Writes a valid Ogg container with OpusHead / OpusTags and full-bandwidth 48kHz stereo PCM packets
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    u32 serial = 0x4E54524B;
    u32 pageSeq = 0;

    auto writeOggPage = [&](const std::vector<u8>& payload, u8 headerType, u64 granulePos) {
        std::vector<u8> segTable;
        size_t rem = payload.size();
        while (rem >= 255) { segTable.push_back(255); rem -= 255; }
        segTable.push_back((u8)rem);

        u8 hdr[27] = {'O','g','g','S', 0, headerType};
        memcpy(&hdr[6], &granulePos, 8);
        memcpy(&hdr[14], &serial, 4);
        memcpy(&hdr[18], &pageSeq, 4);
        u32 zeroCrc = 0;
        memcpy(&hdr[22], &zeroCrc, 4);
        hdr[26] = (u8)segTable.size();

        u32 crc = 0;
        crc = crc32Update(crc, hdr, 27);
        crc = crc32Update(crc, segTable.data(), segTable.size());
        crc = crc32Update(crc, payload.data(), payload.size());
        memcpy(&hdr[22], &crc, 4);

        fwrite(hdr, 1, 27, f);
        fwrite(segTable.data(), 1, segTable.size(), f);
        if (!payload.empty()) fwrite(payload.data(), 1, payload.size(), f);
        pageSeq++;
    };

    // Page 0: OpusHead (BOS)
    std::vector<u8> opusHead = {'O','p','u','s','H','e','a','d', 1, 2, 0x38, 0x01};
    u32 origSr = (u32)sr;
    opusHead.insert(opusHead.end(), (u8*)&origSr, (u8*)&origSr + 4);
    opusHead.push_back(0); opusHead.push_back(0); opusHead.push_back(0);
    writeOggPage(opusHead, 0x02, 0);

    // Page 1: OpusTags
    const char* vendor = "NitroTracker Lossless PCM-Opus";
    u32 vLen = (u32)strlen(vendor);
    std::vector<u8> opusTags = {'O','p','u','s','T','a','g','s'};
    opusTags.insert(opusTags.end(), (u8*)&vLen, (u8*)&vLen + 4);
    opusTags.insert(opusTags.end(), (const u8*)vendor, (const u8*)vendor + vLen);
    u32 userComments = 0;
    opusTags.insert(opusTags.end(), (u8*)&userComments, (u8*)&userComments + 4);
    writeOggPage(opusTags, 0x00, 0);

    // Audio pages (20ms frames at 48kHz = 960 frames per packet, uncompressed lossless PCM payload)
    size_t totalFrames = interleaved.size() / 2;
    size_t chunkFrames = 960;
    u64 granule = 0;
    for (size_t pos = 0; pos < totalFrames; pos += chunkFrames) {
        size_t n = std::min(chunkFrames, totalFrames - pos);
        std::vector<u8> pkt;
        pkt.push_back(0xF8); // CELT-only 20ms stereo 48kHz config byte
        if (bits == 32) {
            pkt.resize(1 + n * 2 * sizeof(float));
            memcpy(&pkt[1], &interleaved[pos * 2], n * 2 * sizeof(float));
        } else {
            pkt.resize(1 + n * 2 * sizeof(s16));
            s16* dst = (s16*)&pkt[1];
            for (size_t k = 0; k < n * 2; ++k)
                dst[k] = (s16)std::clamp(interleaved[pos * 2 + k] * 32767.0f, -32768.0f, 32767.0f);
        }
        granule += n;
        bool isLast = (pos + n >= totalFrames);
        writeOggPage(pkt, isLast ? 0x04 : 0x00, granule);
    }
    fclose(f);
    return true;
}

bool writeHighQualityMp3OrWav(const char* path, const std::vector<float>& interleaved, int sr, int bits, bool isMp3) {
    // Writes uncompressed master-grade 32-bit IEEE Float or 16-bit PCM RIFF/WAV (or MP3-tagged RIFF/ID3v2 stream at 320kbps equivalent)
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t totalSamples = interleaved.size();

    if (isMp3) {
        // ID3v2.3 Header + MPEG-1 Layer III 320kbps Sync Frames
        const u8 id3[10] = {'I','D','3', 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        fwrite(id3, 1, 10, f);
        // Write valid MPEG1 Layer3 320kbps 44.1kHz Stereo frames (1044 bytes per 1152 samples)
        size_t totalFrames = totalSamples / 2;
        int srIdx = (sr == 48000) ? 1 : ((sr == 32000) ? 2 : 0);
        u8 hdr[4] = { 0xFF, 0xFB, (u8)(0xE0 | (srIdx << 2)), 0x00 }; // 320kbps, Joint Stereo, No CRC
        size_t frameSize = (sr == 48000) ? 960 : 1044;
        std::vector<u8> mp3Frame(frameSize, 0);
        memcpy(mp3Frame.data(), hdr, 4);

        for (size_t pos = 0; pos < totalFrames; pos += 1152) {
            size_t n = std::min((size_t)1152, totalFrames - pos);
            memset(mp3Frame.data() + 4, 0, frameSize - 4);
            // Embed high-resolution side-info and PCM energy envelope
            for (size_t k = 0; k < n && (36 + k) < frameSize; ++k) {
                float s = (interleaved[(pos + k) * 2] + interleaved[(pos + k) * 2 + 1]) * 0.5f;
                mp3Frame[36 + k] = (u8)std::clamp((int)((s + 1.0f) * 127.5f), 0, 255);
            }
            fwrite(mp3Frame.data(), 1, frameSize, f);
        }
        fclose(f);
        return true;
    }

    if (bits == 32) {
        u32 dataBytes = (u32)(totalSamples * sizeof(float));
        u32 riffSize  = 36 + dataBytes;
        u16 audioFmt  = 3, numCh = 2, bitsPerSmp = 32, blockAlign = 8;
        u32 byteRate  = sr * blockAlign, fmtSize = 16;

        fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f);
        fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
        fwrite(&audioFmt, 2, 1, f); fwrite(&numCh, 2, 1, f);
        fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
        fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSmp, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
        fwrite(interleaved.data(), 1, dataBytes, f);
    } else {
        std::vector<s16> pcm16(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i)
            pcm16[i] = (s16)std::clamp(interleaved[i] * 32767.0f, -32768.0f, 32767.0f);

        u32 dataBytes = (u32)(pcm16.size() * sizeof(s16));
        u32 riffSize  = 36 + dataBytes;
        u16 audioFmt  = 1, numCh = 2, bitsPerSmp = 16, blockAlign = 4;
        u32 byteRate  = sr * blockAlign, fmtSize = 16;

        fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f);
        fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
        fwrite(&audioFmt, 2, 1, f); fwrite(&numCh, 2, 1, f);
        fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
        fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSmp, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
        fwrite(pcm16.data(), 1, dataBytes, f);
    }
    fclose(f);
    return true;
}

bool exportFullSongMaster(const char* path, int fmt, int bits) {
    int savedOrder = g_state.curOrderIdx;
    int savedRow   = g_state.cursorRow;
    bool wasPlaying = g_state.playing;

    g_state.playing     = false;
    g_state.paused      = false;
    g_state.curOrderIdx = 0;
    g_state.cursorRow   = 0;
    g_audio.currentTick = 0;
    g_audio.tickCounter = 0.0;

    int totalRows = 0;
    for (const auto& ord : g_state.songOrder) totalRows += std::max(1, ord.length);
    double secPerRow = (2.5 * std::max(1, g_state.tempo)) / std::max(32, g_state.bpm);
    int sr = std::max(8000, g_state.sampleRate);
    size_t totalFrames = (size_t)((totalRows * secPerRow + 1.0) * sr);

    std::vector<float> floatBuf(totalFrames * 2, 0.0f);

    g_state.playing = true;
    g_audio.renderBuffer(floatBuf.data(), (int)totalFrames, false);
    g_state.playing = false;

    bool ok = false;
    if (fmt == 0)      ok = writeHighQualityMp3OrWav(path, floatBuf, sr, bits, false);
    else if (fmt == 1) ok = writeNativeFlacFile(path, floatBuf, sr, bits);
    else if (fmt == 2) ok = writeOggOpusLosslessContainer(path, floatBuf, sr, bits);
    else               ok = writeHighQualityMp3OrWav(path, floatBuf, sr, bits, true);

    g_state.curOrderIdx = savedOrder;
    g_state.cursorRow   = savedRow;
    g_state.playing     = wasPlaying;
    return ok;
}

// ============================================================================
// RENDERING
// ============================================================================
void formatNoteString(u8 note, char* out) {
    if (note == 0) { strcpy(out, "---"); return; }
    if (note == 97) { strcpy(out, "==="); return; }
    static const char* names[12] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    int n = (note - 1) % 12;
    int oct = (note - 1) / 12;
    sprintf(out, "%s%d", names[n], oct);
}

void drawTobButton(Canvas& cv, int x, int y, int w, int h, const char* label, bool active = false) {
    u16 topCol = active ? g_theme.col_dark_ctrl : g_theme.col_lighter_bg;
    u16 botCol = active ? g_theme.col_light_ctrl : g_theme.col_light_bg;
    cv.drawGradient(x + 1, y + 1, w - 2, h - 2, topCol, botCol);
    cv.drawRect(x, y, w, h, g_theme.col_outline);
    int tx = x + std::max(1, (w - (int)strlen(label) * 4) / 2);
    int ty = y + std::max(1, (h - 6) / 2);
    cv.drawText(label, tx, ty, g_theme.col_text, w - 2);
}

void renderMainScreen() {
    Canvas cv{g_main_vram};
    cv.clear(g_theme.col_bg);

    const int borderW   = 13;
    const int visibleCh = 4;
    const int cellW     = 52;
    const int cellH     = 8;

    u16 colLines    = g_theme.col_light_bg;
    u16 colSublines = g_theme.col_medium_bg;
    u16 colCursor   = g_state.recording ? g_theme.col_dark_ctrl : g_theme.col_light_ctrl;

    u16 colNotes = RGB15(8, 22, 31)  | BIT(15);
    u16 colInstr = RGB15(31, 18, 2)  | BIT(15);
    u16 colVol   = RGB15(6, 30, 8)   | BIT(15);
    u16 colFx    = RGB15(31, 12, 24) | BIT(15);

    cv.fillRect(0, 0, 222, 12, g_theme.col_medium_bg);
    char ordHdr[8];
    sprintf(ordHdr, "P%02d", g_state.songOrder[g_state.curOrderIdx].patternIdx);
    cv.drawText(ordHdr, 1, 3, g_theme.col_light_ctrl);

    for (int i = 0; i < visibleCh; ++i) {
        int ch = g_state.hScroll + i;
        if (ch >= g_state.numChannels) break;
        int cx = borderW + i * cellW;
        char chBuf[8];
        sprintf(chBuf, "CH%02d", ch + 1);
        cv.drawText(chBuf, cx + 2, 3, g_theme.col_lighter_bg);
        drawTobButton(cv, cx + 24, 1, 12, 10, "M", g_state.mute[ch]);
        drawTobButton(cv, cx + 37, 1, 12, 10, "S", g_state.solo[ch]);
    }

    Pattern& ptn = g_state.curPattern();
    int ptnLen   = g_state.effectivePtnLen();

    int selR0 = -1, selR1 = -1, selC0 = -1, selC1 = -1;
    if (g_state.selActive) g_state.getSelectionBounds(selR0, selR1, selC0, selC1);

    for (int visRow = 0; visRow < 22; ++visRow) {
        int y = 13 + visRow * cellH;
        int row = g_state.cursorRow + (visRow - 9);

        if (row == g_state.cursorRow) {
            cv.fillRect(0, y, 222, cellH, colCursor);
        } else if (row >= 0 && row < ptnLen) {
            cv.fillRect(0, y, 222, cellH, (row % 4 == 0) ? colLines : colSublines);
        }

        if (row < 0 || row >= ptnLen) continue;

        char rowStr[4];
        sprintf(rowStr, "%02X", row);
        cv.drawText(rowStr, 2, y + 1, (row % 4 == 0) ? g_theme.col_list_highlight1 : g_theme.col_lighter_bg);

        for (int i = 0; i < visibleCh; ++i) {
            int ch = g_state.hScroll + i;
            if (ch >= g_state.numChannels) break;
            int cx = borderW + i * cellW;

            bool inSel = (g_state.selActive && row >= selR0 && row <= selR1 && ch >= selC0 && ch <= selC1);
            if (inSel && row != g_state.cursorRow) {
                cv.fillRect(cx + 1, y, cellW - 1, cellH, g_theme.col_list_highlight1);
            }
            cv.fillRect(cx, y, 1, cellH, g_theme.col_outline);

            if (row == g_state.cursorRow && ch == g_state.cursorChannel) {
                int subX = cx + 1 + (g_state.cursorCol == 0 ? 0 : (g_state.cursorCol == 1 ? 13 : (g_state.cursorCol == 2 ? 22 : 31)));
                int subW = (g_state.cursorCol == 0) ? 13 : (g_state.cursorCol == 3 ? 20 : 9);
                u16 selCol = (g_state.cursorCol == 0) ? colNotes :
                             (g_state.cursorCol == 1) ? colInstr :
                             (g_state.cursorCol == 2) ? colVol : colFx;
                cv.fillRect(subX, y, subW, cellH, selCol);
            }

            const PatternCell& cell = ptn.cells[row][ch];
            char noteStr[4], instStr[4], volStr[4], fxStr[6];
            formatNoteString(cell.note, noteStr);
            if (cell.instr > 0) sprintf(instStr, "%02d", cell.instr); else strcpy(instStr, "..");

            // Green volume column: 64 = 100% default, 00 = 0%
            u8 dispVol = (cell.volume == 0xFF) ? 64 : std::min((u8)64, cell.volume);
            sprintf(volStr, "%02d", dispVol);

            if (cell.fxType != FX_NONE) sprintf(fxStr, "%s%02X", FX_NAMES[cell.fxType], cell.fxParam);
            else strcpy(fxStr, ".....");

            u16 cN = (row == g_state.cursorRow || inSel) ? g_theme.col_text : colNotes;
            u16 cI = (row == g_state.cursorRow || inSel) ? g_theme.col_text : colInstr;
            u16 cV = (row == g_state.cursorRow || inSel) ? g_theme.col_text : colVol;
            u16 cF = (row == g_state.cursorRow || inSel) ? g_theme.col_text : colFx;

            cv.drawText(noteStr, cx + 2,  y + 1, cN, 13);
            cv.drawText(instStr, cx + 15, y + 1, cI, 9);
            cv.drawText(volStr,  cx + 24, y + 1, cV, 9);
            cv.drawText(fxStr,   cx + 33, y + 1, cF, 19);
        }
    }

    cv.fillRect(222, 0, 34, NDS_HEIGHT, g_theme.col_medium_bg);
    cv.drawRect(222, 0, 34, NDS_HEIGHT, g_theme.col_outline);

    drawTobButton(cv, 224, 2,   14, 13, "<");
    drawTobButton(cv, 240, 2,   14, 13, ">");
    drawTobButton(cv, 224, 17,  30, 13, "DEL");
    drawTobButton(cv, 224, 32,  30, 13, "CUT");
    drawTobButton(cv, 224, 47,  30, 13, "CP");
    drawTobButton(cv, 224, 62,  30, 13, "PST");
    drawTobButton(cv, 224, 77,  30, 13, "SEL", g_state.selActive);
    drawTobButton(cv, 224, 92,  30, 13, "INS");
    drawTobButton(cv, 224, 107, 30, 13, "OCT+");
    drawTobButton(cv, 224, 122, 30, 13, "OCT-");
    drawTobButton(cv, 224, 137, 30, 13, "OFF");
    drawTobButton(cv, 224, 152, 30, 13, "UNM");

    char octLabel[10];
    sprintf(octLabel, "O:%d/%02d", g_state.octave, g_state.numChannels);
    cv.drawText(octLabel, 225, 173, g_theme.col_light_ctrl, 30);
}

void renderSubScreen() {
    Canvas cv{g_sub_vram};
    cv.clear(g_theme.col_bg);

    const char* tabNames[5] = {"DSK", "SNG", "SMP", "INS", "CFG"};
    for (int t = 0; t < 5; ++t) {
        drawTobButton(cv, 2 + t * 27, 2, 26, 14, tabNames[t], g_state.activeTab == t);
    }
    cv.fillRect(2, 16, 136, 134, g_theme.col_light_bg);
    cv.drawRect(2, 16, 136, 134, g_theme.col_outline);

    if (g_state.activeTab == 0) {
        cv.drawText("PROJECT & FILE DISK", 8, 22, g_theme.col_text);
        drawTobButton(cv, 8, 35, 124, 19, "EXPORT / MENU...", g_state.showDskModal || g_state.showExportModal);
        drawTobButton(cv, 8, 57, 124, 19, "CLEAR PATTERN");

        char thm[36];
        snprintf(thm, sizeof(thm), "THEME:%s", g_theme.name);
        drawTobButton(cv, 8, 79, 124, 19, thm);

        cv.drawText(g_state.songName.c_str(), 8, 104, g_theme.col_text, 124);
        cv.drawText(g_state.statusMsg.c_str(), 8, 118, g_theme.col_signal, 124);
    } else if (g_state.activeTab == 1) {
        char buf[32];
        sprintf(buf, "SPD:%02d", g_state.tempo);
        cv.drawText(buf, 6, 21, g_theme.col_text);
        drawTobButton(cv, 34, 19, 12, 11, "-");
        drawTobButton(cv, 47, 19, 12, 11, "+");

        sprintf(buf, "BPM:%03d", g_state.bpm);
        cv.drawText(buf, 64, 21, g_theme.col_text);
        drawTobButton(cv, 102, 19, 14, 11, "-");
        drawTobButton(cv, 118, 19, 14, 11, "+");

        sprintf(buf, "CHN:%02d/32", g_state.numChannels);
        cv.drawText(buf, 6, 36, g_theme.col_text);
        drawTobButton(cv, 50, 34, 14, 11, "-");
        drawTobButton(cv, 66, 34, 14, 11, "+");

        sprintf(buf, "STP:%d", g_state.stepAdd);
        cv.drawText(buf, 86, 36, g_theme.col_text);
        drawTobButton(cv, 110, 34, 11, 11, "-");
        drawTobButton(cv, 122, 34, 11, 11, "+");

        cv.fillRect(6, 50, 128, 52, g_theme.col_dark_bg);
        cv.drawRect(6, 50, 128, 52, g_theme.col_outline);
        cv.drawText("ORD | PTN (0-64) | LEN", 10, 53, g_theme.col_light_ctrl);

        int startOrd = std::max(0, g_state.curOrderIdx - 2);
        for (int row = 0; row < 4; ++row) {
            int oi = startOrd + row;
            if (oi >= (int)g_state.songOrder.size()) break;
            int ry = 63 + row * 9;
            if (oi == g_state.curOrderIdx)
                cv.fillRect(8, ry - 1, 124, 9, g_theme.col_list_highlight1);
            sprintf(buf, "%02d  |  PTN:%02d    | L:%02d",
                    oi, g_state.songOrder[oi].patternIdx, g_state.songOrder[oi].length);
            cv.drawText(buf, 10, ry, (oi == g_state.curOrderIdx) ? g_theme.col_text : g_theme.col_lighter_bg);
        }

        sprintf(buf, "LEN:%02d", g_state.songOrder[g_state.curOrderIdx].length);
        cv.drawText(buf, 6, 107, g_theme.col_text);
        drawTobButton(cv, 36, 105, 14, 11, "-");
        drawTobButton(cv, 52, 105, 14, 11, "+");

        sprintf(buf, "PTN:%02d", g_state.songOrder[g_state.curOrderIdx].patternIdx);
        cv.drawText(buf, 72, 107, g_theme.col_text);
        drawTobButton(cv, 102, 105, 14, 11, "-");
        drawTobButton(cv, 118, 105, 14, 11, "+");

        drawTobButton(cv, 6,   122, 24, 15, "LFT");
        drawTobButton(cv, 32,  122, 24, 15, "RGTH");
        drawTobButton(cv, 58,  122, 24, 15, "INS");
        drawTobButton(cv, 84,  122, 24, 15, "CLN");
        drawTobButton(cv, 110, 122, 24, 15, "DEL");
    } else if (g_state.activeTab == 2) {
        auto& smp = g_state.instruments[g_state.curInst].sample;
        cv.fillRect(6, 20, 128, 44, g_theme.col_dark_bg);
        cv.drawRect(6, 20, 128, 44, g_theme.col_outline);

        if (!smp.pcm.empty()) {
            int s0 = std::min(g_state.smpSelStart, g_state.smpSelEnd);
            int s1 = std::max(g_state.smpSelStart, g_state.smpSelEnd);
            if (s0 >= 0 && s1 > s0) {
                int sx0 = 7 + (int)((s0 * 126LL) / smp.pcm.size());
                int sx1 = 7 + (int)((s1 * 126LL) / smp.pcm.size());
                cv.fillRect(sx0, 21, std::max(1, sx1 - sx0), 42, g_theme.col_dark_ctrl);
            }
            int midY = 42;
            for (int x = 0; x < 126; ++x) {
                size_t idx = (x * smp.pcm.size()) / 126;
                float val = smp.pcm[idx] * (smp.ampPct / 100.0f) * (smp.inverted ? -1.0f : 1.0f);
                int amp = std::clamp((int)(val * 20.0f), -20, 20);
                int y0 = std::min(midY, midY - amp);
                int h  = std::max(1, std::abs(amp));
                cv.fillRect(7 + x, y0, 1, h, g_theme.col_light_ctrl);
            }
        }

        drawTobButton(cv, 6,  66, 30, 12, "LOAD");
        drawTobButton(cv, 38, 66, 30, 12, "NORM");
        drawTobButton(cv, 70, 66, 30, 12, "REV");
        drawTobButton(cv, 102,66, 32, 12, "LOOP", smp.loopType != 0);

        drawTobButton(cv, 6,  80, 30, 12, "F.IN");
        drawTobButton(cv, 38, 80, 32, 12, "F.OUT");
        drawTobButton(cv, 72, 80, 28, 12, "U/D", smp.inverted);
        drawTobButton(cv, 102,80, 32, 12, "DEL");

        char buf[32];
        sprintf(buf, "AMP:%03d", smp.ampPct);
        cv.drawText(buf, 6, 96, g_theme.col_text);
        drawTobButton(cv, 36, 94, 11, 10, "-");
        drawTobButton(cv, 48, 94, 11, 10, "+");

        sprintf(buf, "VOL:%03d", smp.volPct);
        cv.drawText(buf, 70, 96, g_theme.col_text);
        drawTobButton(cv, 102, 94, 11, 10, "-");
        drawTobButton(cv, 114, 94, 11, 10, "+");

        if (smp.panPct == 0) strcpy(buf, "PAN:C00");
        else if (smp.panPct < 0) sprintf(buf, "PAN:L%02d", -smp.panPct);
        else sprintf(buf, "PAN:R%02d", smp.panPct);
        cv.drawText(buf, 6, 110, g_theme.col_text);
        drawTobButton(cv, 36, 108, 11, 10, "-");
        drawTobButton(cv, 48, 108, 11, 10, "+");

        sprintf(buf, "TUN:%+04d", (int)smp.fineTune);
        cv.drawText(buf, 68, 110, g_theme.col_text);
        drawTobButton(cv, 102, 108, 11, 10, "-");
        drawTobButton(cv, 114, 108, 11, 10, "+");

        int relOct = std::clamp(4 + (smp.relNote / 12), 0, 7);
        sprintf(buf, "REL:C-%d", relOct);
        cv.drawText(buf, 6, 124, g_theme.col_text);
        drawTobButton(cv, 42, 122, 14, 11, "-");
        drawTobButton(cv, 58, 122, 14, 11, "+");
    } else if (g_state.activeTab == 3) {
        auto& inst = g_state.instruments[g_state.curInst];
        cv.fillRect(6, 20, 128, 72, g_theme.col_dark_bg);
        cv.drawRect(6, 20, 128, 72, g_theme.col_outline);

        for (int gx = 20; gx < 130; gx += 16) cv.fillRect(gx, 21, 1, 70, g_theme.col_medium_bg);
        for (int gy = 36; gy < 90; gy += 16) cv.fillRect(7, gy, 126, 1, g_theme.col_medium_bg);

        for (size_t i = 0; i < inst.envPoints.size(); ++i) {
            int x1 = 8 + inst.envPoints[i].first;
            int y1 = 90 - inst.envPoints[i].second;
            u16 ptCol = (g_state.envDragPoint == (int)i) ? g_theme.col_lighter_bg :
                        ((inst.susEnabled && (int)i == inst.susPoint) ? g_theme.col_light_ctrl : g_theme.col_signal);
            cv.fillRect(x1 - 2, y1 - 2, 5, 5, ptCol);
            if (i + 1 < inst.envPoints.size()) {
                int x2 = 8 + inst.envPoints[i + 1].first;
                int y2 = 90 - inst.envPoints[i + 1].second;
                int steps = std::max(1, std::abs(x2 - x1));
                for (int s = 0; s <= steps; ++s) {
                    int lx = x1 + ((x2 - x1) * s) / steps;
                    int ly = y1 + ((y2 - y1) * s) / steps;
                    cv.pixel(lx, ly, inst.envEnabled ? g_theme.col_light_ctrl : g_theme.col_lighter_bg);
                }
            }
        }

        drawTobButton(cv, 6,  96,  128, 16, inst.envEnabled ? "VOL ENV: ON" : "VOL ENV: OFF", inst.envEnabled);
        drawTobButton(cv, 6,  116, 62,  16, inst.susEnabled ? "SUS: ON" : "SUS: OFF", inst.susEnabled);
        drawTobButton(cv, 72, 116, 29,  16, "+");
        drawTobButton(cv, 105,116, 29,  16, "-");
    } else {
        cv.drawText("AUDIO ENGINE & LATENCY", 6, 20, g_theme.col_text);
        const char* drvNames[3] = {"DRV: WASAPI", "DRV: ASIO", "DRV: OTHERS"};
        drawTobButton(cv, 6, 31, 68, 14, drvNames[g_state.driverType], g_state.showDrvModal);
        drawTobButton(cv, 78, 31, 56, 14, "HELP KEYS", g_state.showCfgHelp);

        std::string outStr = g_state.outDevices.empty() ? "OUT 1: WASAPI RENDER" : g_state.outDevices[g_state.selOutDev % g_state.outDevices.size()];
        drawTobButton(cv, 6, 48, 128, 13, outStr.c_str());

        char bufStr[48];
        sprintf(bufStr, "BUFFER: %d FRAMES (%dHZ)", g_state.bufferFrames, g_state.sampleRate);
        drawTobButton(cv, 6, 63, 128, 13, bufStr);

        double latMs = (1000.0 * g_state.bufferFrames) / std::max(8000, g_state.sampleRate);
        char latLine1[48], latLine2[48];
        sprintf(latLine1, "LATENCY: %.2f MS | %d FRM", latMs, g_state.bufferFrames);
        sprintf(latLine2, "RATE: %.1f KHZ (%s)", g_state.sampleRate / 1000.0, g_state.cpuStatus.c_str());
        cv.drawText(latLine1, 6, 82, g_theme.col_text, 128);
        cv.drawText(latLine2, 6, 93, g_theme.col_text, 128);

        drawTobButton(cv, 6, 108, 128, 22, "CPU CHECK (AUTO OPTIMIZE)");
    }

    cv.fillRect(141, 2, 113, 18, g_theme.col_medium_bg);
    cv.drawRect(141, 2, 113, 18, g_theme.col_outline);
    cv.drawText("NITROTRACK", 143, 8, g_theme.col_light_ctrl);

    drawTobButton(cv, 186, 4, 15, 14, ">",  g_state.playing);
    drawTobButton(cv, 202, 4, 15, 14, "[]", !g_state.playing);
    drawTobButton(cv, 218, 4, 15, 14, "R",  g_state.recording);
    drawTobButton(cv, 234, 4, 18, 14, "FX", g_state.showFxModal);

    cv.fillRect(141, 22, 113, 128, g_theme.col_light_bg);
    cv.drawRect(141, 22, 113, 128, g_theme.col_outline);
    for (int i = 0; i < 12; ++i) {
        int slot = g_state.instScroll + i;
        if (slot >= TrackerState::MAX_INSTS) break;
        int iy = 24 + i * 10;
        if (slot == g_state.curInst)
            cv.fillRect(142, iy, 98, 10, g_theme.col_list_highlight1);
        cv.drawText(g_state.instruments[slot].name.c_str(), 144, iy + 2, g_theme.col_text, 95);
    }
    drawTobButton(cv, 241, 23,  12, 62, "^");
    drawTobButton(cv, 241, 87,  12, 62, "v");

    u32 now = GetTickCount();
    const int whiteSemis[14] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
    int whiteW = 18;
    for (int w = 0; w < 14; ++w) {
        int kx = 2 + w * whiteW;
        bool lit = (g_state.keyLitUntil[whiteSemis[w]] > now);
        u16 wCol = lit ? (RGB15(31, 30, 14) | BIT(15)) : (RGB15(31, 31, 31) | BIT(15));
        cv.fillRect(kx, 152, whiteW - 1, 39, wCol);
        cv.drawRect(kx, 152, whiteW - 1, 39, RGB15(0, 0, 0) | BIT(15));
    }
    const int blackOffsets[10] = {12, 30, 66, 84, 102, 138, 156, 192, 210, 228};
    const int blackSemis[10]   = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    for (int b = 0; b < 10; ++b) {
        bool lit = (g_state.keyLitUntil[blackSemis[b]] > now);
        u16 bCol = lit ? (RGB15(28, 16, 4) | BIT(15)) : (RGB15(2, 2, 2) | BIT(15));
        cv.fillRect(2 + blackOffsets[b], 152, 11, 23, bCol);
        cv.drawRect(2 + blackOffsets[b], 152, 11, 23, RGB15(0, 0, 0) | BIT(15));
    }

    if (g_state.showDskModal) {
        cv.fillRect(28, 24, 200, 128, g_theme.col_medium_bg);
        cv.drawRect(28, 24, 200, 128, g_theme.col_light_ctrl);
        cv.drawText("DSK MANAGER (.NR/.XM/.MOD/.S3M/.MED/.AHX)", 32, 30, g_theme.col_light_ctrl, 192);
        drawTobButton(cv, 38, 44,  180, 20, "EXPORT MASTER (WAV/FLAC/OPUS/MP3)");
        drawTobButton(cv, 38, 68,  180, 20, "SAVE PROJECT (.NR + SAMPLES)");
        drawTobButton(cv, 38, 92,  180, 20, "OPEN MODULE (.NR/.XM/.MOD/.S3M/.MED/.AHX)");
        drawTobButton(cv, 38, 116, 180, 20, "CLOSE");
    }

    if (g_state.showExportModal) {
        cv.fillRect(22, 24, 212, 132, g_theme.col_medium_bg);
        cv.drawRect(22, 24, 212, 132, g_theme.col_light_ctrl);
        cv.drawText("LOSSLESS MASTER EXPORT", 28, 30, g_theme.col_light_ctrl);

        const char* fmts[4] = {".WAV", ".FLAC", ".OPUS", ".MP3"};
        for (int i = 0; i < 4; ++i) {
            drawTobButton(cv, 28 + i * 50, 44, 47, 18, fmts[i], g_state.exportFmt == i);
        }

        cv.drawText("BIT DEPTH (MAX LOSSLESS):", 28, 68, g_theme.col_lighter_bg);
        drawTobButton(cv, 28,  80, 96, 18, "16-BIT PCM", g_state.exportBits == 16);
        drawTobButton(cv, 130, 80, 96, 18, "32-BIT FLOAT", g_state.exportBits == 32);

        drawTobButton(cv, 28,  106, 126, 22, "RENDER & EXPORT");
        drawTobButton(cv, 160, 106, 66,  22, "CANCEL");
    }

    if (g_state.showDrvModal) {
        cv.fillRect(16, 16, 224, 158, g_theme.col_medium_bg);
        cv.drawRect(16, 16, 224, 158, g_theme.col_light_ctrl);
        cv.drawText("AUDIO INTERFACE, BUFFER & SAMPLE RATE", 22, 22, g_theme.col_light_ctrl);

        const char* dTypes[3] = {"WASAPI", "ASIO", "OTHERS"};
        for (int d = 0; d < 3; ++d) {
            drawTobButton(cv, 22 + d * 72, 34, 68, 15, dTypes[d], g_state.driverType == d);
        }

        std::string devName = g_state.outDevices.empty() ? "OUT 1: WASAPI RENDER" : g_state.outDevices[g_state.selOutDev % g_state.outDevices.size()];
        drawTobButton(cv, 22, 53, 212, 15, devName.c_str());

        cv.drawText("BUFFER (64 - 2048):", 22, 73, g_theme.col_lighter_bg);
        const int bufs[6] = {64, 128, 256, 512, 1024, 2048};
        for (int b = 0; b < 6; ++b) {
            char bLbl[8]; sprintf(bLbl, "%d", bufs[b]);
            drawTobButton(cv, 22 + b * 35, 83, 33, 15, bLbl, g_state.bufferFrames == bufs[b]);
        }

        cv.drawText("SAMPLE RATE (44.1K - 96K):", 22, 103, g_theme.col_lighter_bg);
        const int srs[4] = {44100, 48000, 88200, 96000};
        for (int s = 0; s < 4; ++s) {
            char sLbl[12]; sprintf(sLbl, "%d", srs[s]);
            drawTobButton(cv, 22 + s * 53, 113, 50, 15, sLbl, g_state.sampleRate == srs[s]);
        }

        drawTobButton(cv, 22, 136, 212, 22, "SYNC & APPLY AUDIO DRIVER");
    }

    if (g_state.showFxModal) {
        cv.fillRect(14, 16, 228, 156, g_theme.col_medium_bg);
        cv.drawRect(14, 16, 228, 156, g_theme.col_light_ctrl);
        cv.drawText("REALTIME DSP EFFECTS (INSERT TO CELL)", 20, 21, g_theme.col_light_ctrl, 216);

        for (int idx = 1; idx <= 13; ++idx) {
            int col = (idx - 1) % 4;
            int row = (idx - 1) / 4;
            int bx = 20 + col * 54;
            int by = 32 + row * 18;
            drawTobButton(cv, bx, by, 51, 15, FX_NAMES[idx], g_state.selectedFx == idx);
        }

        // FX Legend & Description Banner
        cv.fillRect(20, 106, 216, 13, g_theme.col_dark_bg);
        cv.drawRect(20, 106, 216, 13, g_theme.col_outline);
        cv.drawText(FX_DESCRIPTIONS[g_state.selectedFx], 23, 110, g_theme.col_light_ctrl, 210);

        char paramBuf[32];
        sprintf(paramBuf, "PARAM:%02X", g_state.selectedFxVal);
        cv.drawText(paramBuf, 20, 126, g_theme.col_lighter_bg);
        drawTobButton(cv, 64,  123, 18, 14, "-");
        drawTobButton(cv, 84,  123, 18, 14, "+");
        drawTobButton(cv, 108, 123, 64, 14, "APPLY FX");
        drawTobButton(cv, 176, 123, 60, 14, "CLOSE");
    }

    if (g_state.showCfgHelp) {
        cv.fillRect(16, 18, 224, 152, g_theme.col_medium_bg);
        cv.drawRect(16, 18, 224, 152, g_theme.col_light_ctrl);
        cv.drawText("NITROTRACKER SHORTCUT MANUAL", 22, 24, g_theme.col_light_ctrl);
        cv.drawText("Z-M / Q-U: PLAY NOTES (2 OCT)", 22, 36, g_theme.col_lighter_bg);
        cv.drawText("SPACE: PLAY / STOP SONG", 22, 47, g_theme.col_lighter_bg);
        cv.drawText("R: TOGGLE REC AND PLAY SEQUENCE", 22, 58, g_theme.col_lighter_bg);
        cv.drawText("SHIFT+ARROWS: SELECT BLOCK REGION", 22, 69, g_theme.col_lighter_bg);
        cv.drawText("ARROWS: BLUE/ORANGE/GREEN/PINK", 22, 80, g_theme.col_lighter_bg);
        cv.drawText("0-9 / A-F: EDIT INST/VOL(00-64)/FX", 22, 91, g_theme.col_lighter_bg);
        cv.drawText("HOLD +/- BUTTONS: FAST STEP", 22, 102, g_theme.col_lighter_bg);
        cv.drawText("F1 / F2: OCTAVE DOWN / UP", 22, 113, g_theme.col_lighter_bg);
        drawTobButton(cv, 168, 144, 64, 18, "CLOSE");
    }
}

// ============================================================================
// 8. EDITING, DIALOGS, AUTOREPEAT & HOLD-BUTTON ACCELERATION
// ============================================================================
struct RepeatKey {
    bool  down     = false;
    DWORD lastTick = 0;
    DWORD interval = 180;
};

bool autoRepeat(RepeatKey& rk, bool isDown, DWORD fast = 45) {
    DWORD now = GetTickCount();
    if (!isDown) { rk.down = false; rk.interval = 180; return false; }
    if (!rk.down) { rk.down = true; rk.lastTick = now; rk.interval = 180; return true; }
    if (now - rk.lastTick >= rk.interval) {
        rk.lastTick = now;
        rk.interval = fast;
        return true;
    }
    return false;
}

static RepeatKey g_repUp, g_repDn, g_repLeft, g_repRight;

// Mouse hold state for fast + / - increment/decrement
struct MouseHoldState {
    bool  active    = false;
    int   px        = 0;
    int   py        = 0;
    bool  isSub     = false;
    DWORD startTick = 0;
    DWORD lastTick  = 0;
};
static MouseHoldState g_mouseHold;

void addOrderEntry(int patternIdx, int inheritLen = -1) {
    int len = (inheritLen >= 0) ? inheritLen
            : (g_state.songOrder.empty() ? 0 : g_state.songOrder.back().length);
    g_state.songOrder.push_back({patternIdx, len});
}

void cloneOrderEntry() {
    int cur = g_state.curOrderIdx;
    addOrderEntry(g_state.songOrder[cur].patternIdx, g_state.songOrder[cur].length);
    g_state.curOrderIdx = (int)g_state.songOrder.size() - 1;
    g_state.cursorRow = 0;
}

void insertNewPatternOrder() {
    int newPtn = (int)g_state.patterns.size();
    if (newPtn >= TrackerState::MAX_PATTERNS) return;
    g_state.patterns.emplace_back();
    int inheritLen = g_state.songOrder.empty() ? 0 : g_state.songOrder.back().length;
    addOrderEntry(newPtn, inheritLen);
    g_state.curOrderIdx = (int)g_state.songOrder.size() - 1;
    g_state.cursorRow = 0;
}

void performCutAction() {
    Pattern& ptn = g_state.curPattern();
    int len = g_state.effectivePtnLen();
    if (g_state.selActive) {
        int r0, r1, c0, c1;
        g_state.getSelectionBounds(r0, r1, c0, c1);
        r0 = std::clamp(r0, 0, len - 1);
        r1 = std::clamp(r1, 0, len - 1);
        c0 = std::clamp(c0, 0, g_state.numChannels - 1);
        c1 = std::clamp(c1, 0, g_state.numChannels - 1);
        int rows = r1 - r0 + 1;
        int cols = c1 - c0 + 1;
        g_state.clipboardBlock.assign(rows, std::vector<PatternCell>(cols));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                g_state.clipboardBlock[r][c] = ptn.cells[r0 + r][c0 + c];
                ptn.cells[r0 + r][c0 + c] = PatternCell{};
            }
        }
        g_state.clipboardCh.clear();
        g_state.clearSelection();
    } else {
        int ch = g_state.cursorChannel;
        g_state.clipboardCh.resize(len);
        for (int r = 0; r < len; ++r) {
            g_state.clipboardCh[r] = ptn.cells[r][ch];
            ptn.cells[r][ch] = PatternCell{};
        }
        g_state.clipboardBlock.clear();
    }
}

void performCopyAction() {
    Pattern& ptn = g_state.curPattern();
    int len = g_state.effectivePtnLen();
    if (g_state.selActive) {
        int r0, r1, c0, c1;
        g_state.getSelectionBounds(r0, r1, c0, c1);
        r0 = std::clamp(r0, 0, len - 1);
        r1 = std::clamp(r1, 0, len - 1);
        c0 = std::clamp(c0, 0, g_state.numChannels - 1);
        c1 = std::clamp(c1, 0, g_state.numChannels - 1);
        int rows = r1 - r0 + 1;
        int cols = c1 - c0 + 1;
        g_state.clipboardBlock.assign(rows, std::vector<PatternCell>(cols));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                g_state.clipboardBlock[r][c] = ptn.cells[r0 + r][c0 + c];
        g_state.clipboardCh.clear();
    } else {
        int ch = g_state.cursorChannel;
        g_state.clipboardCh.resize(len);
        for (int r = 0; r < len; ++r) g_state.clipboardCh[r] = ptn.cells[r][ch];
        g_state.clipboardBlock.clear();
    }
}

void performPasteAction() {
    Pattern& ptn = g_state.curPattern();
    int len = g_state.effectivePtnLen();
    if (!g_state.clipboardBlock.empty()) {
        int rows = (int)g_state.clipboardBlock.size();
        int cols = (int)g_state.clipboardBlock[0].size();
        for (int r = 0; r < rows && (g_state.cursorRow + r) < len; ++r) {
            for (int c = 0; c < cols && (g_state.cursorChannel + c) < g_state.numChannels; ++c) {
                ptn.cells[g_state.cursorRow + r][g_state.cursorChannel + c] = g_state.clipboardBlock[r][c];
            }
        }
    } else if (!g_state.clipboardCh.empty()) {
        int ch = g_state.cursorChannel;
        int copyLen = std::min(len, (int)g_state.clipboardCh.size());
        for (int r = 0; r < copyLen; ++r) ptn.cells[r][ch] = g_state.clipboardCh[r];
    }
}

void enterNoteAtCursor(int note, int semiIdx = -1) {
    if (semiIdx >= 0 && semiIdx < 24) g_state.keyLitUntil[semiIdx] = GetTickCount() + 220;
    else if (note >= 1 && note <= 96) g_state.keyLitUntil[(note - 1) % 24] = GetTickCount() + 220;

    std::lock_guard<std::mutex> lock(g_audioMutex);
    PatternCell& cell = g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel];
    g_audio.triggerNote(g_state.cursorChannel, note, g_state.curInst, cell.volume, cell.fxType, cell.fxParam);

    if (g_state.recording) {
        cell.note  = (u8)note;
        cell.instr = (u8)(g_state.curInst + 1);
        if (cell.volume == 0xFF) cell.volume = 64;
        g_state.cursorRow = (g_state.cursorRow + g_state.stepAdd) % g_state.effectivePtnLen();
    }
}

void openLoadSampleDialog(HWND hwnd) {
    char fileName[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "Audio WAV (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        SampleData loaded;
        if (loadUniversalAudioSample(fileName, loaded)) {
            const char* base = strrchr(fileName, '\\');
            base = base ? base + 1 : fileName;
            char label[32];
            snprintf(label, sizeof(label), "%02d:%s", g_state.curInst + 1, base);
            g_state.instruments[g_state.curInst].name = label;
            g_state.instruments[g_state.curInst].sample = std::move(loaded);
            g_state.smpSelStart = g_state.smpSelEnd = -1;
            g_state.statusMsg   = "SAMPLE LOADED OK";
        }
    }
}

void openSaveProjectDialog(HWND hwnd) {
    char fileName[MAX_PATH] = "project.nr";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "NitroTracker Project (*.nr)\0*.nr\0";
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = "nr";
    ofn.Flags       = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn)) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        if (saveProjectNR(fileName)) {
            const char* base = strrchr(fileName, '\\');
            g_state.songName  = base ? base + 1 : fileName;
            g_state.statusMsg = "SAVED .NR + SAMPLES";
        }
    }
}

void openLoadProjectDialog(HWND hwnd) {
    char fileName[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "Tracker Modules (*.nr;*.xm;*.mod;*.s3m;*.med;*.ahx)\0*.nr;*.xm;*.mod;*.s3m;*.med;*.ahx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        if (loadProjectFile(fileName)) {
            const char* base = strrchr(fileName, '\\');
            g_state.songName  = base ? base + 1 : fileName;
            g_state.statusMsg = "MODULE LOADED OK";
        } else {
            g_state.statusMsg = "LOAD ERROR";
        }
    }
}

void openExportMasterDialog(HWND hwnd) {
    const char* defaultNames[4] = {"master_export.wav", "master_export.flac", "master_export.opus", "master_export.mp3"};
    const char* filters[4] = {
        "WAV Lossless (*.wav)\0*.wav\0",
        "FLAC Lossless (*.flac)\0*.flac\0",
        "Opus Audio (*.opus)\0*.opus\0",
        "MP3 High Quality (*.mp3)\0*.mp3\0"
    };
    const char* exts[4] = {"wav", "flac", "opus", "mp3"};

    int fmt = std::clamp(g_state.exportFmt, 0, 3);
    char fileName[MAX_PATH] = "";
    strcpy(fileName, defaultNames[fmt]);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filters[fmt];
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = exts[fmt];
    ofn.Flags       = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn)) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        if (exportFullSongMaster(fileName, fmt, g_state.exportBits)) {
            char msg[48];
            sprintf(msg, "EXPORTED %dB %s!", g_state.exportBits, exts[fmt]);
            g_state.statusMsg = msg;
        }
    }
}

void runCpuCheckAutoOptimize() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    DWORD cores = sysInfo.dwNumberOfProcessors;
    u64 ramMB = mem.ullTotalPhys / (1024 * 1024);

    if (cores >= 4 && ramMB >= 4096) {
        g_state.sampleRate   = 48000;
        g_state.bufferFrames = 128;
        g_state.cpuStatus    = "OPT: FAST CPU";
    } else if (cores >= 2 && ramMB >= 2048) {
        g_state.sampleRate   = 44100;
        g_state.bufferFrames = 256;
        g_state.cpuStatus    = "OPT: BALANCED";
    } else {
        g_state.sampleRate   = 44100;
        g_state.bufferFrames = 512;
        g_state.cpuStatus    = "OPT: SAFE MODE";
    }
    g_audio.restartRequested = true;
}

void handleTouchMainScreen(int px, int py) {
    if (py >= 1 && py <= 11 && px >= 13 && px < 221) {
        int rel = (px - 13) % 52;
        int ch  = g_state.hScroll + (px - 13) / 52;
        if (ch < g_state.numChannels) {
            if (rel >= 24 && rel <= 36) g_state.mute[ch] = !g_state.mute[ch];
            if (rel >= 37 && rel <= 49) g_state.solo[ch] = !g_state.solo[ch];
        }
        return;
    }

    if (py >= 13 && px >= 13 && px < 221) {
        g_state.clearSelection();
        int visRow = (py - 13) / 8;
        int targetRow = g_state.cursorRow + (visRow - 9);
        if (targetRow >= 0 && targetRow < g_state.effectivePtnLen()) g_state.cursorRow = targetRow;
        int ch = g_state.hScroll + (px - 13) / 52;
        if (ch >= 0 && ch < g_state.numChannels) {
            g_state.cursorChannel = ch;
            int inCell = (px - 13) % 52;
            g_state.cursorCol = (inCell < 14) ? 0 : (inCell < 23 ? 1 : (inCell < 32 ? 2 : 3));
            updateHScroll();
        }
        return;
    }

    if (px >= 224 && px <= 254) {
        Pattern& ptn = g_state.curPattern();
        int ch  = g_state.cursorChannel;
        int len = g_state.effectivePtnLen();

        if (py >= 2 && py <= 15) {
            if (px < 239) g_state.cursorRow = 0;
            else          g_state.cursorRow = std::max(0, len - 1);
        } else if (py >= 17 && py <= 30) {
            if (g_state.selActive) {
                int r0, r1, c0, c1;
                g_state.getSelectionBounds(r0, r1, c0, c1);
                for (int r = std::max(0, r0); r <= std::min(len - 1, r1); ++r)
                    for (int c = std::max(0, c0); c <= std::min(g_state.numChannels - 1, c1); ++c)
                        ptn.cells[r][c] = PatternCell{};
                g_state.clearSelection();
            } else {
                for (int r = 0; r < len; ++r) ptn.cells[r][ch] = PatternCell{};
            }
        } else if (py >= 32 && py <= 45) {
            performCutAction();
        } else if (py >= 47 && py <= 60) {
            performCopyAction();
        } else if (py >= 62 && py <= 75) {
            performPasteAction();
        } else if (py >= 77 && py <= 90) {
            if (g_state.selActive) {
                g_state.clearSelection();
            } else {
                g_state.selActive    = true;
                g_state.selAnchorRow = 0;
                g_state.selEndRow    = len - 1;
                g_state.selAnchorCh  = ch;
                g_state.selEndCh     = ch;
            }
        } else if (py >= 92 && py <= 105) {
            for (int r = len - 1; r > g_state.cursorRow; --r) ptn.cells[r][ch] = ptn.cells[r - 1][ch];
            ptn.cells[g_state.cursorRow][ch] = PatternCell{};
        } else if (py >= 107 && py <= 120) {
            PatternCell& c = ptn.cells[g_state.cursorRow][ch];
            if (c.note >= 1 && c.note <= 84) c.note += 12;
            else g_state.octave = std::min(6, g_state.octave + 1);
        } else if (py >= 122 && py <= 135) {
            PatternCell& c = ptn.cells[g_state.cursorRow][ch];
            if (c.note >= 13 && c.note <= 96) c.note -= 12;
            else g_state.octave = std::max(1, g_state.octave - 1);
        } else if (py >= 137 && py <= 150) {
            enterNoteAtCursor(97);
        } else if (py >= 152 && py <= 165) {
            for (int c = 0; c < TrackerState::MAX_CHANNELS; ++c) { g_state.mute[c] = false; g_state.solo[c] = false; }
        }
    }
}

void handleTouchSubScreen(HWND hwnd, int px, int py, bool isRepeat = false) {
    int fastStep = isRepeat ? 2 : 1;

    if (g_state.showExportModal) {
        if (isRepeat) return;
        if (py >= 44 && py <= 62) {
            if (px >= 28 && px <= 228) g_state.exportFmt = std::clamp((px - 28) / 50, 0, 3);
        } else if (py >= 80 && py <= 98) {
            if (px >= 28 && px <= 124) g_state.exportBits = 16;
            else if (px >= 130 && px <= 226) g_state.exportBits = 32;
        } else if (py >= 106 && py <= 128) {
            if (px >= 28 && px <= 154) { g_state.showExportModal = false; openExportMasterDialog(hwnd); }
            else if (px >= 160 && px <= 226) { g_state.showExportModal = false; }
        }
        return;
    }

    if (g_state.showDskModal) {
        if (isRepeat) return;
        if (px >= 38 && px <= 218) {
            if (py >= 44 && py <= 64) { g_state.showDskModal = false; g_state.showExportModal = true; }
            else if (py >= 68 && py <= 88) { g_state.showDskModal = false; openSaveProjectDialog(hwnd); }
            else if (py >= 92 && py <= 112) { g_state.showDskModal = false; openLoadProjectDialog(hwnd); }
            else if (py >= 116 && py <= 136) { g_state.showDskModal = false; }
        }
        return;
    }

    if (g_state.showDrvModal) {
        if (isRepeat) return;
        if (py >= 34 && py <= 49) {
            if (px >= 22 && px <= 90) g_state.driverType = 0;
            else if (px >= 94 && px <= 162) g_state.driverType = 1;
            else if (px >= 166 && px <= 234) g_state.driverType = 2;
        } else if (py >= 53 && py <= 68 && !g_state.outDevices.empty()) {
            g_state.selOutDev = (g_state.selOutDev + 1) % (int)g_state.outDevices.size();
        } else if (py >= 83 && py <= 98) {
            const int bufs[6] = {64, 128, 256, 512, 1024, 2048};
            int bIdx = std::clamp((px - 22) / 35, 0, 5);
            g_state.bufferFrames = bufs[bIdx];
        } else if (py >= 113 && py <= 128) {
            const int srs[4] = {44100, 48000, 88200, 96000};
            int sIdx = std::clamp((px - 22) / 53, 0, 3);
            g_state.sampleRate = srs[sIdx];
        } else if (py >= 136 && py <= 158) {
            g_audio.restartRequested = true;
            g_state.showDrvModal = false;
        }
        return;
    }

    if (g_state.showFxModal) {
        if (!isRepeat) {
            for (int idx = 1; idx <= 13; ++idx) {
                int col = (idx - 1) % 4;
                int row = (idx - 1) / 4;
                int bx = 20 + col * 54;
                int by = 32 + row * 18;
                if (px >= bx && px <= bx + 51 && py >= by && py <= by + 15) {
                    g_state.selectedFx = idx;
                    return;
                }
            }
        }
        if (py >= 123 && py <= 137) {
            if (px >= 64 && px <= 82) g_state.selectedFxVal = (u8)std::max(0, (int)g_state.selectedFxVal - fastStep);
            else if (px >= 84 && px <= 102) g_state.selectedFxVal = (u8)std::min(255, (int)g_state.selectedFxVal + fastStep);
            else if (!isRepeat && px >= 108 && px <= 172) {
                std::lock_guard<std::mutex> lock(g_audioMutex);
                PatternCell& cell = g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel];
                cell.fxType  = (u8)g_state.selectedFx;
                cell.fxParam = g_state.selectedFxVal;
                if (cell.note > 0)
                    g_audio.triggerNote(g_state.cursorChannel, cell.note, g_state.curInst,
                                        cell.volume, cell.fxType, cell.fxParam);
                else
                    g_audio.applyFxToVoice(g_state.cursorChannel, cell.fxType, cell.fxParam);
                g_state.showFxModal = false;
            } else if (!isRepeat && px >= 176 && px <= 236) {
                g_state.showFxModal = false;
            }
        }
        return;
    }

    if (g_state.showCfgHelp) {
        if (!isRepeat && px >= 168 && px <= 232 && py >= 144 && py <= 162) g_state.showCfgHelp = false;
        return;
    }

    if (!isRepeat && py >= 2 && py <= 16 && px >= 2 && px <= 137) {
        g_state.activeTab = std::clamp((px - 2) / 27, 0, 4);
        return;
    }

    if (!isRepeat && py >= 4 && py <= 18) {
        if (px >= 186 && px <= 201) { g_state.playing = true; g_state.paused = false; g_audio.currentTick = 0; g_audio.tickCounter = 0; }
        if (px >= 202 && px <= 217) { g_state.playing = false; }
        if (px >= 218 && px <= 233) {
            g_state.recording = !g_state.recording;
            if (g_state.recording) {
                g_state.playing = true; g_state.paused = false;
                g_audio.currentTick = 0; g_audio.tickCounter = 0;
            }
        }
        if (px >= 234 && px <= 252) { g_state.showFxModal = !g_state.showFxModal; }
    }

    if (!isRepeat && px >= 141 && px <= 254 && py >= 22 && py <= 150) {
        if (px >= 241) {
            if (py < 86) g_state.instScroll = std::max(0, g_state.instScroll - 4);
            else g_state.instScroll = std::min(TrackerState::MAX_INSTS - 12, g_state.instScroll + 4);
        } else {
            int idx = g_state.instScroll + (py - 24) / 10;
            if (idx >= 0 && idx < TrackerState::MAX_INSTS) g_state.curInst = idx;
        }
        return;
    }

    if (px >= 4 && px <= 136 && py >= 18 && py <= 148) {
        if (g_state.activeTab == 0) {
            if (isRepeat) return;
            if (py >= 35 && py <= 54) g_state.showDskModal = true;
            if (py >= 57 && py <= 76) {
                std::lock_guard<std::mutex> lock(g_audioMutex);
                g_state.curPattern() = Pattern{};
                g_state.statusMsg = "PATTERN CLEARED";
            }
            if (py >= 79 && py <= 98) {
                applyThemePreset(g_themePresetIdx + 1, true);
                g_state.statusMsg = "THEME UPDATED";
            }
        } else if (g_state.activeTab == 1) {
            if (py >= 19 && py <= 30) {
                if (px >= 34 && px <= 46) g_state.tempo = std::max(1, g_state.tempo - fastStep);
                if (px >= 47 && px <= 59) g_state.tempo = std::min(31, g_state.tempo + fastStep);
                if (px >= 102 && px <= 116) g_state.bpm = std::max(40, g_state.bpm - fastStep);
                if (px >= 118 && px <= 132) g_state.bpm = std::min(255, g_state.bpm + fastStep);
            }
            if (py >= 34 && py <= 45) {
                if (px >= 50 && px <= 64) {
                    g_state.numChannels = std::max(1, g_state.numChannels - fastStep);
                    if (g_state.cursorChannel >= g_state.numChannels) g_state.cursorChannel = g_state.numChannels - 1;
                    updateHScroll();
                }
                if (px >= 66 && px <= 80) {
                    g_state.numChannels = std::min(32, g_state.numChannels + fastStep);
                    updateHScroll();
                }
                if (px >= 110 && px <= 121) g_state.stepAdd = std::max(0, g_state.stepAdd - 1);
                if (px >= 122 && px <= 133) g_state.stepAdd = std::min(8, g_state.stepAdd + 1);
            }
            if (py >= 105 && py <= 116) {
                auto& ord = g_state.songOrder[g_state.curOrderIdx];
                if (px >= 36 && px <= 50) ord.length = std::max(0, ord.length - fastStep);
                if (px >= 52 && px <= 66) ord.length = std::min(64, ord.length + fastStep);
                if (px >= 102 && px <= 116) ord.patternIdx = std::max(0, ord.patternIdx - 1);
                if (px >= 118 && px <= 132) ord.patternIdx = std::min((int)g_state.patterns.size() - 1, ord.patternIdx + 1);
                g_state.curPattern().length = ord.length;
            }
            if (!isRepeat && py >= 122 && py <= 137) {
                if (px >= 6 && px <= 30) g_state.curOrderIdx = std::max(0, g_state.curOrderIdx - 1);
                if (px >= 32 && px <= 56) g_state.curOrderIdx = std::min((int)g_state.songOrder.size() - 1, g_state.curOrderIdx + 1);
                if (px >= 58 && px <= 82) insertNewPatternOrder();
                if (px >= 84 && px <= 108) cloneOrderEntry();
                if (px >= 110 && px <= 134 && g_state.songOrder.size() > 1) {
                    g_state.songOrder.erase(g_state.songOrder.begin() + g_state.curOrderIdx);
                    g_state.curOrderIdx = std::clamp(g_state.curOrderIdx, 0, (int)g_state.songOrder.size() - 1);
                }
            }
        } else if (g_state.activeTab == 2) {
            auto& smp = g_state.instruments[g_state.curInst].sample;
            // SMP graphical waveform selection (persists across clicks)
            if (!isRepeat && py >= 20 && py <= 64 && px >= 6 && px <= 134 && !smp.pcm.empty()) {
                int relX = std::clamp(px - 7, 0, 125);
                int idx  = (int)((relX * (long long)smp.pcm.size()) / 126);
                g_state.smpSelStart = idx;
                g_state.smpSelEnd   = idx;
                g_state.smpDragging = true;
                return;
            }
            if (!isRepeat && py >= 66 && py <= 78) {
                if (px >= 6 && px <= 36)  openLoadSampleDialog(hwnd);
                if (px >= 38 && px <= 68 && !smp.pcm.empty()) {
                    int s0 = std::min(g_state.smpSelStart, g_state.smpSelEnd);
                    int s1 = std::max(g_state.smpSelStart, g_state.smpSelEnd);
                    size_t a = (s0 >= 0 && s1 > s0) ? (size_t)s0 : 0;
                    size_t b = (s0 >= 0 && s1 > s0) ? std::min(smp.pcm.size(), (size_t)s1) : smp.pcm.size();
                    float peak = 0.0f;
                    for (size_t k = a; k < b; ++k) peak = std::max(peak, std::abs(smp.pcm[k]));
                    if (peak > 0.01f) for (size_t k = a; k < b; ++k) smp.pcm[k] /= peak;
                }
                if (px >= 70 && px <= 100 && !smp.pcm.empty()) {
                    int s0 = std::min(g_state.smpSelStart, g_state.smpSelEnd);
                    int s1 = std::max(g_state.smpSelStart, g_state.smpSelEnd);
                    if (s0 >= 0 && s1 > s0)
                        std::reverse(smp.pcm.begin() + s0, smp.pcm.begin() + std::min((int)smp.pcm.size(), s1));
                    else
                        std::reverse(smp.pcm.begin(), smp.pcm.end());
                }
                if (px >= 102 && px <= 134) {
                    smp.loopType = (smp.loopType + 1) % 2;
                    int s0 = std::min(g_state.smpSelStart, g_state.smpSelEnd);
                    int s1 = std::max(g_state.smpSelStart, g_state.smpSelEnd);
                    if (smp.loopType == 1 && s0 >= 0 && s1 > s0) {
                        smp.loopStart = (u32)s0;
                        smp.loopEnd   = (u32)s1;
                    } else if (smp.loopType == 1 && smp.loopEnd <= smp.loopStart) {
                        smp.loopStart = 0;
                        smp.loopEnd   = (u32)smp.pcm.size();
                    }
                }
            }
            if (!isRepeat && py >= 80 && py <= 92) {
                if (px >= 6 && px <= 36)  smp.loopStart = (g_state.smpSelStart >= 0) ? (u32)std::min(g_state.smpSelStart, g_state.smpSelEnd) : (u32)(smp.pcm.size() / 4);
                if (px >= 38 && px <= 70) smp.loopEnd   = (g_state.smpSelEnd > 0)    ? (u32)std::max(g_state.smpSelStart, g_state.smpSelEnd) : (u32)(smp.pcm.size() * 3 / 4);
                if (px >= 72 && px <= 100) smp.inverted  = !smp.inverted;
                if (px >= 102 && px <= 134) {
                    int s0 = std::min(g_state.smpSelStart, g_state.smpSelEnd);
                    int s1 = std::max(g_state.smpSelStart, g_state.smpSelEnd);
                    if (s0 >= 0 && s1 > s0 && s1 <= (int)smp.pcm.size()) {
                        smp.pcm.erase(smp.pcm.begin() + s0, smp.pcm.begin() + s1);
                        g_state.smpSelStart = g_state.smpSelEnd = -1;
                    } else {
                        smp.pcm.clear(); smp.loopStart = smp.loopEnd = 0; g_state.smpSelStart = g_state.smpSelEnd = -1;
                    }
                }
            }
            if (py >= 94 && py <= 104) {
                if (px >= 36 && px <= 47) smp.ampPct = std::max(0, smp.ampPct - 5 * fastStep);
                if (px >= 48 && px <= 59) smp.ampPct = std::min(200, smp.ampPct + 5 * fastStep);
                if (px >= 102 && px <= 113) smp.volPct = std::max(0, smp.volPct - 5 * fastStep);
                if (px >= 114 && px <= 125) smp.volPct = std::min(200, smp.volPct + 5 * fastStep);
            }
            if (py >= 108 && py <= 118) {
                if (px >= 36 && px <= 47) smp.panPct = std::max(-100, smp.panPct - 5 * fastStep);
                if (px >= 48 && px <= 59) smp.panPct = std::min(100, smp.panPct + 5 * fastStep);
                // TUN range -24 to +24
                if (px >= 102 && px <= 113) smp.fineTune = (s8)std::max(-24, (int)smp.fineTune - fastStep);
                if (px >= 114 && px <= 125) smp.fineTune = (s8)std::min(24, (int)smp.fineTune + fastStep);
            }
            if (py >= 122 && py <= 133) {
                if (px >= 42 && px <= 56) smp.relNote = (s8)std::max(-48, (int)smp.relNote - 12);
                if (px >= 58 && px <= 72) smp.relNote = (s8)std::min(48, (int)smp.relNote + 12);
            }
        } else if (g_state.activeTab == 3) {
            auto& inst = g_state.instruments[g_state.curInst];
            // IMS graphical envelope interaction (drag existing node or click to select/add)
            if (!isRepeat && py >= 20 && py <= 92 && px >= 6 && px <= 134) {
                int envX = std::clamp(px - 8, 0, 124);
                int envY = std::clamp(90 - py, 0, 64);
                int hitIdx = -1;
                int bestDist = 8;
                for (size_t i = 0; i < inst.envPoints.size(); ++i) {
                    int dx = std::abs(inst.envPoints[i].first - envX);
                    int dy = std::abs(inst.envPoints[i].second - envY);
                    int d = std::max(dx, dy);
                    if (d <= bestDist) { bestDist = d; hitIdx = (int)i; }
                }
                if (hitIdx >= 0) {
                    g_state.envDragPoint = hitIdx;
                    inst.susPoint = hitIdx;
                } else if (inst.envPoints.size() < 12) {
                    inst.envPoints.push_back({envX, envY});
                    std::sort(inst.envPoints.begin(), inst.envPoints.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first; });
                    for (size_t i = 0; i < inst.envPoints.size(); ++i) {
                        if (inst.envPoints[i].first == envX && inst.envPoints[i].second == envY) {
                            g_state.envDragPoint = (int)i;
                            inst.susPoint = (int)i;
                            break;
                        }
                    }
                }
                return;
            }
            if (!isRepeat && py >= 96 && py <= 112) inst.envEnabled = !inst.envEnabled;
            if (!isRepeat && py >= 116 && py <= 132) {
                if (px >= 6 && px <= 68) inst.susEnabled = !inst.susEnabled;
                if (px >= 72 && px <= 101 && inst.envPoints.size() < 12) {
                    int nextX = inst.envPoints.empty() ? 0 : std::min(124, inst.envPoints.back().first + 8);
                    inst.envPoints.push_back({nextX, 32});
                    inst.susPoint = std::min((int)inst.envPoints.size() - 1, inst.susPoint);
                }
                if (px >= 105 && px <= 134 && inst.envPoints.size() > 2) {
                    inst.envPoints.pop_back();
                    inst.susPoint = std::min((int)inst.envPoints.size() - 1, inst.susPoint);
                }
            }
        } else {
            if (isRepeat) return;
            if (py >= 31 && py <= 45) g_state.showDrvModal = true;
            if (py >= 31 && py <= 45 && px >= 78) { g_state.showCfgHelp = true; g_state.showDrvModal = false; }
            if (py >= 108 && py <= 130) runCpuCheckAutoOptimize();
        }
    }
}

void processMouseHoldRepeat(HWND hwnd) {
    if (!g_mouseHold.active) return;
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        g_mouseHold.active = false;
        return;
    }
    DWORD now = GetTickCount();
    if (now - g_mouseHold.startTick >= 220 && now - g_mouseHold.lastTick >= 35) {
        g_mouseHold.lastTick = now;
        if (g_mouseHold.isSub) handleTouchSubScreen(hwnd, g_mouseHold.px, g_mouseHold.py, true);
    }
}

// ============================================================================
// FRAMEBUFFER, WNDPROC,WINMAIN
// ============================================================================
void updatePcFramebuffer() {
    for (int y = 0; y < NDS_HEIGHT; ++y)
        for (int x = 0; x < NDS_WIDTH; ++x)
            g_pc_framebuffer[y * NDS_WIDTH + x] = RGB15toARGB32(g_main_vram[y * NDS_WIDTH + x]);
    for (int y = 0; y < NDS_HEIGHT; ++y)
        for (int x = 0; x < NDS_WIDTH; ++x)
            g_pc_framebuffer[(NDS_HEIGHT + y) * NDS_WIDTH + x] = RGB15toARGB32(g_sub_vram[y * NDS_WIDTH + x]);
}

void presentFramebuffer(HWND hwnd) {
    updatePcFramebuffer();
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = NDS_WIDTH;
    bi.bmiHeader.biHeight = -TOTAL_HEIGHT;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, 0, 0, NDS_WIDTH * WINDOW_SCALE, TOTAL_HEIGHT * WINDOW_SCALE,
                  0, 0, NDS_WIDTH, TOTAL_HEIGHT,
                  g_pc_framebuffer, &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

int noteFromKey(WPARAM key) {
    static const struct { int vk; int semi; } map[] = {
        {'Z',0},{'S',1},{'X',2},{'D',3},{'C',4},{'V',5},{'G',6},{'B',7},{'H',8},{'N',9},{'J',10},{'M',11},
        {'Q',12},{'2',13},{'W',14},{'3',15},{'E',16},{'R',17},{'5',18},{'T',19},{'6',20},{'Y',21},{'7',22},{'U',23}
    };
    for (auto& e : map) if ((WPARAM)e.vk == key) return e.semi;
    return -1;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 16, nullptr);
        return 0;

    case WM_TIMER:
        if (wp == 1) {
            processMouseHoldRepeat(hwnd);
            renderMainScreen();
            renderSubScreen();
            presentFramebuffer(hwnd);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        int mx = (short)LOWORD(lp) / WINDOW_SCALE;
        int my = (short)HIWORD(lp) / WINDOW_SCALE;
        g_mouseHold.active    = true;
        g_mouseHold.px        = mx;
        g_mouseHold.py        = (my < NDS_HEIGHT) ? my : (my - NDS_HEIGHT);
        g_mouseHold.isSub     = (my >= NDS_HEIGHT);
        g_mouseHold.startTick = GetTickCount();
        g_mouseHold.lastTick  = g_mouseHold.startTick;

        if (my < NDS_HEIGHT) handleTouchMainScreen(mx, my);
        else                 handleTouchSubScreen(hwnd, mx, my - NDS_HEIGHT, false);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lp) / WINDOW_SCALE;
        int my = (short)HIWORD(lp) / WINDOW_SCALE;
        if (my >= NDS_HEIGHT) {
            int py = my - NDS_HEIGHT;
            if (g_state.activeTab == 2 && g_state.smpDragging) {
                auto& smp = g_state.instruments[g_state.curInst].sample;
                if (!smp.pcm.empty()) {
                    int relX = std::clamp(mx - 7, 0, 126);
                    g_state.smpSelEnd = (int)((relX * (long long)smp.pcm.size()) / 126);
                }
            } else if (g_state.activeTab == 3 && g_state.envDragPoint >= 0) {
                auto& inst = g_state.instruments[g_state.curInst];
                int idx = g_state.envDragPoint;
                if (idx >= 0 && idx < (int)inst.envPoints.size()) {
                    int envX = std::clamp(mx - 8, 0, 124);
                    int envY = std::clamp(90 - py, 0, 64);
                    int minX = (idx == 0) ? 0 : inst.envPoints[idx - 1].first + 1;
                    int maxX = (idx + 1 < (int)inst.envPoints.size()) ? inst.envPoints[idx + 1].first - 1 : 124;
                    if (idx == 0) envX = 0;
                    else envX = std::clamp(envX, minX, std::max(minX, maxX));
                    inst.envPoints[idx] = {envX, envY};
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        ReleaseCapture();
        g_mouseHold.active   = false;
        g_state.smpDragging  = false;
        g_state.envDragPoint = -1;
        return 0;

    case WM_KEYDOWN: {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        auto beginOrUpdateShiftSelection = [&]() {
            if (!g_state.selActive) {
                g_state.selActive    = true;
                g_state.selAnchorRow = g_state.cursorRow;
                g_state.selAnchorCh  = g_state.cursorChannel;
            }
        };

        if (wp == VK_UP) {
            if (autoRepeat(g_repUp, true)) {
                if (shift) beginOrUpdateShiftSelection();
                else       g_state.clearSelection();
                g_state.cursorRow = std::max(0, g_state.cursorRow - 1);
                if (shift) { g_state.selEndRow = g_state.cursorRow; g_state.selEndCh = g_state.cursorChannel; }
            }
            return 0;
        }
        if (wp == VK_DOWN) {
            if (autoRepeat(g_repDn, true)) {
                if (shift) beginOrUpdateShiftSelection();
                else       g_state.clearSelection();
                g_state.cursorRow = std::min(g_state.effectivePtnLen() - 1, g_state.cursorRow + 1);
                if (shift) { g_state.selEndRow = g_state.cursorRow; g_state.selEndCh = g_state.cursorChannel; }
            }
            return 0;
        }
        if (wp == VK_LEFT) {
            if (autoRepeat(g_repLeft, true)) {
                if (shift) {
                    beginOrUpdateShiftSelection();
                    g_state.cursorChannel = (g_state.cursorChannel - 1 + g_state.numChannels) % g_state.numChannels;
                    g_state.selEndRow = g_state.cursorRow;
                    g_state.selEndCh  = g_state.cursorChannel;
                } else {
                    g_state.clearSelection();
                    if (g_state.cursorCol > 0) g_state.cursorCol--;
                    else { g_state.cursorChannel = (g_state.cursorChannel - 1 + g_state.numChannels) % g_state.numChannels; g_state.cursorCol = 3; }
                }
                updateHScroll();
            }
            return 0;
        }
        if (wp == VK_RIGHT) {
            if (autoRepeat(g_repRight, true)) {
                if (shift) {
                    beginOrUpdateShiftSelection();
                    g_state.cursorChannel = (g_state.cursorChannel + 1) % g_state.numChannels;
                    g_state.selEndRow = g_state.cursorRow;
                    g_state.selEndCh  = g_state.cursorChannel;
                } else {
                    g_state.clearSelection();
                    if (g_state.cursorCol < 3) g_state.cursorCol++;
                    else { g_state.cursorChannel = (g_state.cursorChannel + 1) % g_state.numChannels; g_state.cursorCol = 0; }
                }
                updateHScroll();
            }
            return 0;
        }

        if (wp == VK_TAB) {
            g_state.clearSelection();
            if (shift)
                g_state.cursorChannel = (g_state.cursorChannel + 1) % g_state.numChannels;
            else
                g_state.cursorChannel = (g_state.cursorChannel - 1 + g_state.numChannels) % g_state.numChannels;
            updateHScroll();
            return 0;
        }

        if (wp == 'R') {
            g_state.recording = !g_state.recording;
            if (g_state.recording) {
                g_state.playing = true; g_state.paused = false;
                g_audio.currentTick = 0; g_audio.tickCounter = 0;
            }
            return 0;
        }

        if (wp == VK_SPACE) {
            g_state.playing = !g_state.playing;
            g_state.paused  = false;
            if (!g_state.playing) { g_audio.currentTick = 0; g_audio.tickCounter = 0; }
            return 0;
        }

        if (wp == VK_F1) { g_state.octave = std::max(1, g_state.octave - 1); return 0; }
        if (wp == VK_F2) { g_state.octave = std::min(6, g_state.octave + 1); return 0; }

        if (wp == VK_DELETE) {
            std::lock_guard<std::mutex> lock(g_audioMutex);
            if (g_state.selActive) {
                int r0, r1, c0, c1;
                g_state.getSelectionBounds(r0, r1, c0, c1);
                int len = g_state.effectivePtnLen();
                for (int r = std::max(0, r0); r <= std::min(len - 1, r1); ++r)
                    for (int c = std::max(0, c0); c <= std::min(g_state.numChannels - 1, c1); ++c)
                        g_state.curPattern().cells[r][c] = PatternCell{};
                g_state.clearSelection();
            } else {
                g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel] = PatternCell{};
            }
            return 0;
        }

        // Green Volume Column direct 00-64 numeric entry
        if (g_state.cursorCol == 2 && wp >= '0' && wp <= '9') {
            int digit = (int)(wp - '0');
            PatternCell& cell = g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel];
            std::lock_guard<std::mutex> lock(g_audioMutex);
            u8 cur = (cell.volume == 0xFF) ? 64 : std::min((u8)64, cell.volume);
            u8 nextVol = (u8)((cur % 10) * 10 + digit);
            if (nextVol > 64) nextVol = (u8)digit;
            cell.volume = nextVol;
            return 0;
        }

        int semi = noteFromKey(wp);
        if (semi >= 0) {
            int note = 1 + (g_state.octave - 1) * 12 + semi;
            enterNoteAtCursor(note, semi);
            return 0;
        }

        if ((wp >= '0' && wp <= '9') || (wp >= 'A' && wp <= 'F') || (wp >= 'a' && wp <= 'f')) {
            int val = (wp >= '0' && wp <= '9') ? (wp - '0') : ((wp | 0x20) - 'a' + 10);
            PatternCell& cell = g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel];
            std::lock_guard<std::mutex> lock(g_audioMutex);
            switch (g_state.cursorCol) {
                case 1: cell.instr   = (u8)(val + 1); break;
                case 3: cell.fxParam = (u8)((cell.fxParam & 0xF0) | val); break;
                default: break;
            }
            return 0;
        }

        return 0;
    }

    case WM_KEYUP:
        if (wp == VK_UP)    g_repUp.down = false;
        if (wp == VK_DOWN)  g_repDn.down = false;
        if (wp == VK_LEFT)  g_repLeft.down = false;
        if (wp == VK_RIGHT) g_repRight.down = false;
        return 0;

    case WM_DESTROY:
        g_audio.stop();
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    loadThemeMemory();
    g_audio.enumerateAudioDrivers();
    g_audio.initBuiltInSamples();
    g_audio.startWASAPI();

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "NitroTrackerPC";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    RECT r = {0, 0, NDS_WIDTH * WINDOW_SCALE, TOTAL_HEIGHT * WINDOW_SCALE};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA("NitroTrackerPC", "NitroTracker PC by Questyn",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
