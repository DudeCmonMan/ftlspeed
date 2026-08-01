#include "common.h"
#include "clock.h"
#include "iat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define TITLE_MAX 320

static SpeedConfig cfg;
static int hooksInstalled;
static double lastToggleSpeed = 2.0;
static double baseSpeed = 1.0;
static int turboHeld;

static double ClampSpeed(double speed)
{
    /* Negated compare so NaN (strtod "nan", speed = nan in the toml) clamps to the floor. */
    if (!(speed >= FTLSPEED_MIN_SPEED))
        return FTLSPEED_MIN_SPEED;
    if (speed > FTLSPEED_MAX_SPEED)
        return FTLSPEED_MAX_SPEED;
    return speed;
}

static void ApplyEffective(void)
{
    ClockSetSpeed(ClampSpeed(turboHeld ? baseSpeed * cfg.turboSpeed : baseSpeed));
}

static void ApplySpeed(double speed)
{
    baseSpeed = ClampSpeed(speed);
    if (baseSpeed != 1.0)
        lastToggleSpeed = baseSpeed;
    ApplyEffective();
}

static void StepPreset(int direction)
{
    double current = baseSpeed;
    double bestDistance;
    int best = 0;
    int i;

    if (cfg.presetCount <= 0)
        return;
    bestDistance = fabs(cfg.presets[0] - current);
    for (i = 1; i < cfg.presetCount; i++) {
        double distance = fabs(cfg.presets[i] - current);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    best += direction;
    if (best < 0)
        best = 0;
    if (best >= cfg.presetCount)
        best = cfg.presetCount - 1;
    ApplySpeed(cfg.presets[best]);
}

static void ToggleSpeed(void)
{
    if (baseSpeed == 1.0)
        ApplySpeed(lastToggleSpeed);
    else
        ApplySpeed(1.0);
}

static void HandleCommand(char *request, char *response, size_t responseSize)
{
    size_t length = strlen(request);

    while (length > 0 && (unsigned char)request[length - 1] <= ' ')
        request[--length] = 0;

    if (strcmp(request, "GET") == 0) {
        /* reply below */
    } else if (strncmp(request, "SET ", 4) == 0) {
        char *end;
        double value = strtod(request + 4, &end);
        if (end == request + 4) {
            sprintf_s(response, responseSize, "ERR bad speed\n");
            return;
        }
        ApplySpeed(value);
    } else if (strcmp(request, "UP") == 0) {
        StepPreset(1);
    } else if (strcmp(request, "DOWN") == 0) {
        StepPreset(-1);
    } else if (strcmp(request, "TOGGLE") == 0) {
        ToggleSpeed();
    } else if (strcmp(request, "RESET") == 0) {
        ApplySpeed(1.0);
    } else if (strcmp(request, "STATS") == 0) {
        ClockStats stats;
        ClockGetStats(&stats);
        sprintf_s(response, responseSize,
                  "OK qpc=%ld tick=%ld tgt=%ld sleep=%ld regressions=%ld hooks=%d/4\n",
                  stats.qpcCalls, stats.tickCalls, stats.timeGetTimeCalls,
                  stats.sleepCalls, stats.regressions, hooksInstalled);
        return;
    } else {
        sprintf_s(response, responseSize, "ERR unknown command\n");
        return;
    }

    sprintf_s(response, responseSize, "OK %.2f\n", ClockGetSpeed());
}

static DWORD WINAPI PipeThread(LPVOID parameter)
{
    (void)parameter;

    for (;;) {
        HANDLE pipe = CreateNamedPipeW(FTLSPEED_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       1, FTLSPEED_PIPE_BUF, FTLSPEED_PIPE_BUF, 0, NULL);
        char request[FTLSPEED_PIPE_BUF];
        char response[FTLSPEED_PIPE_BUF];
        DWORD read = 0;
        DWORD written = 0;

        if (pipe == INVALID_HANDLE_VALUE) {
            Real_Sleep(1000);
            continue;
        }
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            if (ReadFile(pipe, request, sizeof(request) - 1, &read, NULL) && read > 0) {
                request[read] = 0;
                response[0] = 0;
                HandleCommand(request, response, sizeof(response));
                WriteFile(pipe, response, (DWORD)strlen(response), &written, NULL);
                FlushFileBuffers(pipe);
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

static BOOL CALLBACK FindOwnWindow(HWND window, LPARAM parameter)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(window)) {
        *(HWND *)parameter = window;
        return FALSE;
    }
    return TRUE;
}

static void StripSuffix(wchar_t *title)
{
    wchar_t *marker = wcsstr(title, L"  [");
    size_t length = wcslen(title);

    if (marker && length > 0 && title[length - 1] == L']')
        *marker = 0;
}

static void UpdateTitle(void)
{
    static HWND window;
    static wchar_t original[TITLE_MAX];
    static wchar_t applied[TITLE_MAX];
    wchar_t current[TITLE_MAX];
    wchar_t wanted[TITLE_MAX];

    if (!window) {
        EnumWindows(FindOwnWindow, (LPARAM)&window);
        if (!window)
            return;
    }

    current[0] = 0;
    GetWindowTextW(window, current, TITLE_MAX);
    if (!current[0]) {
        /* FTL destroys and recreates its window on a fullscreen toggle. */
        window = NULL;
        applied[0] = 0;
        return;
    }

    /* Anything we did not write ourselves is FTL rewriting its own caption. */
    if (wcscmp(current, applied) != 0) {
        wcscpy_s(original, TITLE_MAX, current);
        StripSuffix(original);
    }

    _snwprintf_s(wanted, TITLE_MAX, _TRUNCATE, L"%s  [%.2fx]", original, ClockGetSpeed());
    if (wcscmp(wanted, current) != 0) {
        SetWindowTextW(window, wanted);
        wcscpy_s(applied, TITLE_MAX, wanted);
    }
}

static int KeyPressed(int key, int *held)
{
    int down = (GetAsyncKeyState(key) & 0x8000) != 0;
    int pressed = down && !*held;

    *held = down;
    return pressed;
}

static int ForegroundIsOurs(void)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI HotkeyThread(LPVOID parameter)
{
    int heldUp = 0;
    int heldDown = 0;
    int heldToggle = 0;

    (void)parameter;

    for (;;) {
        int ours = ForegroundIsOurs();
        int turboDown = ours && (GetAsyncKeyState(cfg.turboKey) & 0x8000) != 0;

        if (KeyPressed(cfg.fasterKey, &heldUp) && ours)
            StepPreset(1);
        if (KeyPressed(cfg.slowerKey, &heldDown) && ours)
            StepPreset(-1);
        if (KeyPressed(cfg.toggleKey, &heldToggle) && ours)
            ToggleSpeed();

        /* Gated on focus so losing focus mid-hold cannot latch turbo on. */
        if (turboDown != turboHeld) {
            turboHeld = turboDown;
            ApplyEffective();
        }

        if (cfg.showInTitle)
            UpdateTitle();

        /* Our own Sleep is hooked, so this poll must use the real one. */
        Real_Sleep(33);
    }
}

static void StartThread(LPTHREAD_START_ROUTINE entry)
{
    HANDLE thread = CreateThread(NULL, 0, entry, NULL, 0, NULL);

    if (thread)
        CloseHandle(thread);
}

void SpeedCoreStart(HMODULE self)
{
    wchar_t path[MAX_PATH + 32];
    wchar_t *slash;
    HMODULE kernel32;
    HMODULE winmm;
    HMODULE game;

    ConfigDefaults(&cfg);
    if (GetModuleFileNameW(self, path, MAX_PATH)) {
        slash = wcsrchr(path, L'\\');
        if (slash) {
            slash[1] = 0;
            wcscat_s(path, MAX_PATH + 32, L"speed.toml");
            ConfigLoadToml(&cfg, path);
        }
    }
    cfg.turboSpeed = ClampSpeed(cfg.turboSpeed);

    kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32)
        return;

    Real_QueryPerformanceCounter =
        (BOOL (WINAPI *)(LARGE_INTEGER *))GetProcAddress(kernel32, "QueryPerformanceCounter");
    Real_GetTickCount = (DWORD (WINAPI *)(void))GetProcAddress(kernel32, "GetTickCount");
    Real_Sleep = (void (WINAPI *)(DWORD))GetProcAddress(kernel32, "Sleep");
    if (!Real_QueryPerformanceCounter || !Real_GetTickCount || !Real_Sleep)
        return;

    /* No winmm loaded means the game has no timeGetTime import to hook, so the fallback
       keeps that clock consistent without making the other three hooks conditional. */
    winmm = GetModuleHandleA("winmm.dll");
    if (winmm)
        Real_timeGetTime = (DWORD (WINAPI *)(void))GetProcAddress(winmm, "timeGetTime");
    if (!Real_timeGetTime)
        Real_timeGetTime = Real_GetTickCount;

    ClockInit();
    ApplySpeed(cfg.speed);

    game = GetModuleHandleW(NULL);
    if (IatHook(game, "QueryPerformanceCounter", (void *)Hook_QueryPerformanceCounter))
        hooksInstalled++;
    if (IatHook(game, "GetTickCount", (void *)Hook_GetTickCount))
        hooksInstalled++;
    if (IatHook(game, "Sleep", (void *)Hook_Sleep))
        hooksInstalled++;
    if (IatHook(game, "timeGetTime", (void *)Hook_timeGetTime))
        hooksInstalled++;

    StartThread(PipeThread);
    StartThread(HotkeyThread);
}
