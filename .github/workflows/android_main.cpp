#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dirent.h>
#include <sys/stat.h>

char g_dataPath[512] = "/data/local/tmp";
static char g_extPath[512] = "/sdcard";
#include "tracker.cpp"

static android_app* g_app = nullptr;
static EGLDisplay g_dsp = EGL_NO_DISPLAY;
static EGLSurface g_srf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static GLuint g_tex = 0; static GLint g_aPos = 0, g_aUv = 0;
static bool g_glOk = false;
static int g_sw = 0, g_sh = 0, g_ox = 0, g_oy = 0;
static float g_scale = 1.0f;
static u8 g_rgba[NDS_WIDTH * TOTAL_HEIGHT * 4];
static bool g_down = false; static int g_hx = 0, g_hy = 0;
static DWORD g_hStart = 0, g_hLast = 0;
static bool g_pad = false;

struct Browser {
    bool open = false; int mode = 0; std::string dir;
    std::vector<std::string> name; std::vector<bool> isDir; int scroll = 0;
} g_br;

static void brRefresh() {
    g_br.name.clear(); g_br.isDir.clear(); g_br.scroll = 0;
    DIR* d = opendir(g_br.dir.c_str()); if (!d) return;
    for (int pass = 0; pass < 2; ++pass) {
        rewinddir(d);
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            struct stat st;
            bool isd = (!stat((g_br.dir + "/" + n).c_str(), &st) && S_ISDIR(st.st_mode));
            if ((pass == 0) == isd) { g_br.name.push_back(n); g_br.isDir.push_back(isd); }
        }
    }
    closedir(d);
}
static void brOpen(int m) { g_br.mode = m; g_br.dir = g_extPath; g_br.open = true; brRefresh(); }
static const char* brExt() {
    const char* e[4] = {"wav","flac","opus","mp3"};
    return e[std::clamp(g_state.exportFmt, 0, 3)];
}
static void brDoSave() {
    std::string p = g_br.dir + "/" + (g_br.mode == 2 ? "project.nr" : std::string("master_export.") + brExt());
    std::lock_guard<std::mutex> lk(g_audioMutex);
    if (g_br.mode == 2) {
        if (saveProjectNR(p.c_str())) { g_state.songName = "project.nr"; g_state.statusMsg = "SAVED .NR"; }
        else g_state.statusMsg = "SAVE ERROR";
    } else {
        if (exportFullSongMaster(p.c_str(), g_state.exportFmt, g_state.exportBits))
            g_state.statusMsg = std::string("EXPORTED ") + brExt();
        else g_state.statusMsg = "EXPORT ERROR";
    }
    g_br.open = false;
}
static void brTap(int x, int y) {
    if (y >= 176) {
        if (x < 42) {
            size_t s = g_br.dir.find_last_of('/');
            if (s != std::string::npos && s > 0) { g_br.dir = g_br.dir.substr(0, s); brRefresh(); }
        } else if (x < 88) { g_br.dir = g_extPath; brRefresh(); }
        else if (x < 150 && (g_br.mode == 2 || g_br.mode == 4)) brDoSave();
        else if (x < 172) g_br.scroll = std::max(0, g_br.scroll - 6);
        else if (x < 194) g_br.scroll = std::min(std::max(0, (int)g_br.name.size() - 14), g_br.scroll + 6);
        else g_br.open = false;
        return;
    }
    if (y < 22) return;
    int i = g_br.scroll + (y - 22) / 11;
    if (i < 0 || i >= (int)g_br.name.size()) return;
    std::string full = g_br.dir + "/" + g_br.name[i];
    if (g_br.isDir[i]) { g_br.dir = full; brRefresh(); return; }
    std::lock_guard<std::mutex> lk(g_audioMutex);
    if (g_br.mode == 1) {
        SampleData s;
        if (loadUniversalAudioSample(full.c_str(), s)) {
            char lbl[64]; snprintf(lbl, sizeof(lbl), "%02d:%s", g_state.curInst + 1, g_br.name[i].c_str());
            g_state.instruments[g_state.curInst].name = lbl;
            g_state.instruments[g_state.curInst].sample = std::move(s);
            g_state.smpSelStart = g_state.smpSelEnd = -1;
            g_state.statusMsg = "SAMPLE LOADED"; g_br.open = false;
        } else g_state.statusMsg = "WAV LOAD ERROR";
    } else if (g_br.mode == 3) {
        if (loadProjectFile(full.c_str())) {
            g_state.songName = g_br.name[i]; g_state.statusMsg = "MODULE LOADED"; g_br.open = false;
        } else g_state.statusMsg = "LOAD ERROR";
    }
}
static void brDraw() {
    Canvas cv{g_sub_vram};
    cv.clear(g_theme.col_bg);
    const char* t[5] = {"", "LOAD WAV", "SAVE .NR", "OPEN MODULE", "EXPORT MASTER"};
    cv.drawText(t[g_br.mode], 4, 4, g_theme.col_light_ctrl);
    cv.drawText(g_br.dir.c_str(), 4, 13, g_theme.col_lighter_bg, 248);
    cv.fillRect(2, 21, 252, 153, g_theme.col_light_bg);
    for (int i = 0; i < 14; ++i) {
        int k = g_br.scroll + i; if (k >= (int)g_br.name.size()) break;
        std::string s = g_br.isDir[k] ? "[" + g_br.name[k] + "]" : g_br.name[k];
        cv.drawText(s.c_str(), 5, 24 + i * 11, g_br.isDir[k] ? g_theme.col_light_ctrl : g_theme.col_text, 246);
    }
    drawTobButton(cv, 2, 176, 40, 14, "UP");
    drawTobButton(cv, 44, 176, 44, 14, "HOME");
    if (g_br.mode == 2 || g_br.mode == 4) drawTobButton(cv, 90, 176, 60, 14, "SAVE HERE");
    drawTobButton(cv, 152, 176, 20, 14, "^");
    drawTobButton(cv, 174, 176, 20, 14, "v");
    drawTobButton(cv, 196, 176, 58, 14, "CLOSE");
}
static void padDraw() {
    Canvas cv{g_sub_vram};
    cv.fillRect(16, 18, 224, 156, g_theme.col_medium_bg);
    cv.drawRect(16, 18, 224, 156, g_theme.col_light_ctrl);
    const char* lbl[4] = {"NOTE", "INST", "VOL 00-64", "FX PARAM"};
    cv.drawText(lbl[std::clamp(g_state.cursorCol, 0, 3)], 22, 24, g_theme.col_light_ctrl);
    const char* h = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        char b[2] = {h[i], 0};
        drawTobButton(cv, 20 + (i % 4) * 54, 36 + (i / 4) * 28, 52, 26, b);
    }
    drawTobButton(cv, 20, 150, 216, 18, "CLOSE");
}
static void padTap(int x, int y) {
    if (y >= 150 && y <= 168) { g_pad = false; return; }
    for (int i = 0; i < 16; ++i) {
        int bx = 20 + (i % 4) * 54, by = 36 + (i / 4) * 28;
        if (x < bx || x >= bx + 52 || y < by || y >= by + 26) continue;
        std::lock_guard<std::mutex> lk(g_audioMutex);
        PatternCell& c = g_state.curPattern().cells[g_state.cursorRow][g_state.cursorChannel];
        if (g_state.cursorCol == 1) c.instr = (u8)std::min(80, i + 1);
        else if (g_state.cursorCol == 2) {
            if (i > 9) return;
            u8 cur = (c.volume == 0xFF) ? 64 : std::min((u8)64, c.volume);
            int v = (cur % 10) * 10 + i;
            c.volume = (u8)(v > 64 ? i : v);
        } else if (g_state.cursorCol == 3) c.fxParam = (u8)((c.fxParam << 4) | i);
        return;
    }
}
static void pianoTap(int x, int y) {
    static const int bo[10] = {12,30,66,84,102,138,156,192,210,228};
    static const int bs[10] = {1,3,6,8,10,13,15,18,20,22};
    static const int ws[14] = {0,2,4,5,7,9,11,12,14,16,17,19,21,23};
    int semi = -1;
    if (y < 175) for (int b = 0; b < 10; ++b) {
        int bx = 2 + bo[b];
        if (x >= bx && x < bx + 11) { semi = bs[b]; break; }
    }
    if (semi < 0) { int w = (x - 2) / 18; if (w >= 0 && w < 14) semi = ws[w]; }
    if (semi < 0) return;
    int note = 1 + (g_state.octave - 1) * 12 + semi;
    if (note >= 1 && note <= 96) enterNoteAtCursor(note, semi);
}
static void tap(int lx, int ly, bool rep) {
    if (lx < 0 || lx >= NDS_WIDTH || ly < 0 || ly >= TOTAL_HEIGHT) return;
    if (g_br.open) { if (!rep) brTap(lx, ly); return; }
    if (ly < NDS_HEIGHT) {
        if (rep) return;
        if (g_pad) { g_pad = false; return; }
        handleTouchMainScreen(lx, ly);
        if (ly >= 13 && lx >= 13 && lx < 221 && g_state.cursorCol > 0) g_pad = true;
        return;
    }
    int sy = ly - NDS_HEIGHT;
    if (g_pad) { if (!rep) padTap(lx, sy); return; }
    bool modal = g_state.showDskModal || g_state.showExportModal || g_state.showFxModal
              || g_state.showDrvModal || g_state.showCfgHelp;
    if (!rep && !modal && sy >= 152) { pianoTap(lx, sy); return; }
    handleTouchSubScreen(nullptr, lx, sy, rep);
}
static void holdRepeat() {
    if (!g_down) return;
    DWORD n = GetTickCount();
    if (n - g_hStart >= 220 && n - g_hLast >= 35) { g_hLast = n; tap(g_hx, g_hy, true); }
}
static void dragMove(int lx, int sy) {
    if (g_state.activeTab == 2 && g_state.smpDragging) {
        auto& smp = g_state.instruments[g_state.curInst].sample;
        if (!smp.pcm.empty())
            g_state.smpSelEnd = (int)((std::clamp(lx - 7, 0, 126) * (long long)smp.pcm.size()) / 126);
    } else if (g_state.activeTab == 3 && g_state.envDragPoint >= 0) {
        auto& inst = g_state.instruments[g_state.curInst];
        int i = g_state.envDragPoint;
        if (i < (int)inst.envPoints.size()) {
            int ex = std::clamp(lx - 8, 0, 124), ey = std::clamp(90 - sy, 0, 64);
            if (i == 0) ex = 0;
            else {
                int mn = inst.envPoints[i - 1].first + 1;
                int mx = (i + 1 < (int)inst.envPoints.size()) ? inst.envPoints[i + 1].first - 1 : 124;
                ex = std::clamp(ex, mn, std::max(mn, mx));
            }
            inst.envPoints[i] = {ex, ey};
        }
    }
}
static int32_t onInput(android_app*, AInputEvent* ev) {
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int lx = (int)((AMotionEvent_getX(ev, 0) - g_ox) / g_scale);
    int ly = (int)((AMotionEvent_getY(ev, 0) - g_oy) / g_scale);
    switch (AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK) {
        case AMOTION_EVENT_ACTION_DOWN:
            g_down = true; g_hx = lx; g_hy = ly;
            g_hStart = g_hLast = GetTickCount(); tap(lx, ly, false); break;
        case AMOTION_EVENT_ACTION_MOVE:
            g_hx = lx; g_hy = ly;
            if (ly >= NDS_HEIGHT) dragMove(lx, ly - NDS_HEIGHT); break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            g_down = false; g_state.smpDragging = false; g_state.envDragPoint = -1; break;
    }
    return 1;
}
static const char* VS = "attribute vec2 p;attribute vec2 t;varying vec2 v;void main(){v=t;gl_Position=vec4(p,0.,1.);}";
static const char* FS = "precision mediump float;varying vec2 v;uniform sampler2D s;void main(){gl_FragColor=texture2D(s,v);}";
static GLuint mkShader(GLenum t, const char* s) {
    GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh); return sh;
}
static void initGL() {
    if (g_glOk || !g_app->window) return;
    g_dsp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_dsp, nullptr, nullptr);
    const EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    eglChooseConfig(g_dsp, ca, &cfg, 1, &n);
    g_srf = eglCreateWindowSurface(g_dsp, cfg, g_app->window, nullptr);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dsp, cfg, EGL_NO_CONTEXT, xa);
    eglMakeCurrent(g_dsp, g_srf, g_srf, g_ctx);
    eglQuerySurface(g_dsp, g_srf, EGL_WIDTH, &g_sw);
    eglQuerySurface(g_dsp, g_srf, EGL_HEIGHT, &g_sh);
    GLuint pr = glCreateProgram();
    glAttachShader(pr, mkShader(GL_VERTEX_SHADER, VS));
    glAttachShader(pr, mkShader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(pr); glUseProgram(pr);
    g_aPos = glGetAttribLocation(pr, "p"); g_aUv = glGetAttribLocation(pr, "t");
    glGenTextures(1, &g_tex); glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, NDS_WIDTH, TOTAL_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glUniform1i(glGetUniformLocation(pr, "s"), 0);
    g_scale = std::min((float)g_sw / NDS_WIDTH, (float)g_sh / TOTAL_HEIGHT);
    int dw = (int)(NDS_WIDTH * g_scale), dh = (int)(TOTAL_HEIGHT * g_scale);
    g_ox = (g_sw - dw) / 2; g_oy = (g_sh - dh) / 2;
    glViewport(g_ox, g_sh - g_oy - dh, dw, dh);
    glDisable(GL_DEPTH_TEST);
    g_glOk = true;
}
static void termGL() {
    if (!g_glOk) return;
    eglMakeCurrent(g_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_srf != EGL_NO_SURFACE) eglDestroySurface(g_dsp, g_srf);
    if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dsp, g_ctx);
    g_srf = EGL_NO_SURFACE; g_ctx = EGL_NO_CONTEXT; g_glOk = false;
}
static void drawFrame() {
    for (int i = 0; i < NDS_WIDTH * TOTAL_HEIGHT; ++i) {
        u32 c = g_pc_framebuffer[i];
        g_rgba[i*4] = (u8)(c >> 16); g_rgba[i*4+1] = (u8)(c >> 8);
        g_rgba[i*4+2] = (u8)c;       g_rgba[i*4+3] = 255;
    }
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NDS_WIDTH, TOTAL_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_rgba);
    const GLfloat v[] = {-1,1,0,0, -1,-1,0,1, 1,1,1,0, 1,-1,1,1};
    glEnableVertexAttribArray(g_aPos); glEnableVertexAttribArray(g_aUv);
    glVertexAttribPointer(g_aPos, 2, GL_FLOAT, GL_FALSE, 16, v);
    glVertexAttribPointer(g_aUv, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(g_dsp, g_srf);
}
static void onCmd(android_app*, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: initGL(); break;
        case APP_CMD_TERM_WINDOW: termGL(); break;
        case APP_CMD_GAINED_FOCUS: if (!g_audio.running) g_audio.startWASAPI(); break;
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_DESTROY: g_audio.stop(); break;
    }
}
void android_main(android_app* app) {
    g_app = app; app->onAppCmd = onCmd; app->onInputEvent = onInput;
    snprintf(g_dataPath, sizeof(g_dataPath), "%s", app->activity->internalDataPath);
    if (app->activity->externalDataPath)
        snprintf(g_extPath, sizeof(g_extPath), "%s", app->activity->externalDataPath);
    loadThemeMemory();
    g_audio.enumerateAudioDrivers();
    g_audio.initBuiltInSamples();
    while (!app->destroyRequested) {
        android_poll_source* src;
        while (ALooper_pollOnce(g_glOk ? 0 : -1, nullptr, nullptr, (void**)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested || !g_glOk) continue;
        g_audio.pollAudioRestart();
        if (g_fileReq) { brOpen(g_fileReq); g_fileReq = 0; }
        holdRepeat();
        { std::lock_guard<std::mutex> lk(g_audioMutex); renderMainScreen(); renderSubScreen(); }
        if (g_pad) padDraw();
        if (g_br.open) brDraw();
        updatePcFramebuffer();
        drawFrame();
        usleep(16000);
    }
    g_audio.stop();
}