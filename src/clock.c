#include "clock.h"
#include "common.h"

BOOL  (WINAPI *Real_QueryPerformanceCounter)(LARGE_INTEGER *) = NULL;
DWORD (WINAPI *Real_GetTickCount)(void) = NULL;
DWORD (WINAPI *Real_timeGetTime)(void) = NULL;
void  (WINAPI *Real_Sleep)(DWORD) = NULL;

static CRITICAL_SECTION lock;
static double multiplier = 1.0;

static LONGLONG qpcRealBase;
static LONGLONG qpcFakeBase;
static LONGLONG qpcLast;

static DWORD tickRealBase;
static DWORD tickFakeBase;
static DWORD tickLast;

static DWORD tgtRealBase;
static DWORD tgtFakeBase;
static DWORD tgtLast;

static ClockStats stats;

void ClockInit(void)
{
    LARGE_INTEGER qpc;
    DWORD tick;
    DWORD tgt;

    InitializeCriticalSection(&lock);

    multiplier = 1.0;
    ZeroMemory(&stats, sizeof(stats));

    qpc.QuadPart = 0;
    Real_QueryPerformanceCounter(&qpc);
    qpcRealBase = qpc.QuadPart;
    qpcFakeBase = qpc.QuadPart;
    qpcLast = qpc.QuadPart;

    tick = Real_GetTickCount();
    tickRealBase = tick;
    tickFakeBase = tick;
    tickLast = tick;

    tgt = Real_timeGetTime();
    tgtRealBase = tgt;
    tgtFakeBase = tgt;
    tgtLast = tgt;
}

void ClockSetSpeed(double speed)
{
    LARGE_INTEGER qpc;
    DWORD tick;
    DWORD tgt;

    /* Negated compare so NaN clamps to the floor instead of poisoning every base. */
    if (!(speed >= FTLSPEED_MIN_SPEED))
        speed = FTLSPEED_MIN_SPEED;
    if (speed > FTLSPEED_MAX_SPEED)
        speed = FTLSPEED_MAX_SPEED;

    EnterCriticalSection(&lock);

    /* Rebase every clock at the old multiplier first, else time jumps on change. */
    qpc.QuadPart = qpcRealBase;
    Real_QueryPerformanceCounter(&qpc);
    qpcFakeBase = qpcFakeBase + (LONGLONG)((double)(qpc.QuadPart - qpcRealBase) * multiplier);
    qpcRealBase = qpc.QuadPart;

    tick = Real_GetTickCount();
    tickFakeBase = tickFakeBase + (DWORD)(LONGLONG)((double)(tick - tickRealBase) * multiplier);
    tickRealBase = tick;

    tgt = Real_timeGetTime();
    tgtFakeBase = tgtFakeBase + (DWORD)(LONGLONG)((double)(tgt - tgtRealBase) * multiplier);
    tgtRealBase = tgt;

    multiplier = speed;

    LeaveCriticalSection(&lock);
}

double ClockGetSpeed(void)
{
    double speed;

    EnterCriticalSection(&lock);
    speed = multiplier;
    LeaveCriticalSection(&lock);

    return speed;
}

void ClockGetStats(ClockStats *out)
{
    EnterCriticalSection(&lock);
    *out = stats;
    LeaveCriticalSection(&lock);
}

BOOL WINAPI Hook_QueryPerformanceCounter(LARGE_INTEGER *counter)
{
    LONGLONG fake;

    InterlockedIncrement(&stats.qpcCalls);

    /* Sample under the lock, else a concurrent rebase pairs this reading with the wrong base. */
    EnterCriticalSection(&lock);
    if (!Real_QueryPerformanceCounter(counter)) {
        LeaveCriticalSection(&lock);
        return FALSE;
    }
    fake = qpcFakeBase + (LONGLONG)((double)(counter->QuadPart - qpcRealBase) * multiplier);
    if (fake < qpcLast)
        InterlockedIncrement(&stats.regressions);
    qpcLast = fake;
    LeaveCriticalSection(&lock);

    counter->QuadPart = fake;
    return TRUE;
}

DWORD WINAPI Hook_GetTickCount(void)
{
    DWORD real;
    DWORD fake;

    InterlockedIncrement(&stats.tickCalls);

    EnterCriticalSection(&lock);
    real = Real_GetTickCount();
    fake = tickFakeBase + (DWORD)(LONGLONG)((double)(real - tickRealBase) * multiplier);
    if ((LONG)(fake - tickLast) < 0)
        InterlockedIncrement(&stats.regressions);
    tickLast = fake;
    LeaveCriticalSection(&lock);

    return fake;
}

DWORD WINAPI Hook_timeGetTime(void)
{
    DWORD real;
    DWORD fake;

    InterlockedIncrement(&stats.timeGetTimeCalls);

    EnterCriticalSection(&lock);
    real = Real_timeGetTime();
    fake = tgtFakeBase + (DWORD)(LONGLONG)((double)(real - tgtRealBase) * multiplier);
    if ((LONG)(fake - tgtLast) < 0)
        InterlockedIncrement(&stats.regressions);
    tgtLast = fake;
    LeaveCriticalSection(&lock);

    return fake;
}

void WINAPI Hook_Sleep(DWORD milliseconds)
{
    double speed;
    double scaled;

    InterlockedIncrement(&stats.sleepCalls);

    /* Scaling INFINITE would turn a deliberate park into a short nap. */
    if (milliseconds == 0 || milliseconds == INFINITE) {
        Real_Sleep(milliseconds);
        return;
    }

    EnterCriticalSection(&lock);
    speed = multiplier;
    LeaveCriticalSection(&lock);

    scaled = milliseconds / speed;
    if (scaled < 1.0)
        scaled = 1.0;
    if (scaled > 1000.0)
        scaled = 1000.0;

    Real_Sleep((DWORD)scaled);
}
