#include "common.h"
#include "clock.h"
#include "iat.h"
#include "overlay.h"

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

/* Next preset strictly past the current speed, so a manually entered value such as
   2.7 steps to 3.0 going up and 2.0 going down. */
static void StepPreset(int direction)
{
    double best = 0.0;
    int found = 0;
    int i;

    if (cfg.presetCount <= 0)
        return;

    for (i = 0; i < cfg.presetCount; i++) {
        double candidate = cfg.presets[i];
        if (direction > 0 && candidate > baseSpeed) {
            if (!found || candidate < best) { best = candidate; found = 1; }
        } else if (direction < 0 && candidate < baseSpeed) {
            if (!found || candidate > best) { best = candidate; found = 1; }
        }
    }

    if (!found) {
        best = cfg.presets[0];
        for (i = 1; i < cfg.presetCount; i++) {
            if (direction > 0 ? cfg.presets[i] > best : cfg.presets[i] < best)
                best = cfg.presets[i];
        }
    }
    ApplySpeed(best);
}

static void ToggleSpeed(void)
{
    if (baseSpeed == 1.0)
        ApplySpeed(lastToggleSpeed);
    else
        ApplySpeed(1.0);
}

static BOOL (WINAPI *Real_SwapBuffers)(HDC);
static int swapHooked;

static BOOL WINAPI Hook_SwapBuffers(HDC dc)
{
    OverlayOnSwapBuffers(dc);
    return Real_SwapBuffers(dc);
}

static void HostStepPreset(int direction)
{
    StepPreset(direction);
}

static void HostSetSpeed(double speed)
{
    ApplySpeed(speed);
}

static void HostReadStatus(OverlayStatus *out)
{
    out->baseSpeed = baseSpeed;
    out->effectiveSpeed = ClockGetSpeed();
    out->turboHeld = turboHeld;
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
                  "OK qpc=%ld tick=%ld tgt=%ld sleep=%ld regressions=%ld hooks=%d/4 swap=%d\n",
                  stats.qpcCalls, stats.tickCalls, stats.timeGetTimeCalls,
                  stats.sleepCalls, stats.regressions, hooksInstalled, swapHooked);
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

static HWND gameWindow;

static void EnsureWindow(void)
{
    if (gameWindow && !IsWindow(gameWindow))
        gameWindow = NULL;
    if (gameWindow)
        return;
    EnumWindows(FindOwnWindow, (LPARAM)&gameWindow);
    if (gameWindow)
        OverlayAttachWindow(gameWindow);
}

static void UpdateTitle(void)
{
    static wchar_t original[TITLE_MAX];
    static wchar_t applied[TITLE_MAX];
    wchar_t current[TITLE_MAX];
    wchar_t wanted[TITLE_MAX];
    HWND window = gameWindow;

    if (!window)
        return;

    current[0] = 0;
    GetWindowTextW(window, current, TITLE_MAX);
    if (!current[0]) {
        /* FTL destroys and recreates its window on a fullscreen toggle. */
        gameWindow = NULL;
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
    int heldOverlay = 0;

    (void)parameter;

    for (;;) {
        int ours = ForegroundIsOurs();
        /* Speed keys stand down while the overlay's entry field owns the keyboard. */
        int act = ours && !OverlayCapturesKeyboard();
        int turboDown = act && (GetAsyncKeyState(cfg.turboKey) & 0x8000) != 0;

        EnsureWindow();

        if (KeyPressed(cfg.overlayKey, &heldOverlay) && ours)
            OverlayToggle();
        if (KeyPressed(cfg.fasterKey, &heldUp) && act)
            StepPreset(1);
        if (KeyPressed(cfg.slowerKey, &heldDown) && act)
            StepPreset(-1);
        if (KeyPressed(cfg.toggleKey, &heldToggle) && act)
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
    wchar_t directory[MAX_PATH + 8];
    wchar_t path[MAX_PATH + 40];
    wchar_t *slash;
    HMODULE kernel32;
    HMODULE winmm;
    HMODULE gdi32;
    HMODULE game;
    OverlayHost host;

    ConfigDefaults(&cfg);
    directory[0] = 0;
    if (GetModuleFileNameW(self, directory, MAX_PATH)) {
        slash = wcsrchr(directory, L'\\');
        if (slash) {
            slash[1] = 0;
            swprintf_s(path, MAX_PATH + 40, L"%sspeed.toml", directory);
            ConfigLoadToml(&cfg, path);
        } else {
            directory[0] = 0;
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

    host.stepPreset = HostStepPreset;
    host.setSpeed = HostSetSpeed;
    host.readStatus = HostReadStatus;
    OverlayInit(&cfg, directory, &host);

    gdi32 = GetModuleHandleA("gdi32.dll");
    if (gdi32)
        Real_SwapBuffers = (BOOL (WINAPI *)(HDC))GetProcAddress(gdi32, "SwapBuffers");
    if (Real_SwapBuffers && IatHook(game, "SwapBuffers", (void *)Hook_SwapBuffers))
        swapHooked = 1;

    StartThread(PipeThread);
    StartThread(HotkeyThread);
}
