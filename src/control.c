#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "common.h"

static int SendCommand(const char *request, char *response, DWORD responseBytes)
{
    HANDLE pipe;
    DWORD written;
    DWORD received;
    BOOL ok;

    pipe = CreateFileW(FTLSPEED_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
        WaitNamedPipeW(FTLSPEED_PIPE_NAME, 2000);
        pipe = CreateFileW(FTLSPEED_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return 0;

    received = 0;
    ok = WriteFile(pipe, request, (DWORD)strlen(request), &written, NULL);
    if (ok)
        ok = ReadFile(pipe, response, responseBytes - 1, &received, NULL);
    CloseHandle(pipe);
    if (!ok)
        return 0;

    response[received] = 0;
    while (received > 0 && (unsigned char)response[received - 1] <= ' ')
        response[--received] = 0;
    return 1;
}

static int RunCommand(const char *request)
{
    char response[FTLSPEED_PIPE_BUF];

    if (!SendCommand(request, response, sizeof(response))) {
        printf("not connected - FTL is not running, or dbghelp.dll did not load\n");
        return 0;
    }
    printf("%s\n", response);
    return 1;
}

static char *TrimLine(char *line)
{
    size_t length;

    while (*line && (unsigned char)*line <= ' ')
        line++;
    length = strlen(line);
    while (length > 0 && (unsigned char)line[length - 1] <= ' ')
        line[--length] = 0;
    return line;
}

static void Repl(void)
{
    char line[128];
    char request[FTLSPEED_PIPE_BUF];
    char *command;

    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            return;

        command = TrimLine(line);
        if (command[0] == 0)
            continue;

        if (_stricmp(command, "q") == 0 || _stricmp(command, "quit") == 0)
            return;
        else if (strcmp(command, "+") == 0)
            strcpy(request, "UP");
        else if (strcmp(command, "-") == 0)
            strcpy(request, "DOWN");
        else if (_stricmp(command, "t") == 0)
            strcpy(request, "TOGGLE");
        else if (_stricmp(command, "r") == 0)
            strcpy(request, "RESET");
        else if (_stricmp(command, "stats") == 0)
            strcpy(request, "STATS");
        else if ((command[0] >= '0' && command[0] <= '9') || command[0] == '.')
            sprintf(request, "SET %.24s", command);
        else {
            printf("unknown command '%s'\n", command);
            continue;
        }

        RunCommand(request);
    }
}

int main(int argc, char **argv)
{
    char request[FTLSPEED_PIPE_BUF];
    char response[FTLSPEED_PIPE_BUF];

    if (argc == 3 && strcmp(argv[1], "--speed") == 0) {
        sprintf(request, "SET %.24s", argv[2]);
        return RunCommand(request) ? 0 : 1;
    }
    if (argc != 1) {
        printf("usage: ftlspeed-dbg [--speed <multiplier>]\n");
        return 2;
    }

    if (!SendCommand("GET", response, sizeof(response))) {
        printf("not connected - FTL is not running, or dbghelp.dll did not load\n");
        return 1;
    }
    printf("ftlspeed-dbg - %s\n", response);
    printf("<number> set, + faster, - slower, t toggle, r reset, stats, q quit\n");
    Repl();
    return 0;
}
