#pragma once
#include <time.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

typedef void*    HWND;
typedef uint32_t DWORD;
#define MAX_PATH 512

extern char g_dataPath[512];

static inline DWORD GetTickCount() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}
static inline std::string getExeThemePath() {
    return std::string(g_dataPath) + "/theme.dat";
}