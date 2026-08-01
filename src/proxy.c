#include "common.h"

#include <string.h>

extern const char *const g_names[];
extern const WORD g_ordinals[];
extern void *g_real[];

static HMODULE g_realDll;
static HMODULE g_self;

void *__stdcall ProxyResolve(int index)
{
    if (!g_realDll) {
        wchar_t path[MAX_PATH];
        if (!GetSystemDirectoryW(path, MAX_PATH))
            return NULL;
        lstrcatW(path, L"\\dbghelp.dll");
        g_realDll = LoadLibraryW(path);
        if (!g_realDll)
            return NULL;
    }
    if (!g_real[index]) {
        const char *name = g_names[index];
        g_real[index] = (void *)GetProcAddress(g_realDll,
                                               name ? name : MAKEINTRESOURCEA(g_ordinals[index]));
    }
    return g_real[index];
}

/* Entered with the export index in eax. The tail-jump leaves the stack exactly as the
   caller built it, so any calling convention passes through without knowing signatures.
   The fail path cannot balance a stdcall stack, but it is unreachable: the system DLL
   always resolves. */
__declspec(naked) void ProxyThunk(void)
{
    __asm {
        push eax
        call ProxyResolve
        test eax, eax
        jz fail
        jmp eax
    fail:
        xor eax, eax
        ret
    }
}

static DWORD WINAPI InitThread(LPVOID unused)
{
    (void)unused;

    SpeedCoreStart(g_self);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    HANDLE thread;

    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_self = (HMODULE)instance;
        DisableThreadLibraryCalls(instance);
        /* Init reads speed.toml and resolves imports, so keep it off the loader lock. */
        thread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
