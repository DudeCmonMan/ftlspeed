#include "iat.h"

#include <string.h>

static void PatchSlot(void **slot, void *replacement)
{
    DWORD oldProtect;

    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect))
        return;
    *slot = replacement;
    VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
}

void *IatHook(HMODULE module, const char *functionName, void *replacement)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_IMPORT_DESCRIPTOR *desc;
    void *firstOld = NULL;

    if (!base || !functionName)
        return NULL;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return NULL;

    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress || !dir->Size)
        return NULL;

    for (desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
         desc->Name && desc->FirstThunk;
         desc++) {
        IMAGE_THUNK_DATA32 *slots = (IMAGE_THUNK_DATA32 *)(base + desc->FirstThunk);
        IMAGE_THUNK_DATA32 *names = NULL;
        void *boundTarget = NULL;
        DWORD i;

        if (desc->OriginalFirstThunk) {
            names = (IMAGE_THUNK_DATA32 *)(base + desc->OriginalFirstThunk);
        } else {
            HMODULE dll = GetModuleHandleA((const char *)(base + desc->Name));
            boundTarget = dll ? (void *)GetProcAddress(dll, functionName) : NULL;
            if (!boundTarget)
                continue;
        }

        for (i = 0; slots[i].u1.Function; i++) {
            int matched;

            if (names) {
                IMAGE_IMPORT_BY_NAME *imported;

                if (!names[i].u1.AddressOfData)
                    break;
                if (IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal))
                    continue;
                imported = (IMAGE_IMPORT_BY_NAME *)(base + names[i].u1.AddressOfData);
                matched = strcmp((const char *)imported->Name, functionName) == 0;
            } else {
                matched = (void *)slots[i].u1.Function == boundTarget;
            }

            if (matched) {
                void *old = (void *)slots[i].u1.Function;
                PatchSlot((void **)&slots[i].u1.Function, replacement);
                if (!firstOld)
                    firstOld = old;
            }
        }
    }

    return firstOld;
}
