#include "common.h"
#include "overlay.h"

#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FRAMEBUFFER_X               0x8D40
#define GL_DRAW_FRAMEBUFFER_X          0x8CA9
#define GL_READ_FRAMEBUFFER_X          0x8CA8
#define GL_DRAW_FRAMEBUFFER_BINDING_X  0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING_X  0x8CAA
#define GL_CURRENT_PROGRAM_X           0x8B8D
#define GL_ACTIVE_TEXTURE_X            0x84E0
#define GL_TEXTURE0_X                  0x84C0

#define DEFAULT_FRACTION_X 0.289
#define DEFAULT_FRACTION_Y 0.917
#define MIN_SCALE 1
#define MAX_SCALE 4
#define EDIT_MAX  12

typedef void (APIENTRY *PFN_v_e)(GLenum);
typedef void (APIENTRY *PFN_v_v)(void);
typedef void (APIENTRY *PFN_color)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_vertex)(GLfloat, GLfloat);
typedef void (APIENTRY *PFN_blend)(GLenum, GLenum);
typedef void (APIENTRY *PFN_pushattrib)(GLbitfield);
typedef void (APIENTRY *PFN_ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
typedef void (APIENTRY *PFN_getint)(GLenum, GLint *);
typedef GLenum (APIENTRY *PFN_geterror)(void);
typedef void (APIENTRY *PFN_raster)(GLint, GLint);
typedef void (APIENTRY *PFN_listbase)(GLuint);
typedef void (APIENTRY *PFN_calllists)(GLsizei, GLenum, const GLvoid *);
typedef GLuint (APIENTRY *PFN_genlists)(GLsizei);
typedef void (APIENTRY *PFN_deletelists)(GLuint, GLsizei);
typedef void (APIENTRY *PFN_viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_bindfb)(GLenum, GLuint);
typedef void (APIENTRY *PFN_useprogram)(GLuint);
typedef BOOL (WINAPI *PFN_usefontbitmaps)(HDC, DWORD, DWORD, DWORD);
typedef PROC (WINAPI *PFN_wglgetproc)(LPCSTR);

static PFN_v_e pglEnable, pglDisable, pglMatrixMode, pglBegin;
static PFN_v_v pglEnd, pglPushMatrix, pglPopMatrix, pglLoadIdentity, pglPopAttrib;
static PFN_color pglColor4f;
static PFN_vertex pglVertex2f;
static PFN_blend pglBlendFunc;
static PFN_pushattrib pglPushAttrib;
static PFN_ortho pglOrtho;
static PFN_getint pglGetIntegerv;
static PFN_geterror pglGetError;
static PFN_raster pglRasterPos2i;
static PFN_listbase pglListBase;
static PFN_calllists pglCallLists;
static PFN_genlists pglGenLists;
static PFN_deletelists pglDeleteLists;
static PFN_viewport pglViewport;
static PFN_bindfb pglBindFramebuffer;
static PFN_useprogram pglUseProgram;
static PFN_v_e pglActiveTexture;
static PFN_usefontbitmaps pwglUseFontBitmaps;
static PFN_wglgetproc pwglGetProcAddress;

typedef struct { int x, y, w, h; } Rect;
typedef struct { GLuint base; int charW, charH, ascent; } Font;

typedef struct {
    Rect panel, header, expander, minus, plus, field, set, grip;
    int titleY, bind1Y, bind2Y, rowY;
} Layout;

static const SpeedConfig *g_cfg;
static OverlayHost g_host;
static wchar_t g_statePath[MAX_PATH + 40];

static HWND g_window;
static WNDPROC g_originalProc;

static int g_glReady, g_failed, g_coreFramebuffer;
static Font g_title, g_body;
static int g_fontScale;

static int g_visible, g_expanded;
static int g_scale = 1;
static int g_anchorRight, g_anchorBottom = 1;
static int g_offsetX, g_offsetY;
static int g_havePosition;

static Layout g_layout;
static int g_mouseX, g_mouseY, g_mouseOverPanel;
static int g_lastButton;
static int g_dragging, g_resizing;
static int g_dragDX, g_dragDY;
static int g_resizeStartX, g_resizeStartScale;
static int g_editing;
static char g_edit[EDIT_MAX];
static unsigned char g_keyHeld[32];
static int g_autoScale;
static int g_checkedError;

/* ---------- GL resolution ---------- */

static int ResolveGl(void)
{
    HMODULE gl;

    if (g_glReady)
        return 1;
    gl = GetModuleHandleA("opengl32.dll");
    if (!gl)
        return 0;

#define GET(var, name) \
    var = (void *)GetProcAddress(gl, name); \
    if (!var) return 0

    GET(pglEnable, "glEnable");
    GET(pglDisable, "glDisable");
    GET(pglMatrixMode, "glMatrixMode");
    GET(pglBegin, "glBegin");
    GET(pglEnd, "glEnd");
    GET(pglPushMatrix, "glPushMatrix");
    GET(pglPopMatrix, "glPopMatrix");
    GET(pglLoadIdentity, "glLoadIdentity");
    GET(pglPopAttrib, "glPopAttrib");
    GET(pglColor4f, "glColor4f");
    GET(pglVertex2f, "glVertex2f");
    GET(pglBlendFunc, "glBlendFunc");
    GET(pglPushAttrib, "glPushAttrib");
    GET(pglOrtho, "glOrtho");
    GET(pglGetIntegerv, "glGetIntegerv");
    GET(pglGetError, "glGetError");
    GET(pglRasterPos2i, "glRasterPos2i");
    GET(pglListBase, "glListBase");
    GET(pglCallLists, "glCallLists");
    GET(pglGenLists, "glGenLists");
    GET(pglDeleteLists, "glDeleteLists");
    GET(pglViewport, "glViewport");
    GET(pwglUseFontBitmaps, "wglUseFontBitmapsA");
    GET(pwglGetProcAddress, "wglGetProcAddress");
#undef GET

    /* Optional: FTL renders through an FBO, and may leave a shader bound. */
    /* The EXT entry point only accepts GL_FRAMEBUFFER, so remember which one we got. */
    pglBindFramebuffer = (PFN_bindfb)pwglGetProcAddress("glBindFramebuffer");
    g_coreFramebuffer = pglBindFramebuffer != NULL;
    if (!pglBindFramebuffer)
        pglBindFramebuffer = (PFN_bindfb)pwglGetProcAddress("glBindFramebufferEXT");
    pglUseProgram = (PFN_useprogram)pwglGetProcAddress("glUseProgram");
    pglActiveTexture = (PFN_v_e)pwglGetProcAddress("glActiveTexture");

    g_glReady = 1;
    return 1;
}

/* ---------- fonts ---------- */

static int BuildFont(HDC dc, Font *font, int pixelHeight)
{
    HFONT created, previous;
    TEXTMETRICA metrics;

    if (font->base)
        pglDeleteLists(font->base, 96);
    font->base = pglGenLists(96);
    if (!font->base)
        return 0;

    created = CreateFontA(-pixelHeight, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                          FIXED_PITCH | FF_MODERN, "Consolas");
    if (!created)
        return 0;

    previous = (HFONT)SelectObject(dc, created);
    if (!pwglUseFontBitmaps(dc, 32, 96, font->base)) {
        SelectObject(dc, previous);
        DeleteObject(created);
        return 0;
    }
    GetTextMetricsA(dc, &metrics);
    font->charW = metrics.tmAveCharWidth;
    font->charH = metrics.tmHeight;
    font->ascent = metrics.tmAscent;
    SelectObject(dc, previous);
    DeleteObject(created);
    return font->charW > 0;
}

static int BuildFonts(HDC dc)
{
    if (!BuildFont(dc, &g_title, 20 * g_scale))
        return 0;
    if (!BuildFont(dc, &g_body, 13 * g_scale))
        return 0;
    g_fontScale = g_scale;
    return 1;
}

/* ---------- persisted position ---------- */

static void LoadState(void)
{
    FILE *file = NULL;
    char line[128];

    if (_wfopen_s(&file, g_statePath, L"r") != 0 || !file)
        return;
    while (fgets(line, sizeof(line), file)) {
        int value;
        if (sscanf(line, "anchor_right = %d", &value) == 1) g_anchorRight = value != 0;
        else if (sscanf(line, "anchor_bottom = %d", &value) == 1) g_anchorBottom = value != 0;
        else if (sscanf(line, "x = %d", &value) == 1) { g_offsetX = value; g_havePosition = 1; }
        else if (sscanf(line, "y = %d", &value) == 1) g_offsetY = value;
        else if (sscanf(line, "scale = %d", &value) == 1) { g_scale = value; g_autoScale = 0; }
        else if (sscanf(line, "expanded = %d", &value) == 1) g_expanded = value != 0;
    }
    fclose(file);
    if (g_scale < MIN_SCALE) g_scale = MIN_SCALE;
    if (g_scale > MAX_SCALE) g_scale = MAX_SCALE;
}

static void SaveState(void)
{
    FILE *file = NULL;

    if (_wfopen_s(&file, g_statePath, L"w") != 0 || !file)
        return;
    fprintf(file, "# Written by FTL Speed Mod. Safe to delete.\n");
    fprintf(file, "anchor_right = %d\n", g_anchorRight);
    fprintf(file, "anchor_bottom = %d\n", g_anchorBottom);
    fprintf(file, "x = %d\n", g_offsetX);
    fprintf(file, "y = %d\n", g_offsetY);
    fprintf(file, "scale = %d\n", g_scale);
    fprintf(file, "expanded = %d\n", g_expanded);
    fclose(file);
}

/* ---------- layout ---------- */

static void SpeedText(char *out, size_t size, const OverlayStatus *status)
{
    if (status->turboHeld)
        sprintf_s(out, size, "%.2fx TURBO", status->effectiveSpeed);
    else
        sprintf_s(out, size, "%.2fx", status->effectiveSpeed);
}

static void BindText(char *line1, size_t size1, char *line2, size_t size2)
{
    char slower[16], faster[16], toggle[16], turbo[16], panel[16];

    ConfigKeyName(g_cfg->slowerKey, slower, sizeof(slower));
    ConfigKeyName(g_cfg->fasterKey, faster, sizeof(faster));
    ConfigKeyName(g_cfg->toggleKey, toggle, sizeof(toggle));
    ConfigKeyName(g_cfg->turboKey, turbo, sizeof(turbo));
    ConfigKeyName(g_cfg->overlayKey, panel, sizeof(panel));
    sprintf_s(line1, size1, "%s%s speed  %s toggle", slower, faster, toggle);
    sprintf_s(line2, size2, "%s turbo  %s panel", turbo, panel);
}

static int TextWidth(const Font *font, const char *text)
{
    return (int)strlen(text) * font->charW;
}

static void ComputeLayout(int clientW, int clientH, const OverlayStatus *status, Layout *out)
{
    char bind1[64], bind2[64];
    int pad = 5 * g_scale;
    int gap = 3 * g_scale;
    int buttonW = g_body.charH * 2;
    int fieldW = g_body.charW * 8 + gap * 2;
    int setW = g_body.charW * 5;
    int contentW, contentH, y;

    (void)status;
    BindText(bind1, sizeof(bind1), bind2, sizeof(bind2));

    /* Measured against the widest possible readout so the panel never resizes
       when turbo engages or the speed gains a digit. */
    contentW = TextWidth(&g_title, "00.00x TURBO") + gap + g_title.charH;
    if (TextWidth(&g_body, bind1) > contentW) contentW = TextWidth(&g_body, bind1);
    if (TextWidth(&g_body, bind2) > contentW) contentW = TextWidth(&g_body, bind2);
    if (g_expanded && buttonW * 2 + fieldW + setW + gap * 3 > contentW)
        contentW = buttonW * 2 + fieldW + setW + gap * 3;
    contentW = contentW * 5 / 4;

    y = pad;
    out->titleY = y;
    y += g_title.charH + gap;
    out->bind1Y = y; y += g_body.charH;
    out->bind2Y = y; y += g_body.charH;
    if (g_expanded) {
        y += gap;
        out->rowY = y;
        y += g_body.charH + gap * 2;
    } else {
        out->rowY = -1;
    }
    contentH = y - pad;

    out->panel.w = contentW + pad * 2;
    out->panel.h = contentH + pad * 2;

    if (!g_havePosition) {
        g_anchorRight = 0;
        g_anchorBottom = 1;
        g_offsetX = (int)(DEFAULT_FRACTION_X * clientW);
        g_offsetY = clientH - (int)(DEFAULT_FRACTION_Y * clientH) - out->panel.h;
        if (g_offsetY < 0) g_offsetY = 0;
        g_havePosition = 1;
    }

    out->panel.x = g_anchorRight ? clientW - g_offsetX - out->panel.w : g_offsetX;
    out->panel.y = g_anchorBottom ? clientH - g_offsetY - out->panel.h : g_offsetY;
    if (out->panel.x > clientW - out->panel.w) out->panel.x = clientW - out->panel.w;
    if (out->panel.y > clientH - out->panel.h) out->panel.y = clientH - out->panel.h;
    if (out->panel.x < 0) out->panel.x = 0;
    if (out->panel.y < 0) out->panel.y = 0;

    out->titleY += out->panel.y;
    out->bind1Y += out->panel.y;
    out->bind2Y += out->panel.y;
    if (out->rowY >= 0) out->rowY += out->panel.y;

    out->header.x = out->panel.x;
    out->header.y = out->panel.y;
    out->header.w = out->panel.w;
    out->header.h = g_title.charH + pad;

    out->expander.w = g_title.charH;
    out->expander.h = g_title.charH;
    out->expander.x = out->panel.x + out->panel.w - pad - out->expander.w;
    out->expander.y = out->titleY;

    out->minus.x = out->panel.x + pad;
    out->minus.y = out->rowY >= 0 ? out->rowY : 0;
    out->minus.w = buttonW;
    out->minus.h = g_body.charH;
    out->plus = out->minus;
    out->plus.x = out->minus.x + buttonW + gap;
    out->field = out->minus;
    out->field.x = out->plus.x + buttonW + gap;
    out->field.w = fieldW;
    out->set = out->minus;
    out->set.x = out->field.x + fieldW + gap;
    out->set.w = setW;

    out->grip.w = 4 * g_scale;
    out->grip.h = 4 * g_scale;
    out->grip.x = out->panel.x + out->panel.w - out->grip.w;
    out->grip.y = out->panel.y + out->panel.h - out->grip.h;
}

static int Inside(const Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ---------- drawing ---------- */

static void Quad(float x, float y, float w, float h)
{
    pglVertex2f(x, y);
    pglVertex2f(x + w, y);
    pglVertex2f(x + w, y + h);
    pglVertex2f(x, y + h);
}

static void Fill(const Rect *r, float cr, float cg, float cb, float ca)
{
    pglColor4f(cr, cg, cb, ca);
    pglBegin(GL_QUADS);
    Quad((float)r->x, (float)r->y, (float)r->w, (float)r->h);
    pglEnd();
}

static void Border(const Rect *r, int thickness, float cr, float cg, float cb, float ca)
{
    float x = (float)r->x, y = (float)r->y, w = (float)r->w, h = (float)r->h;
    float t = (float)thickness;

    pglColor4f(cr, cg, cb, ca);
    pglBegin(GL_QUADS);
    Quad(x, y, w, t);
    Quad(x, y + h - t, w, t);
    Quad(x, y, t, h);
    Quad(x + w - t, y, t, h);
    pglEnd();
}

static void Text(const Font *font, int x, int y, const char *s,
                 float cr, float cg, float cb, float ca)
{
    pglColor4f(cr, cg, cb, ca);
    pglRasterPos2i(x, y + font->ascent);
    pglListBase(font->base - 32);
    pglCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

static void Arrow(float x, float y, float size, float cr, float cg, float cb, float ca)
{
    pglColor4f(cr, cg, cb, ca);
    pglBegin(GL_TRIANGLES);
    pglVertex2f(x, y);
    pglVertex2f(x, y + 15.0f * size);
    pglVertex2f(x + 10.0f * size, y + 10.5f * size);
    pglEnd();
}

/* FTL's own cursor is behind the panel, so the overlay draws its own on top. */
static void DrawCursor(void)
{
    float size = (float)g_scale;

    Arrow((float)g_mouseX - 1.5f, (float)g_mouseY - 1.5f, size * 1.22f, 0.0f, 0.0f, 0.0f, 0.85f);
    Arrow((float)g_mouseX, (float)g_mouseY, size, 1.0f, 1.0f, 1.0f, 1.0f);
}

static void Button(const Rect *r, const char *label, int hot)
{
    if (hot)
        Fill(r, 0.25f, 0.45f, 0.45f, 0.95f);
    else
        Fill(r, 0.10f, 0.16f, 0.18f, 0.95f);
    Border(r, 1, 0.45f, 0.80f, 0.80f, 0.90f);
    Text(&g_body, r->x + (r->w - TextWidth(&g_body, label)) / 2, r->y, label,
         0.85f, 1.0f, 1.0f, 1.0f);
}

/* ---------- input ---------- */

static int EdgePressed(int vk, int slot)
{
    int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    int pressed = down && !g_keyHeld[slot];

    g_keyHeld[slot] = (unsigned char)down;
    return pressed;
}

static void CommitEdit(void)
{
    char *end;
    double value;

    if (g_edit[0]) {
        value = strtod(g_edit, &end);
        if (end != g_edit)
            g_host.setSpeed(value);
    }
    g_editing = 0;
    g_edit[0] = 0;
}

static void UpdateEditKeys(void)
{
    static const int digits[10] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    size_t length = strlen(g_edit);
    int i;

    for (i = 0; i < 10; i++) {
        /* Both slots must be polled every tick, so no short-circuit here. */
        int row = EdgePressed(digits[i], i);
        int pad = EdgePressed(VK_NUMPAD0 + i, i + 10);
        if ((row || pad) && length < EDIT_MAX - 1) {
            g_edit[length++] = (char)('0' + i);
            g_edit[length] = 0;
        }
    }
    {
        int row = EdgePressed(VK_OEM_PERIOD, 20);
        int pad = EdgePressed(VK_DECIMAL, 21);
        if ((row || pad) && length < EDIT_MAX - 1 && !strchr(g_edit, '.')) {
            g_edit[length++] = '.';
            g_edit[length] = 0;
        }
    }
    if (EdgePressed(VK_BACK, 22) && length > 0)
        g_edit[length - 1] = 0;
    /* Applying is deliberately button-only, so Enter does nothing here. */
    if (EdgePressed(VK_ESCAPE, 24)) {
        g_editing = 0;
        g_edit[0] = 0;
    }
}

static void StoreAnchor(int clientW, int clientH, const Rect *panel)
{
    g_anchorRight = (panel->x + panel->w / 2) > clientW / 2;
    g_anchorBottom = (panel->y + panel->h / 2) > clientH / 2;
    g_offsetX = g_anchorRight ? clientW - (panel->x + panel->w) : panel->x;
    g_offsetY = g_anchorBottom ? clientH - (panel->y + panel->h) : panel->y;
    if (g_offsetX < 0) g_offsetX = 0;
    if (g_offsetY < 0) g_offsetY = 0;
}

static void UpdateInput(HWND window, int clientW, int clientH)
{
    POINT cursor;
    int button, pressed, released;

    if (GetForegroundWindow() != window) {
        g_lastButton = 0;
        g_mouseOverPanel = 0;
        return;
    }

    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    g_mouseX = cursor.x;
    g_mouseY = cursor.y;
    g_mouseOverPanel = Inside(&g_layout.panel, g_mouseX, g_mouseY);

    button = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    pressed = button && !g_lastButton;
    released = !button && g_lastButton;
    g_lastButton = button;

    if (g_editing)
        UpdateEditKeys();

    if (g_dragging) {
        Rect moved = g_layout.panel;
        moved.x = g_mouseX - g_dragDX;
        moved.y = g_mouseY - g_dragDY;
        if (moved.x < 0) moved.x = 0;
        if (moved.y < 0) moved.y = 0;
        if (moved.x > clientW - moved.w) moved.x = clientW - moved.w;
        if (moved.y > clientH - moved.h) moved.y = clientH - moved.h;
        StoreAnchor(clientW, clientH, &moved);
        if (released) {
            g_dragging = 0;
            SaveState();
        }
        return;
    }

    if (g_resizing) {
        int delta = (g_mouseX - g_resizeStartX) / (40);
        int wanted = g_resizeStartScale + delta;

        if (wanted < MIN_SCALE) wanted = MIN_SCALE;
        if (wanted > MAX_SCALE) wanted = MAX_SCALE;
        g_scale = wanted;
        if (released) {
            g_resizing = 0;
            SaveState();
        }
        return;
    }

    if (!pressed)
        return;

    if (Inside(&g_layout.grip, g_mouseX, g_mouseY)) {
        g_resizing = 1;
        g_resizeStartX = g_mouseX;
        g_resizeStartScale = g_scale;
        return;
    }
    if (Inside(&g_layout.expander, g_mouseX, g_mouseY)) {
        g_expanded = !g_expanded;
        if (!g_expanded) {
            g_editing = 0;
            g_edit[0] = 0;
        }
        SaveState();
        return;
    }
    if (g_expanded && Inside(&g_layout.minus, g_mouseX, g_mouseY)) {
        g_host.stepPreset(-1);
        return;
    }
    if (g_expanded && Inside(&g_layout.plus, g_mouseX, g_mouseY)) {
        g_host.stepPreset(1);
        return;
    }
    if (g_expanded && Inside(&g_layout.set, g_mouseX, g_mouseY)) {
        if (g_editing)
            CommitEdit();
        return;
    }
    if (g_expanded && Inside(&g_layout.field, g_mouseX, g_mouseY)) {
        g_editing = 1;
        g_edit[0] = 0;
        memset(g_keyHeld, 0, sizeof(g_keyHeld));
        return;
    }
    if (Inside(&g_layout.header, g_mouseX, g_mouseY)) {
        g_dragging = 1;
        g_dragDX = g_mouseX - g_layout.panel.x;
        g_dragDY = g_mouseY - g_layout.panel.y;
        return;
    }
    if (!g_mouseOverPanel && g_editing) {
        g_editing = 0;
        g_edit[0] = 0;
    }
}

/* ---------- frame ---------- */

/* glPushAttrib predates shaders, FBOs and texture units, so none of this is covered by it.
   Leaving any of it changed is what blanks the game on the following frame. */
static GLint g_previousDrawFbo, g_previousReadFbo;
static GLint g_previousProgram, g_previousTextureUnit;

static void BeginGl(int w, int h)
{
    if (pglBindFramebuffer) {
        pglGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_X, &g_previousDrawFbo);
        if (g_coreFramebuffer)
            pglGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_X, &g_previousReadFbo);
        pglBindFramebuffer(GL_FRAMEBUFFER_X, 0);
    }
    if (pglUseProgram) {
        pglGetIntegerv(GL_CURRENT_PROGRAM_X, &g_previousProgram);
        pglUseProgram(0);
    }
    if (pglActiveTexture) {
        pglGetIntegerv(GL_ACTIVE_TEXTURE_X, &g_previousTextureUnit);
        pglActiveTexture(GL_TEXTURE0_X);
    }

    pglPushAttrib(GL_ALL_ATTRIB_BITS);
    pglViewport(0, 0, w, h);
    pglMatrixMode(GL_PROJECTION);
    pglPushMatrix();
    pglLoadIdentity();
    pglOrtho(0, w, h, 0, -1, 1);
    pglMatrixMode(GL_MODELVIEW);
    pglPushMatrix();
    pglLoadIdentity();
    pglDisable(GL_DEPTH_TEST);
    pglDisable(GL_TEXTURE_2D);
    pglDisable(GL_LIGHTING);
    pglDisable(GL_CULL_FACE);
    pglDisable(GL_SCISSOR_TEST);
    pglEnable(GL_BLEND);
    pglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void EndGl(void)
{
    pglMatrixMode(GL_PROJECTION);
    pglPopMatrix();
    pglMatrixMode(GL_MODELVIEW);
    pglPopMatrix();
    pglPopAttrib();

    if (pglActiveTexture)
        pglActiveTexture((GLenum)g_previousTextureUnit);
    if (pglUseProgram)
        pglUseProgram((GLuint)g_previousProgram);
    if (pglBindFramebuffer) {
        if (g_coreFramebuffer) {
            pglBindFramebuffer(GL_DRAW_FRAMEBUFFER_X, (GLuint)g_previousDrawFbo);
            pglBindFramebuffer(GL_READ_FRAMEBUFFER_X, (GLuint)g_previousReadFbo);
        } else {
            pglBindFramebuffer(GL_FRAMEBUFFER_X, (GLuint)g_previousDrawFbo);
        }
    }
}

static void DrawPanel(const OverlayStatus *status)
{
    char speed[32], bind1[64], bind2[64], shown[EDIT_MAX + 2];
    const Layout *l = &g_layout;
    int pad = 5 * g_scale;

    SpeedText(speed, sizeof(speed), status);
    BindText(bind1, sizeof(bind1), bind2, sizeof(bind2));

    Fill(&l->panel, 0.04f, 0.07f, 0.08f, 0.82f);
    Border(&l->panel, 1, 0.45f, 0.80f, 0.80f, 0.85f);

    if (status->turboHeld)
        Text(&g_title, l->panel.x + pad, l->titleY, speed, 1.0f, 0.85f, 0.35f, 1.0f);
    else
        Text(&g_title, l->panel.x + pad, l->titleY, speed, 0.60f, 1.0f, 0.85f, 1.0f);

    Text(&g_body, l->panel.x + pad, l->bind1Y, bind1, 0.60f, 0.68f, 0.70f, 1.0f);
    Text(&g_body, l->panel.x + pad, l->bind2Y, bind2, 0.60f, 0.68f, 0.70f, 1.0f);

    Button(&l->expander, g_expanded ? "^" : "v",
           Inside(&l->expander, g_mouseX, g_mouseY));

    if (g_expanded) {
        Button(&l->minus, "-", Inside(&l->minus, g_mouseX, g_mouseY));
        Button(&l->plus, "+", Inside(&l->plus, g_mouseX, g_mouseY));

        Fill(&l->field, 0.02f, 0.03f, 0.04f, 0.95f);
        Border(&l->field, 1, g_editing ? 1.0f : 0.45f, 0.80f, 0.80f, 0.90f);
        if (g_editing)
            sprintf_s(shown, sizeof(shown), "%s_", g_edit);
        else
            sprintf_s(shown, sizeof(shown), "%.2f", status->baseSpeed);
        Text(&g_body, l->field.x + 3 * g_scale, l->field.y, shown, 0.90f, 1.0f, 1.0f, 1.0f);

        Button(&l->set, "set", g_editing && Inside(&l->set, g_mouseX, g_mouseY));
    }

    Fill(&l->grip, 0.45f, 0.80f, 0.80f, 0.75f);

    if (g_mouseOverPanel || g_dragging || g_resizing)
        DrawCursor();
}

void OverlayOnSwapBuffers(HDC dc)
{
    OverlayStatus status;
    HWND window;
    RECT client;
    int w, h;

    if (g_failed || !g_visible || !g_cfg)
        return;
    if (!ResolveGl()) {
        g_failed = 1;
        return;
    }
    window = WindowFromDC(dc);
    if (!window)
        return;
    if (!GetClientRect(window, &client))
        return;
    w = client.right;
    h = client.bottom;
    if (w <= 0 || h <= 0)
        return;

    if (g_autoScale) {
        g_scale = h / 720;
        if (g_scale < MIN_SCALE) g_scale = MIN_SCALE;
        if (g_scale > MAX_SCALE) g_scale = MAX_SCALE;
        g_autoScale = 0;
    }

    if (!g_fontScale || g_fontScale != g_scale) {
        if (!BuildFonts(dc)) {
            g_failed = 1;
            return;
        }
    }

    g_host.readStatus(&status);
    ComputeLayout(w, h, &status, &g_layout);
    UpdateInput(window, w, h);
    ComputeLayout(w, h, &status, &g_layout);

    if (!g_checkedError)
        while (pglGetError() != GL_NO_ERROR)
            ;

    BeginGl(w, h);
    DrawPanel(&status);
    EndGl();

    /* Immediate mode failing means a core profile context - disable instead of spamming. */
    if (!g_checkedError) {
        g_checkedError = 1;
        if (pglGetError() != GL_NO_ERROR)
            g_failed = 1;
    }
}

/* ---------- window ---------- */

static LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (g_visible && !g_failed) {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEWHEEL:
            if (g_mouseOverPanel || g_dragging || g_resizing)
                return 0;
            break;
        case WM_INPUT: {
            /* Movement must reach FTL or its own cursor freezes where it entered the
               panel. Only the button events are withheld. */
            RAWINPUT raw;
            UINT size = sizeof(raw);

            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &size,
                                sizeof(RAWINPUTHEADER)) == (UINT)-1)
                break;
            if (raw.header.dwType == RIM_TYPEKEYBOARD && g_editing)
                return 0;
            if (raw.header.dwType == RIM_TYPEMOUSE && raw.data.mouse.usButtonFlags != 0 &&
                (g_mouseOverPanel || g_dragging || g_resizing))
                return 0;
            break;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (g_editing)
                return 0;
            break;
        default:
            break;
        }
    }
    return CallWindowProcW(g_originalProc, window, message, wp, lp);
}

void OverlayAttachWindow(HWND window)
{
    if (!window || window == g_window)
        return;
    g_window = window;
    g_originalProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrW(window, GWLP_WNDPROC,
                                                          (LONG_PTR)OverlayWndProc);
}

void OverlayToggle(void)
{
    g_visible = !g_visible;
    if (!g_visible) {
        g_editing = 0;
        g_dragging = 0;
        g_resizing = 0;
        g_edit[0] = 0;
    }
}

int OverlayCapturesKeyboard(void)
{
    return g_editing;
}

void OverlayInit(const SpeedConfig *cfg, const wchar_t *stateDir, const OverlayHost *host)
{
    g_cfg = cfg;
    g_host = *host;
    g_visible = cfg->overlay;
    g_scale = cfg->overlayScale;
    g_autoScale = cfg->overlayScale <= 0;

    swprintf_s(g_statePath, MAX_PATH + 40, L"%slastwindowpos.toml", stateDir);
    LoadState();

    if (!g_autoScale) {
        if (g_scale < MIN_SCALE)
            g_scale = MIN_SCALE;
        if (g_scale > MAX_SCALE)
            g_scale = MAX_SCALE;
    }
}
