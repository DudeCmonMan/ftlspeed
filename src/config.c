#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ConfigDefaults(SpeedConfig *cfg)
{
    static const double defaults[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0 };
    int i;

    cfg->speed = 1.0;
    cfg->turboSpeed = 2.0;
    cfg->showInTitle = 1;
    cfg->turboKey = VK_OEM_3;   /* ` */
    cfg->fasterKey = VK_OEM_6;  /* ] */
    cfg->slowerKey = VK_OEM_4;  /* [ */
    cfg->toggleKey = VK_OEM_5;  /* \ */
    cfg->overlay = 0;
    cfg->overlayKey = VK_INSERT;
    cfg->overlayScale = 0;      /* 0 = derive from resolution */
    cfg->overlayOpacity = 0.50;
    cfg->presetCount = (int)(sizeof(defaults) / sizeof(defaults[0]));
    for (i = 0; i < cfg->presetCount; i++)
        cfg->presets[i] = defaults[i];
}

static char *Trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = 0;
    return text;
}

static void ParseFlag(const char *value, int *out)
{
    if (strcmp(value, "true") == 0)
        *out = 1;
    else if (strcmp(value, "false") == 0)
        *out = 0;
}

static void ParseNumber(const char *value, double *out)
{
    char *end;
    double parsed = strtod(value, &end);

    if (end != value)
        *out = parsed;
}

static const struct { const char *name; int vk; } keyNames[] = {
    { "PgUp", VK_PRIOR }, { "PgDn", VK_NEXT }, { "Home", VK_HOME }, { "End", VK_END },
    { "Ins", VK_INSERT }, { "Del", VK_DELETE }, { "Tab", VK_TAB }, { "Space", VK_SPACE },
    { "Backspace", VK_BACK }, { "CapsLock", VK_CAPITAL },
    { "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
    { "LAlt", VK_LMENU }, { "RAlt", VK_RMENU }, { "LCtrl", VK_LCONTROL },
    { "RCtrl", VK_RCONTROL }, { "LShift", VK_LSHIFT }, { "RShift", VK_RSHIFT },
    { "F1", VK_F1 }, { "F2", VK_F2 }, { "F3", VK_F3 }, { "F4", VK_F4 },
    { "F5", VK_F5 }, { "F6", VK_F6 }, { "F7", VK_F7 }, { "F8", VK_F8 },
    { "F9", VK_F9 }, { "F10", VK_F10 }, { "F11", VK_F11 }, { "F12", VK_F12 },
    { "Numpad+", VK_ADD }, { "Numpad-", VK_SUBTRACT },
    { "Numpad*", VK_MULTIPLY }, { "Numpad/", VK_DIVIDE }
};

static void ParseKey(const char *value, int *out)
{
    char text[32];
    size_t length;
    int i;

    if (*value == '"')
        value++;
    strncpy_s(text, sizeof(text), value, _TRUNCATE);
    length = strlen(text);
    while (length > 0 && text[length - 1] == '"')
        text[--length] = 0;
    if (length == 0)
        return;
    if (strcmp(text, "\\\\") == 0) {
        text[1] = 0;
        length = 1;
    }

    for (i = 0; i < (int)(sizeof(keyNames) / sizeof(keyNames[0])); i++) {
        if (_stricmp(text, keyNames[i].name) == 0) {
            *out = keyNames[i].vk;
            return;
        }
    }

    /* Single character: resolve through the active layout so non-US keyboards work. */
    if (length == 1) {
        SHORT scan = VkKeyScanA(text[0]);
        if (scan != -1)
            *out = scan & 0xFF;
    }
}

void ConfigKeyName(int vk, char *out, size_t size)
{
    int i;
    UINT character;

    for (i = 0; i < (int)(sizeof(keyNames) / sizeof(keyNames[0])); i++) {
        if (keyNames[i].vk == vk) {
            strncpy_s(out, size, keyNames[i].name, _TRUNCATE);
            return;
        }
    }
    character = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
    if (character >= 32 && character < 127 && size >= 2) {
        out[0] = (char)character;
        out[1] = 0;
        return;
    }
    strncpy_s(out, size, "?", _TRUNCATE);
}

static void ParseList(const char *value, SpeedConfig *cfg)
{
    int count = 0;

    if (*value != '[')
        return;
    value++;
    while (count < FTLSPEED_MAX_PRESETS) {
        char *end;
        double parsed = strtod(value, &end);
        if (end == value)
            break;
        cfg->presets[count++] = parsed;
        value = end;
        while (*value == ' ' || *value == '\t')
            value++;
        if (*value != ',')
            break;
        value++;
    }
    if (count > 0)
        cfg->presetCount = count;
}

void ConfigLoadToml(SpeedConfig *cfg, const wchar_t *path)
{
    FILE *file = NULL;
    char line[512];

    if (_wfopen_s(&file, path, L"r") != 0 || !file)
        return;

    while (fgets(line, sizeof(line), file)) {
        char *scan = line;
        char *separator;
        char *key;
        char *value;
        int inQuotes = 0;

        /* Quote-aware so a key can be bound to "#". */
        while (*scan) {
            if (*scan == '"')
                inQuotes = !inQuotes;
            else if (*scan == '#' && !inQuotes)
                break;
            scan++;
        }
        *scan = 0;
        separator = strchr(line, '=');
        if (!separator)
            continue;
        *separator = 0;
        key = Trim(line);
        value = Trim(separator + 1);

        if (strcmp(key, "speed") == 0)
            ParseNumber(value, &cfg->speed);
        else if (strcmp(key, "turbo_speed") == 0)
            ParseNumber(value, &cfg->turboSpeed);
        else if (strcmp(key, "show_in_title") == 0)
            ParseFlag(value, &cfg->showInTitle);
        else if (strcmp(key, "presets") == 0)
            ParseList(value, cfg);
        else if (strcmp(key, "turbo_key") == 0)
            ParseKey(value, &cfg->turboKey);
        else if (strcmp(key, "faster_key") == 0)
            ParseKey(value, &cfg->fasterKey);
        else if (strcmp(key, "slower_key") == 0)
            ParseKey(value, &cfg->slowerKey);
        else if (strcmp(key, "toggle_key") == 0)
            ParseKey(value, &cfg->toggleKey);
        else if (strcmp(key, "overlay") == 0)
            ParseFlag(value, &cfg->overlay);
        else if (strcmp(key, "overlay_key") == 0)
            ParseKey(value, &cfg->overlayKey);
        else if (strcmp(key, "overlay_opacity") == 0)
            ParseNumber(value, &cfg->overlayOpacity);
        else if (strcmp(key, "overlay_scale") == 0) {
            double parsed = cfg->overlayScale;
            ParseNumber(value, &parsed);
            cfg->overlayScale = (int)parsed;
        }
    }

    fclose(file);

    /* Negated compare so a NaN in the toml cannot reach glColor4f. */
    if (!(cfg->overlayOpacity >= 0.0))
        cfg->overlayOpacity = 0.0;
    if (cfg->overlayOpacity > 1.0)
        cfg->overlayOpacity = 1.0;
}
