import re
src = open('main.cpp', encoding='utf-8', errors='replace').read()

def cut(a, b, repl=''):
    global src
    i = src.index(a); j = src.index(b, i)
    src = src[:i] + repl + src[j:]

STD = '''#include <cstdint>
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
#include "compat.h"
'''

ENUM = '''    void enumerateAudioDrivers() {
        g_state.outDevices.clear(); g_state.inDevices.clear();
        g_state.outDevices.push_back("OUT 1: AAUDIO LOW LATENCY");
        g_state.inDevices.push_back("IN 1: AAUDIO");
    }
'''

AUDIO = '''    AAudioStream* stream = nullptr;
    static aaudio_data_callback_result_t aaCb(AAudioStream*, void* u, void* d, int32_t n) {
        ((AudioEngine*)u)->renderBuffer((float*)d, n, true);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void aaErr(AAudioStream*, void* u, aaudio_result_t e) {
        if (e == AAUDIO_ERROR_DISCONNECTED) ((AudioEngine*)u)->restartRequested = true;
    }
    void startWASAPI() {
        running = true;
        for (int att = 0; att < 2 && !stream; ++att) {
            AAudioStreamBuilder* b = nullptr;
            if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
            AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
            AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
            AAudioStreamBuilder_setChannelCount(b, 2);
            AAudioStreamBuilder_setSampleRate(b, g_state.sampleRate);
            AAudioStreamBuilder_setSharingMode(b, att == 0 ? AAUDIO_SHARING_MODE_EXCLUSIVE : AAUDIO_SHARING_MODE_SHARED);
            AAudioStreamBuilder_setPerformanceMode(b, att == 0 ? AAUDIO_PERFORMANCE_MODE_LOW_LATENCY : AAUDIO_PERFORMANCE_MODE_NONE);
            AAudioStreamBuilder_setDataCallback(b, aaCb, this);
            AAudioStreamBuilder_setErrorCallback(b, aaErr, this);
            AAudioStream_openStream(b, &stream);
            AAudioStreamBuilder_delete(b);
        }
        if (!stream) { g_state.cpuStatus = "AUDIO OPEN FAILED"; return; }
        g_state.sampleRate = AAudioStream_getSampleRate(stream);
        int burst = AAudioStream_getFramesPerBurst(stream);
        if (burst <= 0) burst = 192;
        g_state.bufferFrames = burst;
        AAudioStream_setBufferSizeInFrames(stream, burst * 2);
        resizeDspBuffers(g_state.sampleRate);
        AAudioStream_requestStart(stream);
        g_state.cpuStatus = "AAUDIO ACTIVE";
    }
    void pollAudioRestart() {
        if (!restartRequested) return;
        restartRequested = false;
        stop(); startWASAPI();
    }
    void stop() {
        running = false;
        if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); stream = nullptr; }
    }
};
'''

DIALOGS = '''int  g_fileReq = 0;
void openLoadSampleDialog(HWND)   { g_fileReq = 1; }
void openSaveProjectDialog(HWND)  { g_fileReq = 2; }
void openLoadProjectDialog(HWND)  { g_fileReq = 3; }
void openExportMasterDialog(HWND) { g_fileReq = 4; }
void runCpuCheckAutoOptimize() {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    struct sysinfo si; sysinfo(&si);
    u64 ramMB = (u64)si.totalram * si.mem_unit / (1024 * 1024);
    if (cores >= 6 && ramMB >= 4096)      g_state.cpuStatus = "OPT: FAST CPU";
    else if (cores >= 4 && ramMB >= 2048) g_state.cpuStatus = "OPT: BALANCED";
    else                                  g_state.cpuStatus = "OPT: SAFE MODE";
    g_audio.restartRequested = true;
}
'''

cut('#define WIN32_LEAN_AND_MEAN', 'typedef uint8_t', STD)
cut('std::string getExeThemePath()', 'void saveThemeMemory()')
cut('void enumerateAudioDrivers()', 'void initBuiltInSamples()', ENUM)
cut('void startWASAPI()', 'static AudioEngine g_audio;', AUDIO)
cut('void openLoadSampleDialog(HWND hwnd)', 'void handleTouchMainScreen(int px, int py)', DIALOGS)
cut('void processMouseHoldRepeat(HWND hwnd)', 'void updatePcFramebuffer()')
src = src[:src.index('void presentFramebuffer(HWND hwnd)')]

src = src.replace('fo rmatNoteString', 'formatNoteString')
src = src.replace('B IT(', 'BIT(').replace('& &', '&&')
src = src.replace('< <', '<<').replace('> >', '>>')
src = re.sub(r'BIT\(\s*(\d+)\s*\)', r'BIT(\1)', src)
src = re.sub(r'std::vector\s*<\s*(\w+)\s*>', r'std::vector<\1>', src)
src = re.sub(r'#include\s+<\s*(\w+)\s*>', r'#include <\1>', src)

i = src.index('static void computeMd5'); j = src.index('bool writeNativeFlacFile')
seg = re.sub(r'0x\s*([0-9a-fA-F]+)(?:\s+([0-9a-fA-F]+))?',
             lambda m: '0x' + m.group(1) + (m.group(2) or ''), src[i:j])
src = src[:i] + seg + src[j:]

for a, b in [('"M.K. "','"M.K."'), ('"M!K! "','"M!K!"'), ('"FLT4 "','"FLT4."'),
             ('"6CHN "','"6CHN"'), ('"8CHN "','"8CHN"'), ('"OCTA "','"OCTA"'),
             ('".med "','".med"'), ('".ahx "','".ahx"'),
             ('"MMD0 "','"MMD0"'), ('"MMD1 "','"MMD1"'),
             ('"MMD2 "','"MMD2"'), ('"MMD3 "','"MMD3"'),
             (r'"THX\0 "', r'"THX\0"'), (r'"THX\1 "', r'"THX\1"'),
             ('char noteStr[4]', 'char noteStr[8]'), ('fxStr[6]', 'fxStr[8]'),
             ('char thm[36]', 'char thm[48]')]:
    src = src.replace(a, b)

open('tracker.cpp', 'w', encoding='utf-8').write(src)
print('tracker.cpp generado:', len(src.splitlines()), 'lineas')