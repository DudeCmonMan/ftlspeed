#ifndef FTLSPEED_CLOCK_H
#define FTLSPEED_CLOCK_H

#include <windows.h>

typedef struct {
    LONG qpcCalls;
    LONG tickCalls;
    LONG timeGetTimeCalls;
    LONG sleepCalls;
    LONG regressions;
} ClockStats;

/* Real_* must be resolved via GetProcAddress and ClockInit called BEFORE any
   hook is installed, so a hook can never run against uninitialised bases. */
extern BOOL  (WINAPI *Real_QueryPerformanceCounter)(LARGE_INTEGER *);
extern DWORD (WINAPI *Real_GetTickCount)(void);
extern DWORD (WINAPI *Real_timeGetTime)(void);
extern void  (WINAPI *Real_Sleep)(DWORD);

void   ClockInit(void);
void   ClockSetSpeed(double speed);
double ClockGetSpeed(void);
void   ClockGetStats(ClockStats *out);

BOOL  WINAPI Hook_QueryPerformanceCounter(LARGE_INTEGER *counter);
DWORD WINAPI Hook_GetTickCount(void);
DWORD WINAPI Hook_timeGetTime(void);
void  WINAPI Hook_Sleep(DWORD milliseconds);

#endif
