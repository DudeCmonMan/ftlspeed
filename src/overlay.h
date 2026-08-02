#ifndef FTLSPEED_OVERLAY_H
#define FTLSPEED_OVERLAY_H

#include <windows.h>
#include "common.h"

typedef struct {
    double baseSpeed;
    double effectiveSpeed;
    int turboHeld;
} OverlayStatus;

/* Callbacks into core so the overlay never owns speed state. */
typedef struct {
    void (*stepPreset)(int direction);
    void (*setSpeed)(double speed);
    void (*readStatus)(OverlayStatus *out);
} OverlayHost;

void OverlayInit(const SpeedConfig *cfg, const wchar_t *stateDir, const OverlayHost *host);
void OverlayToggle(void);
void OverlayAttachWindow(HWND window);
void OverlayOnSwapBuffers(HDC dc);

/* True while the manual entry field owns the keyboard, so core skips its hotkeys. */
int OverlayCapturesKeyboard(void);

#endif
