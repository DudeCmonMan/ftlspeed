#ifndef FTLSPEED_IAT_H
#define FTLSPEED_IAT_H

#include <windows.h>

/* Matches by name whatever DLL declares it, so api-set redirection cannot hide an import. */
void *IatHook(HMODULE module, const char *functionName, void *replacement);

#endif
