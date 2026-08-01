#ifndef FTLSPEED_IAT_H
#define FTLSPEED_IAT_H

#include <windows.h>

/* Patches every import thunk in `module` whose name matches `functionName`,
   regardless of which DLL declares it. Returns the previous target, or NULL
   if no matching import was found. */
void *IatHook(HMODULE module, const char *functionName, void *replacement);

#endif
