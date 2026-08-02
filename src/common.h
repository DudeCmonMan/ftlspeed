#ifndef FTLSPEED_COMMON_H
#define FTLSPEED_COMMON_H

#include <windows.h>

#define FTLSPEED_PIPE_NAME   L"\\\\.\\pipe\\ftlspeed"
#define FTLSPEED_PIPE_BUF    512
#define FTLSPEED_MIN_SPEED   0.1
#define FTLSPEED_MAX_SPEED   20.0
#define FTLSPEED_MAX_PRESETS 16

typedef struct {
    double speed;
    double turboSpeed;
    int showInTitle;
    int turboKey;
    int fasterKey;
    int slowerKey;
    int toggleKey;
    int overlay;
    int overlayKey;
    int overlayScale;
    double presets[FTLSPEED_MAX_PRESETS];
    int presetCount;
} SpeedConfig;

void ConfigDefaults(SpeedConfig *cfg);
void ConfigLoadToml(SpeedConfig *cfg, const wchar_t *path);
void ConfigKeyName(int vk, char *out, size_t size);

void SpeedCoreStart(HMODULE self);

/* Pipe protocol, one request line in, one response line out.
   Requests:  GET | SET <float> | UP | DOWN | TOGGLE | RESET | STATS
   Responses: OK <speed> | ERR <message>
              STATS -> OK qpc=<n> tick=<n> tgt=<n> sleep=<n> regressions=<n> hooks=<n>/4 */

#endif
