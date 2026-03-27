#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define MA_SOUND_DEFINED

#include "Engine.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <math.h>

#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER     0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#  define GL_DYNAMIC_DRAW     0x88E8
#endif

#if !defined(GL_VERSION_1_5)
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif

typedef void (*PFNGLGENBUFFERSPROC)   (GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)   (GLenum,  GLuint);
typedef void (*PFNGLBUFFERDATAPROC)   (GLenum,  GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);

static PFNGLGENBUFFERSPROC    _glGenBuffers    = NULL;
static PFNGLBINDBUFFERPROC    _glBindBuffer    = NULL;
static PFNGLBUFFERDATAPROC    _glBufferData    = NULL;
static PFNGLDELETEBUFFERSPROC _glDeleteBuffers = NULL;

#define glGenBuffers    _glGenBuffers
#define glBindBuffer    _glBindBuffer
#define glBufferData    _glBufferData
#define glDeleteBuffers _glDeleteBuffers

static int _vbo_load_procs(void)
{
    _glGenBuffers    = (PFNGLGENBUFFERSPROC)
        glXGetProcAddressARB((const GLubyte *)"glGenBuffers");
    _glBindBuffer    = (PFNGLBINDBUFFERPROC)
        glXGetProcAddressARB((const GLubyte *)"glBindBuffer");
    _glBufferData    = (PFNGLBUFFERDATAPROC)
        glXGetProcAddressARB((const GLubyte *)"glBufferData");
    _glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        glXGetProcAddressARB((const GLubyte *)"glDeleteBuffers");

    if (!_glGenBuffers || !_glBindBuffer ||
        !_glBufferData  || !_glDeleteBuffers) {
        fprintf(stderr, "Engine: OpenGL VBO não disponível.\n");
        return 0;
    }
    return 1;
}

/* ==============================================================
 * Batch / VBO — tamanho reduzido de 8192 → 4096 quads (-128 KB)
 * ============================================================== */

#define BATCH_MAX_QUADS          4096
#define BATCH_FLOATS_PER_VERTEX  8          /* x y u v r g b a */
#define BATCH_VERTICES_PER_QUAD  4
#define BATCH_FLOATS_PER_QUAD   (BATCH_FLOATS_PER_VERTEX * BATCH_VERTICES_PER_QUAD)
#define BATCH_BUFFER_FLOATS     (BATCH_MAX_QUADS * BATCH_FLOATS_PER_QUAD)

typedef struct {
    GLuint  vbo;
    float   buf[BATCH_BUFFER_FLOATS];
    int     quad_count;
    GLuint  current_tex;
} BatchState;

static BatchState s_batch;
static int        s_batch_ready = 0;
static GLuint     s_last_tex    = 0;

/* Buffers de teclas — swap O(1) */
static int  s_keys_a[ENGINE_MAX_KEYS];
static int  s_keys_b[ENGINE_MAX_KEYS];
static int *s_keys_cur  = s_keys_a;
static int *s_keys_prev = s_keys_b;

/* ==============================================================
 * Timer interno
 * ============================================================== */

static struct timespec s_time_origin = {0, 0};
static struct timespec s_time_prev   = {0, 0};

static double _ts_to_secs(struct timespec t) {
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* ==============================================================
 * Helpers internos
 * ============================================================== */

static inline unsigned long _pack_color(int r, int g, int b) {
    return ((unsigned long)(r & 0xFF) << 16) |
           ((unsigned long)(g & 0xFF) <<  8) |
           ((unsigned long)(b & 0xFF));
}

static inline void _unpack_color(unsigned long c,
                                  float *r, float *g, float *b) {
    *r = (float)((c >> 16) & 0xFF) / 255.0f;
    *g = (float)((c >>  8) & 0xFF) / 255.0f;
    *b = (float)( c        & 0xFF) / 255.0f;
}

static inline float _lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline void _bind_texture(GLuint tex) {
    if (tex != s_last_tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        s_last_tex = tex;
    }
}

/* ---- Mapeamento de teclas ---------------------------------- */

static KeySym _name_to_keysym(const char *key) {
    if (!key) return 0;
    if (strcmp(key, "left")      == 0) return XK_Left;
    if (strcmp(key, "right")     == 0) return XK_Right;
    if (strcmp(key, "up")        == 0) return XK_Up;
    if (strcmp(key, "down")      == 0) return XK_Down;
    if (strcmp(key, "space")     == 0) return XK_space;
    if (strcmp(key, "return")    == 0) return XK_Return;
    if (strcmp(key, "escape")    == 0) return XK_Escape;
    if (strcmp(key, "shift")     == 0) return XK_Shift_L;
    if (strcmp(key, "ctrl")      == 0) return XK_Control_L;
    if (strcmp(key, "alt")       == 0) return XK_Alt_L;
    if (strcmp(key, "tab")       == 0) return XK_Tab;
    if (strcmp(key, "backspace") == 0) return XK_BackSpace;
    if (strcmp(key, "delete")    == 0) return XK_Delete;
    if (strcmp(key, "f1")        == 0) return XK_F1;
    if (strcmp(key, "f2")        == 0) return XK_F2;
    if (strcmp(key, "f3")        == 0) return XK_F3;
    if (strcmp(key, "f4")        == 0) return XK_F4;
    if (strcmp(key, "f5")        == 0) return XK_F5;
    if (strcmp(key, "f6")        == 0) return XK_F6;
    if (strcmp(key, "f7")        == 0) return XK_F7;
    if (strcmp(key, "f8")        == 0) return XK_F8;
    if (strcmp(key, "f9")        == 0) return XK_F9;
    if (strcmp(key, "f10")       == 0) return XK_F10;
    if (strcmp(key, "f11")       == 0) return XK_F11;
    if (strcmp(key, "f12")       == 0) return XK_F12;
    if (strcmp(key, "a")         == 0) return XK_a;
    if (strcmp(key, "d")         == 0) return XK_d;
    if (strcmp(key, "w")         == 0) return XK_w;
    if (strcmp(key, "s")         == 0) return XK_s;
    if (strcmp(key, "e")         == 0) return XK_e;
    if (strcmp(key, "q")         == 0) return XK_q;
    if (strcmp(key, "z")         == 0) return XK_z;
    if (strcmp(key, "x")         == 0) return XK_x;
    if (strcmp(key, "c")         == 0) return XK_c;
    if (strcmp(key, "i")         == 0) return XK_i;
    if (strcmp(key, "j")         == 0) return XK_j;
    if (strcmp(key, "k")         == 0) return XK_k;
    if (strcmp(key, "l")         == 0) return XK_l;
    if (strcmp(key, "m")         == 0) return XK_m;
    if (strcmp(key, "p")         == 0) return XK_p;
    if (strcmp(key, "r")         == 0) return XK_r;
    if (strcmp(key, "t")         == 0) return XK_t;
    if (strcmp(key, "u")         == 0) return XK_u;
    if (strcmp(key, "v")         == 0) return XK_v;
    if (key[0] && !key[1])              return (KeySym)(unsigned char)key[0];
    return 0;
}

/*
 * Monta a tabela keycode→keysym uma única vez no engine_init.
 * engine_key_down/pressed/released passam a ser lookups O(1)
 * sem nenhuma chamada a XDisplayKeycodes ou XKeycodeToKeysym por frame.
 */
static void _build_keysym_cache(Engine *e)
{
    int kc_min, kc_max, kc;
    memset(e->ksym_cache.map, 0, sizeof(e->ksym_cache.map));
    XDisplayKeycodes(e->display, &kc_min, &kc_max);
    for (kc = kc_min; kc <= kc_max; kc++)
        e->ksym_cache.map[kc & (ENGINE_MAX_KEYS - 1)] =
            XKeycodeToKeysym(e->display, (KeyCode)kc, 0);
}

/* ---- Carregamento PNG -------------------------------------- */

static unsigned char *_load_png_rgba(const char *path,
                                      unsigned int *out_w,
                                      unsigned int *out_h)
{
    FILE         *fp;
    png_structp   ps;
    png_infop     pi;
    unsigned int  img_w, img_h, row;
    int           bit_depth, color_type, row_bytes;
    unsigned char *img_data;
    png_bytep     *rows;

    fp = fopen(path, "rb");
    if (!fp) return NULL;

    ps = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!ps) { fclose(fp); return NULL; }

    pi = png_create_info_struct(ps);
    if (!pi) { png_destroy_read_struct(&ps, NULL, NULL); fclose(fp); return NULL; }

    if (setjmp(png_jmpbuf(ps))) {
        png_destroy_read_struct(&ps, &pi, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(ps, fp);
    png_read_info(ps, pi);

    img_w      = png_get_image_width(ps, pi);
    img_h      = png_get_image_height(ps, pi);
    bit_depth  = png_get_bit_depth(ps, pi);
    color_type = png_get_color_type(ps, pi);

    if (bit_depth == 16)                                png_set_strip_16(ps);
    if (color_type == PNG_COLOR_TYPE_PALETTE)           png_set_palette_to_rgb(ps);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
                                                        png_set_expand_gray_1_2_4_to_8(ps);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)        png_set_gray_to_rgb(ps);
    png_set_tRNS_to_alpha(ps);
    png_set_filler(ps, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(ps, pi);

    row_bytes = (int)(img_w * 4);
    img_data  = (unsigned char *)malloc((size_t)img_h * (size_t)row_bytes);
    rows      = (png_bytep *)malloc(img_h * sizeof(png_bytep));
    if (!img_data || !rows) {
        free(img_data); free(rows);
        png_destroy_read_struct(&ps, &pi, NULL);
        fclose(fp);
        return NULL;
    }

    for (row = 0; row < img_h; row++)
        rows[row] = img_data + row * (size_t)row_bytes;

    png_read_image(ps, rows);
    png_destroy_read_struct(&ps, &pi, NULL);
    fclose(fp);
    free(rows);

    *out_w = img_w;
    *out_h = img_h;
    return img_data;
}

/* ---- Cache de PNG (somente para engine_load_sprite_region) -- */

typedef struct {
    char          path[512];
    unsigned char *data;
    unsigned int  w, h;
} PngCache;

static PngCache s_png_cache = { "", NULL, 0, 0 };

static unsigned char *_get_png_cached(const char *path,
                                       unsigned int *out_w,
                                       unsigned int *out_h)
{
    if (s_png_cache.data && strcmp(s_png_cache.path, path) == 0) {
        *out_w = s_png_cache.w;
        *out_h = s_png_cache.h;
        return s_png_cache.data;
    }
    free(s_png_cache.data);
    s_png_cache.data = _load_png_rgba(path, out_w, out_h);
    if (!s_png_cache.data) return NULL;
    strncpy(s_png_cache.path, path, sizeof(s_png_cache.path) - 1);
    s_png_cache.path[sizeof(s_png_cache.path) - 1] = '\0';
    s_png_cache.w = *out_w;
    s_png_cache.h = *out_h;
    return s_png_cache.data;
}

static void _png_cache_clear(void) {
    free(s_png_cache.data);
    s_png_cache.data = NULL;
    s_png_cache.path[0] = '\0';
}

/* ---- Cria textura OpenGL ----------------------------------- */

static GLuint _make_texture(const unsigned char *rgba,
                              unsigned int w, unsigned int h)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    s_last_tex = tex;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return tex;
}

/* ---- Projeção ortográfica ---------------------------------- */

static void _apply_fullscreen_viewport(Engine *e); /* fwd */

static void _setup_projection(Engine *e) {
    int best_scale, vp_w, vp_h, off_x, off_y;

    best_scale = e->win_w / e->render_w;
    if (e->win_h / e->render_h < best_scale)
        best_scale = e->win_h / e->render_h;
    if (best_scale < 1) best_scale = 1;

    vp_w  = e->render_w * best_scale;
    vp_h  = e->render_h * best_scale;
    off_x = (e->win_w - vp_w) / 2;
    off_y = (e->win_h - vp_h) / 2;

    glViewport(off_x, off_y, vp_w, vp_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, e->render_w, e->render_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* ==============================================================
 * Batch / VBO
 * ============================================================== */

static void _batch_init(void) {
    glGenBuffers(1, &s_batch.vbo);
    s_batch.quad_count  = 0;
    s_batch.current_tex = 0;
    s_batch_ready       = 1;
}

static void _batch_flush(void) {
    int vertex_count;

    /* OTIMIZAÇÃO GPU: retorna cedo sem nenhuma chamada GL */
    if (s_batch.quad_count == 0) return;

    vertex_count = s_batch.quad_count * BATCH_VERTICES_PER_QUAD;

    glBindBuffer(GL_ARRAY_BUFFER, s_batch.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertex_count * BATCH_FLOATS_PER_VERTEX * sizeof(float)),
                 s_batch.buf, GL_DYNAMIC_DRAW);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer  (2, GL_FLOAT, BATCH_FLOATS_PER_VERTEX * sizeof(float),
                      (void *)(0 * sizeof(float)));
    glTexCoordPointer(2, GL_FLOAT, BATCH_FLOATS_PER_VERTEX * sizeof(float),
                      (void *)(2 * sizeof(float)));
    glColorPointer   (4, GL_FLOAT, BATCH_FLOATS_PER_VERTEX * sizeof(float),
                      (void *)(4 * sizeof(float)));

    glDrawArrays(GL_QUADS, 0, vertex_count);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    s_batch.quad_count = 0;
}

static void _batch_set_texture(GLuint tex) {
    if (tex != s_batch.current_tex) {
        _batch_flush();
        _bind_texture(tex);
        s_batch.current_tex = tex;
    }
}

static void _batch_push_quad(float dx, float dy, float dw, float dh,
                               float u0, float v0, float u1, float v1,
                               float r,  float g,  float b,  float a)
{
    float *p;

    if (s_batch.quad_count >= BATCH_MAX_QUADS) _batch_flush();

    p = s_batch.buf + s_batch.quad_count * BATCH_FLOATS_PER_QUAD;

    *p++ = dx;     *p++ = dy;     *p++ = u0; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    *p++ = dx+dw;  *p++ = dy;     *p++ = u1; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    *p++ = dx+dw;  *p++ = dy+dh;  *p++ = u1; *p++ = v1; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    *p++ = dx;     *p++ = dy+dh;  *p++ = u0; *p++ = v1; *p++ = r; *p++ = g; *p++ = b; *p++ = a;

    s_batch.quad_count++;
}

static void _batch_push_quad_flip(float dx, float dy, float dw, float dh,
                                    float u0, float v0, float u1, float v1,
                                    int flip_h, int flip_v,
                                    float r, float g, float b, float a)
{
    float fu0 = flip_h ? u1 : u0;
    float fu1 = flip_h ? u0 : u1;
    float fv0 = flip_v ? v1 : v0;
    float fv1 = flip_v ? v0 : v1;
    _batch_push_quad(dx, dy, dw, dh, fu0, fv0, fu1, fv1, r, g, b, a);
}

/*
 * Quad com rotação em torno do centro.
 * Usa glPushMatrix/glPopMatrix.
 * Só chamado quando rotation != 0.
 */
static void _draw_quad_rotated(float dx, float dy, float dw, float dh,
                                 float u0, float v0, float u1, float v1,
                                 float r, float g, float b, float a,
                                 float rotation_deg,
                                 int flip_h, int flip_v)
{
    float cx, cy;
    float fu0, fu1, fv0, fv1;

    fu0 = flip_h ? u1 : u0;
    fu1 = flip_h ? u0 : u1;
    fv0 = flip_v ? v1 : v0;
    fv1 = flip_v ? v0 : v1;

    _batch_flush();

    cx = dx + dw * 0.5f;
    cy = dy + dh * 0.5f;

    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
    glTranslatef(-dw * 0.5f, -dh * 0.5f, 0.0f);

    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glTexCoord2f(fu0, fv0); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(fu1, fv0); glVertex2f(dw,   0.0f);
        glTexCoord2f(fu1, fv1); glVertex2f(dw,   dh);
        glTexCoord2f(fu0, fv1); glVertex2f(0.0f, dh);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glPopMatrix();
}

/* ---- UV helper -------------------------------------------- */

static void _uv_for_tile(const SpriteData *sp,
                          int tile_x, int tile_y,
                          int tile_w, int tile_h,
                          float *u0, float *v0,
                          float *u1, float *v1)
{
    float tw = (float)sp->width;
    float th = (float)sp->height;
    *u0 = (float)tile_x / tw;
    *v0 = (float)tile_y / th;
    *u1 = (float)(tile_x + tile_w) / tw;
    *v1 = (float)(tile_y + tile_h) / th;
}

/* ==============================================================
 * engine_init / engine_destroy
 * ============================================================== */

int engine_init(Engine *e, int width, int height,
                const char *title, int scale)
{
    static int visual_attribs[] = {
        GLX_RGBA, GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 0, None
    };

    XVisualInfo           *vi;
    XSetWindowAttributes   swa;
    unsigned char          white_pixel[4] = {255, 255, 255, 255};

    memset(e, 0, sizeof(Engine));

    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;

    e->render_w = width;
    e->render_h = height;
    e->scale    = scale;
    e->win_w    = width  * scale;
    e->win_h    = height * scale;
    e->depth    = 24;
    e->running  = 1;

    e->camera.zoom    = 1.0f;
    e->camera_enabled = 0;

    e->fade_alpha  = 0.0f;
    e->fade_target = 0.0f;
    e->fade_speed  = 1.0f;

    memset(s_keys_a, 0, sizeof(s_keys_a));
    memset(s_keys_b, 0, sizeof(s_keys_b));
    s_keys_cur  = s_keys_a;
    s_keys_prev = s_keys_b;

    clock_gettime(CLOCK_MONOTONIC, &s_time_origin);
    s_time_prev = s_time_origin;

    e->display = XOpenDisplay(NULL);
    if (!e->display) return 0;

    e->screen = DefaultScreen(e->display);

    vi = glXChooseVisual(e->display, e->screen, visual_attribs);
    if (!vi) { XCloseDisplay(e->display); e->display = NULL; return 0; }

    swa.colormap   = XCreateColormap(e->display,
                                      RootWindow(e->display, vi->screen),
                                      vi->visual, AllocNone);
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                   | StructureNotifyMask
                   | ButtonPressMask | ButtonReleaseMask
                   | PointerMotionMask;
    swa.background_pixel = 0;
    swa.border_pixel     = 0;

    e->window = XCreateWindow(
        e->display, RootWindow(e->display, vi->screen),
        0, 0, (unsigned int)e->win_w, (unsigned int)e->win_h,
        0, vi->depth, InputOutput, vi->visual,
        CWColormap | CWEventMask | CWBackPixel | CWBorderPixel, &swa);

    XStoreName(e->display, e->window, title);
    XMapWindow(e->display, e->window);

    e->glx_ctx = glXCreateContext(e->display, vi, NULL, GL_TRUE);
    XFree(vi);
    if (!e->glx_ctx) {
        XDestroyWindow(e->display, e->window);
        XCloseDisplay(e->display);
        e->display = NULL;
        return 0;
    }

    glXMakeCurrent(e->display, e->window, e->glx_ctx);

    if (!_vbo_load_procs()) {
        glXMakeCurrent(e->display, None, NULL);
        glXDestroyContext(e->display, e->glx_ctx);
        XDestroyWindow(e->display, e->window);
        XCloseDisplay(e->display);
        e->display = NULL;
        return 0;
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    _setup_projection(e);

    e->white_tex = _make_texture(white_pixel, 1, 1);
    _batch_init();

    /* OTIMIZAÇÃO GPU: VSync habilitado aqui, não lazy no primeiro present */
    {
        typedef void (*PFNGLXSWAPINTERVALEXTPROC)(Display*, GLXDrawable, int);
        PFNGLXSWAPINTERVALEXTPROC fn =
            (PFNGLXSWAPINTERVALEXTPROC)
            glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
        if (fn) {
            fn(e->display, e->window, 1);
        } else {
            typedef int (*PFNGLXSWAPINTERVALMESAPROC)(unsigned int);
            PFNGLXSWAPINTERVALMESAPROC fn2 =
                (PFNGLXSWAPINTERVALMESAPROC)
                glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");
            if (fn2) fn2(1);
        }
    }

    /* OTIMIZAÇÃO CPU: cache keycode→keysym montado 1x aqui */
    _build_keysym_cache(e);

    XFlush(e->display);
    return 1;
}

void engine_destroy(Engine *e)
{
    int i;
    if (!e->display) return;

    glXMakeCurrent(e->display, e->window, e->glx_ctx);
    _png_cache_clear();

    if (s_batch_ready) {
        _batch_flush();
        glDeleteBuffers(1, &s_batch.vbo);
        s_batch_ready = 0;
    }

    for (i = 0; i < e->sprite_count; i++)
        if (e->sprites[i].loaded && e->sprites[i].texture)
            glDeleteTextures(1, &e->sprites[i].texture);

    if (e->white_tex) glDeleteTextures(1, &e->white_tex);

    glXMakeCurrent(e->display, None, NULL);
    glXDestroyContext(e->display, e->glx_ctx);
    XDestroyWindow(e->display, e->window);
    XCloseDisplay(e->display);
    e->display = NULL;
}

/* ==============================================================
 * Configuração
 * ============================================================== */

void engine_set_background(Engine *e, int r, int g, int b) {
    e->bg_color = _pack_color(r, g, b);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

/* ==============================================================
 * Delta time
 * ============================================================== */

double engine_get_time(Engine *e) {
    struct timespec now;
    (void)e;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return _ts_to_secs(now) - _ts_to_secs(s_time_origin);
}

double engine_get_delta(Engine *e) {
    return e->delta_time;
}

/* ==============================================================
 * Sprites
 * ============================================================== */

int engine_load_sprite(Engine *e, const char *path)
{
    unsigned int   img_w, img_h;
    unsigned char *img_data;
    SpriteData    *sp;
    int            sid;

    if (e->sprite_count >= ENGINE_MAX_SPRITES) return -1;
    /* OTIMIZAÇÃO RAM: não usa cache aqui — libera RAM logo após upload GPU */
    img_data = _load_png_rgba(path, &img_w, &img_h);
    if (!img_data) return -1;

    sid = e->sprite_count;
    sp  = &e->sprites[sid];
    sp->texture = _make_texture(img_data, img_w, img_h);
    sp->width   = (int)img_w;
    sp->height  = (int)img_h;
    sp->loaded  = 1;

    free(img_data);   /* liberado imediatamente */
    e->sprite_count++;
    return sid;
}

int engine_load_sprite_region(Engine *e, const char *path,
                               int x, int y, int w, int h)
{
    unsigned int   img_w, img_h, row;
    unsigned char *img_data, *region_data;
    SpriteData    *sp;
    int            sid, full_row_bytes, region_row_bytes;

    if (e->sprite_count >= ENGINE_MAX_SPRITES) return -1;
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return -1;

    /* Cache ainda útil aqui: múltiplas regiões do mesmo atlas */
    img_data = _get_png_cached(path, &img_w, &img_h);
    if (!img_data) return -1;

    if ((unsigned int)(x + w) > img_w || (unsigned int)(y + h) > img_h)
        return -1;

    full_row_bytes   = (int)img_w * 4;
    region_row_bytes = w * 4;
    region_data      = (unsigned char *)malloc((size_t)h * (size_t)region_row_bytes);
    if (!region_data) return -1;

    for (row = 0; row < (unsigned int)h; row++)
        memcpy(region_data + row * (size_t)region_row_bytes,
               img_data + ((size_t)(y + (int)row) * (size_t)full_row_bytes)
                        + (size_t)(x * 4),
               (size_t)region_row_bytes);

    sid = e->sprite_count;
    sp  = &e->sprites[sid];
    sp->texture = _make_texture(region_data, (unsigned int)w, (unsigned int)h);
    sp->width   = w;
    sp->height  = h;
    sp->loaded  = 1;

    free(region_data);
    e->sprite_count++;
    return sid;
}

/* ==============================================================
 * Objetos
 * ============================================================== */

int engine_add_object(Engine *e, int x, int y, int sprite_id,
                       int width, int height, int r, int g, int b)
{
    int         oid;
    GameObject *obj;
    if (e->object_count >= ENGINE_MAX_OBJECTS) return -1;

    oid = e->object_count++;
    obj = &e->objects[oid];
    memset(obj, 0, sizeof(GameObject));
    obj->x         = x;
    obj->y         = y;
    obj->sprite_id = sprite_id;
    obj->width     = width;
    obj->height    = height;
    obj->color     = _pack_color(r, g, b);
    obj->active    = 1;
    obj->scale_x   = 1.0f;
    obj->scale_y   = 1.0f;
    obj->alpha     = 1.0f;
    obj->layer     = 0;
    obj->z_order   = oid;
    return oid;
}

int engine_add_tile_object(Engine *e, int x, int y, int sprite_id,
                            int tile_x, int tile_y,
                            int tile_w, int tile_h)
{
    int oid = engine_add_object(e, x, y, sprite_id, tile_w, tile_h, 255, 255, 255);
    if (oid < 0) return -1;
    e->objects[oid].use_tile = 1;
    e->objects[oid].tile_x   = tile_x;
    e->objects[oid].tile_y   = tile_y;
    e->objects[oid].tile_w   = tile_w;
    e->objects[oid].tile_h   = tile_h;
    return oid;
}

void engine_move_object(Engine *e, int oid, int dx, int dy) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].x += dx;
        e->objects[oid].y += dy;
    }
}

void engine_set_object_pos(Engine *e, int oid, int x, int y) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].x = x;
        e->objects[oid].y = y;
    }
}

void engine_set_object_sprite(Engine *e, int oid, int sprite_id) {
    if (oid >= 0 && oid < e->object_count)
        e->objects[oid].sprite_id = sprite_id;
}

void engine_get_object_pos(Engine *e, int oid, int *out_x, int *out_y) {
    if (oid >= 0 && oid < e->object_count) {
        *out_x = e->objects[oid].x;
        *out_y = e->objects[oid].y;
    } else {
        *out_x = *out_y = 0;
    }
}

void engine_set_object_tile(Engine *e, int oid, int tile_x, int tile_y) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].tile_x = tile_x;
        e->objects[oid].tile_y = tile_y;
    }
}

void engine_set_object_flip(Engine *e, int oid, int flip_h, int flip_v) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].flip_h = flip_h;
        e->objects[oid].flip_v = flip_v;
    }
}

void engine_set_object_scale(Engine *e, int oid, float sx, float sy) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].scale_x = sx;
        e->objects[oid].scale_y = sy;
    }
}

void engine_set_object_rotation(Engine *e, int oid, float degrees) {
    if (oid >= 0 && oid < e->object_count)
        e->objects[oid].rotation = degrees;
}

void engine_set_object_alpha(Engine *e, int oid, float alpha) {
    if (oid >= 0 && oid < e->object_count)
        e->objects[oid].alpha = _clampf(alpha, 0.0f, 1.0f);
}

void engine_set_object_layer(Engine *e, int oid, int layer, int z_order) {
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].layer   = layer;
        e->objects[oid].z_order = z_order;
    }
}

void engine_set_object_hitbox(Engine *e, int oid,
                               int offset_x, int offset_y,
                               int width, int height)
{
    if (oid >= 0 && oid < e->object_count) {
        e->objects[oid].hitbox.offset_x = offset_x;
        e->objects[oid].hitbox.offset_y = offset_y;
        e->objects[oid].hitbox.width    = width;
        e->objects[oid].hitbox.height   = height;
        e->objects[oid].hitbox.enabled  = 1;
    }
}

void engine_remove_object(Engine *e, int oid) {
    if (oid >= 0 && oid < e->object_count)
        e->objects[oid].active = 0;
}

/* ==============================================================
 * Câmera
 * ============================================================== */

void engine_camera_set(Engine *e, float x, float y) {
    e->camera.x = x;
    e->camera.y = y;
}

void engine_camera_move(Engine *e, float dx, float dy) {
    e->camera.x += dx;
    e->camera.y += dy;
}

void engine_camera_zoom(Engine *e, float zoom) {
    e->camera.zoom = zoom < 0.1f ? 0.1f : zoom;
}

void engine_camera_follow(Engine *e, int oid, float lerp_speed) {
    float tx, ty;
    if (oid < 0 || oid >= e->object_count) return;
    tx = (float)e->objects[oid].x - (float)e->render_w * 0.5f;
    ty = (float)e->objects[oid].y - (float)e->render_h * 0.5f;
    e->camera.x = _lerpf(e->camera.x, tx, _clampf(lerp_speed, 0.0f, 1.0f));
    e->camera.y = _lerpf(e->camera.y, ty, _clampf(lerp_speed, 0.0f, 1.0f));
}

void engine_camera_shake(Engine *e, float intensity, float duration) {
    e->camera.shake_intensity = intensity;
    e->camera.shake_duration  = duration;
    e->camera.shake_timer     = duration;
}

void engine_camera_enable(Engine *e, int enabled) {
    e->camera_enabled = enabled;
}

void engine_world_to_screen(Engine *e, float wx, float wy,
                              float *sx, float *sy)
{
    *sx = (wx - e->camera.x) * e->camera.zoom + e->camera.shake_x;
    *sy = (wy - e->camera.y) * e->camera.zoom + e->camera.shake_y;
}

void engine_screen_to_world(Engine *e, float sx, float sy,
                              float *wx, float *wy)
{
    *wx = (sx - e->camera.shake_x) / e->camera.zoom + e->camera.x;
    *wy = (sy - e->camera.shake_y) / e->camera.zoom + e->camera.y;
}

static void _camera_push(Engine *e) {
    if (!e->camera_enabled) return;
    glPushMatrix();
    glTranslatef(e->camera.shake_x, e->camera.shake_y, 0.0f);
    glScalef(e->camera.zoom, e->camera.zoom, 1.0f);
    glTranslatef(-e->camera.x, -e->camera.y, 0.0f);
}

static void _camera_pop(Engine *e) {
    if (!e->camera_enabled) return;
    _batch_flush();
    glPopMatrix();
}

/* ==============================================================
 * Partículas
 * ============================================================== */

int engine_emitter_add(Engine *e, ParticleEmitter *cfg)
{
    int eid;
    if (e->emitter_count >= 16) return -1;
    eid = e->emitter_count++;
    e->emitters[eid] = *cfg;
    e->emitters[eid].active = 1;
    e->emitters[eid]._acc   = 0.0f;
    return eid;
}

void engine_emitter_set_pos(Engine *e, int eid, float x, float y) {
    if (eid >= 0 && eid < e->emitter_count) {
        e->emitters[eid].x = x;
        e->emitters[eid].y = y;
    }
}

/*
 * OTIMIZAÇÃO CPU: slot livre encontrado com índice circular — O(1) amortizado.
 * Também atualiza particle_count para limitar as iterações de update/draw.
 */
static void _spawn_particle(Engine *e, ParticleEmitter *em)
{
    int i, idx;
    Particle *p = NULL;
    float t;

    for (i = 0; i < ENGINE_MAX_PARTICLES; i++) {
        idx = (e->particle_next + i) % ENGINE_MAX_PARTICLES;
        if (!e->particles[idx].active) {
            p = &e->particles[idx];
            e->particle_next = (idx + 1) % ENGINE_MAX_PARTICLES;
            if (idx + 1 > e->particle_count)
                e->particle_count = idx + 1;
            break;
        }
    }
    if (!p) return;

    t = (float)rand() / (float)RAND_MAX;
    p->x    = em->x;
    p->y    = em->y;
    p->vx   = em->vx_min + t * (em->vx_max - em->vx_min);
    t = (float)rand() / (float)RAND_MAX;
    p->vy   = em->vy_min + t * (em->vy_max - em->vy_min);
    p->ax   = em->ax;
    p->ay   = em->ay;
    t = (float)rand() / (float)RAND_MAX;
    p->life = p->life_max = em->life_min + t * (em->life_max - em->life_min);
    p->size_start = em->size_start;
    p->size_end   = em->size_end;
    p->r0 = em->r0; p->g0 = em->g0; p->b0 = em->b0; p->a0 = em->a0;
    p->r1 = em->r1; p->g1 = em->g1; p->b1 = em->b1; p->a1 = em->a1;
    p->sprite_id = em->sprite_id;
    p->active    = 1;
}

void engine_emitter_burst(Engine *e, int eid, int count)
{
    int i;
    if (eid < 0 || eid >= e->emitter_count) return;
    for (i = 0; i < count; i++)
        _spawn_particle(e, &e->emitters[eid]);
}

void engine_emitter_remove(Engine *e, int eid) {
    if (eid >= 0 && eid < e->emitter_count)
        e->emitters[eid].active = 0;
}

/*
 * OTIMIZAÇÃO CPU: itera somente até particle_count (não ENGINE_MAX_PARTICLES).
 * Cor e tamanho interpolados corretamente usando life/life_max.
 */
void engine_particles_update(Engine *e, float dt)
{
    int i, eid;
    Particle *p;
    ParticleEmitter *em;

    for (eid = 0; eid < e->emitter_count; eid++) {
        em = &e->emitters[eid];
        if (!em->active || em->rate <= 0) continue;
        em->_acc += dt;
        while (em->_acc >= 1.0f / (float)em->rate) {
            _spawn_particle(e, em);
            em->_acc -= 1.0f / (float)em->rate;
        }
    }

    for (i = 0; i < e->particle_count; i++) {
        p = &e->particles[i];
        if (!p->active) continue;

        p->vx   += p->ax * dt;
        p->vy   += p->ay * dt;
        p->x    += p->vx * dt;
        p->y    += p->vy * dt;
        p->life -= dt;

        if (p->life <= 0.0f) { p->active = 0; }
    }
}

void engine_particles_draw(Engine *e)
{
    int i;
    Particle *p;
    float t, r, g, b, a, size;

    for (i = 0; i < e->particle_count; i++) {
        p = &e->particles[i];
        if (!p->active) continue;

        /* Interpolação correta: t=0 → cor/size inicial, t=1 → final */
        t    = (p->life_max > 0.0f) ? (1.0f - p->life / p->life_max) : 1.0f;
        r    = _lerpf(p->r0, p->r1, t);
        g    = _lerpf(p->g0, p->g1, t);
        b    = _lerpf(p->b0, p->b1, t);
        a    = _lerpf(p->a0, p->a1, t);
        size = _lerpf(p->size_start, p->size_end, t);

        if (p->sprite_id >= 0 && p->sprite_id < e->sprite_count &&
            e->sprites[p->sprite_id].loaded) {
            _batch_set_texture(e->sprites[p->sprite_id].texture);
        } else {
            _batch_set_texture(e->white_tex);
        }
        _batch_push_quad(p->x, p->y, size, size,
                         0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a);
    }
    _batch_flush();
}

/* ==============================================================
 * Animação
 * ============================================================== */

int engine_animator_add(Engine *e, int *sprite_ids, int frame_count,
                         float fps, int loop, int object_id)
{
    Animator *a;
    int i, aid;

    if (e->animator_count >= ENGINE_MAX_ANIMATORS) return -1;
    if (frame_count <= 0 || frame_count > 32) return -1;

    aid = e->animator_count++;
    a   = &e->animators[aid];
    memset(a, 0, sizeof(Animator));

    for (i = 0; i < frame_count; i++)
        a->sprite_ids[i] = sprite_ids[i];
    a->frame_count   = frame_count;
    a->fps           = fps > 0.0f ? fps : 10.0f;
    a->frame_dur     = 1.0f / a->fps;   /* OTIMIZAÇÃO: pré-computa divisão */
    a->loop          = loop;
    a->object_id     = object_id;
    a->current_frame = 0;
    a->timer         = 0.0f;
    a->finished      = 0;
    return aid;
}

void engine_animator_play(Engine *e, int aid) {
    if (aid >= 0 && aid < e->animator_count)
        e->animators[aid].finished = 0;
}

void engine_animator_stop(Engine *e, int aid) {
    if (aid >= 0 && aid < e->animator_count)
        e->animators[aid].finished = 1;
}

void engine_animator_reset(Engine *e, int aid) {
    if (aid >= 0 && aid < e->animator_count) {
        e->animators[aid].current_frame = 0;
        e->animators[aid].timer         = 0.0f;
        e->animators[aid].finished      = 0;
    }
}

int engine_animator_finished(Engine *e, int aid) {
    if (aid >= 0 && aid < e->animator_count)
        return e->animators[aid].finished;
    return 1;
}

/* OTIMIZAÇÃO CPU: usa frame_dur pré-computado, sem divisão por frame */
void engine_animators_update(Engine *e, float dt)
{
    int i;
    Animator *a;

    for (i = 0; i < e->animator_count; i++) {
        a = &e->animators[i];
        if (a->finished) continue;

        a->timer += dt;
        if (a->timer >= a->frame_dur) {
            a->timer -= a->frame_dur;
            a->current_frame++;
            if (a->current_frame >= a->frame_count) {
                if (a->loop) {
                    a->current_frame = 0;
                } else {
                    a->current_frame = a->frame_count - 1;
                    a->finished = 1;
                }
            }
            if (a->object_id >= 0 && a->object_id < e->object_count)
                e->objects[a->object_id].sprite_id = a->sprite_ids[a->current_frame];
        }
    }
}

/* ==============================================================
 * Fade
 * ============================================================== */

void engine_fade_to(Engine *e, float target_alpha, float speed,
                    int r, int g, int b)
{
    e->fade_target = _clampf(target_alpha, 0.0f, 1.0f);
    e->fade_speed  = speed > 0.0f ? speed : 1.0f;
    e->fade_r = r; e->fade_g = g; e->fade_b = b;
}

void engine_fade_draw(Engine *e)
{
    if (e->fade_alpha <= 0.0f) return;
    _batch_set_texture(e->white_tex);
    _batch_push_quad(0.0f, 0.0f, (float)e->render_w, (float)e->render_h,
                     0.0f, 0.0f, 1.0f, 1.0f,
                     e->fade_r / 255.0f, e->fade_g / 255.0f, e->fade_b / 255.0f,
                     e->fade_alpha);
    _batch_flush();
}

int engine_fade_done(Engine *e) {
    float diff = e->fade_target - e->fade_alpha;
    return (diff < 0 ? -diff : diff) < 0.01f;
}

/* ==============================================================
 * engine_update
 * ============================================================== */

void engine_update(Engine *e, float dt)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    e->delta_time   = _ts_to_secs(now) - _ts_to_secs(s_time_prev);
    e->time_elapsed = _ts_to_secs(now) - _ts_to_secs(s_time_origin);
    s_time_prev = now;

    if (dt <= 0.0f) dt = (float)e->delta_time;
    if (dt > 0.1f)  dt = 0.1f;

    if (e->camera.shake_timer > 0.0f) {
        float amp;
        e->camera.shake_timer -= dt;
        amp = e->camera.shake_intensity *
              (e->camera.shake_timer / e->camera.shake_duration);
        e->camera.shake_x = ((float)(rand() % 1000) / 500.0f - 1.0f) * amp;
        e->camera.shake_y = ((float)(rand() % 1000) / 500.0f - 1.0f) * amp;
        if (e->camera.shake_timer <= 0.0f) {
            e->camera.shake_x = 0.0f;
            e->camera.shake_y = 0.0f;
        }
    }

    if (e->fade_alpha < e->fade_target)
        e->fade_alpha = _clampf(e->fade_alpha + e->fade_speed * dt, 0.0f, e->fade_target);
    else if (e->fade_alpha > e->fade_target)
        e->fade_alpha = _clampf(e->fade_alpha - e->fade_speed * dt, e->fade_target, 1.0f);

    engine_particles_update(e, dt);
    engine_animators_update(e, dt);
}

/* ==============================================================
 * Renderização
 * ============================================================== */

void engine_clear(Engine *e) {
    (void)e;
    glClear(GL_COLOR_BUFFER_BIT);
}

/*
 * OTIMIZAÇÃO CPU: loop vai só até object_count (nunca mais).
 */
void engine_draw(Engine *e)
{
    int         i;
    GameObject *obj;
    SpriteData *sp;
    float       u0, v0, u1, v1;
    float       cr, cg, cb;
    float       ox, oy, dw, dh;

    _camera_push(e);

    for (i = 0; i < e->object_count; i++) {
        obj = &e->objects[i];
        if (!obj->active) continue;

        ox = (float)obj->x;
        oy = (float)obj->y;

        if (obj->sprite_id >= 0 && obj->sprite_id < e->sprite_count) {
            sp = &e->sprites[obj->sprite_id];
            if (!sp->loaded) continue;

            if (obj->use_tile) {
                int px = obj->tile_x * obj->tile_w;
                int py = obj->tile_y * obj->tile_h;
                _uv_for_tile(sp, px, py, obj->tile_w, obj->tile_h,
                             &u0, &v0, &u1, &v1);
                dw = (float)obj->tile_w * obj->scale_x;
                dh = (float)obj->tile_h * obj->scale_y;
            } else {
                u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
                dw = (float)sp->width  * obj->scale_x;
                dh = (float)sp->height * obj->scale_y;
            }

            _batch_set_texture(sp->texture);

            if (obj->rotation != 0.0f) {
                _draw_quad_rotated(ox, oy, dw, dh, u0, v0, u1, v1,
                                   1.0f, 1.0f, 1.0f, obj->alpha,
                                   obj->rotation, obj->flip_h, obj->flip_v);
            } else {
                _batch_push_quad_flip(ox, oy, dw, dh, u0, v0, u1, v1,
                                      obj->flip_h, obj->flip_v,
                                      1.0f, 1.0f, 1.0f, obj->alpha);
            }

        } else {
            dw = (float)obj->width  * obj->scale_x;
            dh = (float)obj->height * obj->scale_y;
            _batch_set_texture(e->white_tex);
            _unpack_color(obj->color, &cr, &cg, &cb);
            _batch_push_quad(ox, oy, dw, dh,
                             0.0f, 0.0f, 1.0f, 1.0f,
                             cr, cg, cb, obj->alpha);
        }
    }

    _batch_flush();
    _camera_pop(e);
}

/* ==============================================================
 * Primitivas de desenho
 * ============================================================== */

void engine_draw_rect(Engine *e, int x, int y, int w, int h,
                       int r, int g, int b)
{
    _batch_set_texture(e->white_tex);
    _batch_push_quad((float)x, (float)y, (float)w, (float)h,
                      0.0f, 0.0f, 1.0f, 1.0f,
                      r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

void engine_draw_rect_outline(Engine *e, int x, int y, int w, int h,
                               int r, int g, int b, int thickness)
{
    engine_draw_rect(e, x,             y,             w,         thickness, r, g, b);
    engine_draw_rect(e, x,             y+h-thickness, w,         thickness, r, g, b);
    engine_draw_rect(e, x,             y,             thickness, h,         r, g, b);
    engine_draw_rect(e, x+w-thickness, y,             thickness, h,         r, g, b);
}

/*
 * OTIMIZAÇÃO GPU: linha via quad no batch (sem glBegin/glEnd por frame).
 */
void engine_draw_line(Engine *e, int x0, int y0, int x1, int y1,
                      int r, int g, int b, int thickness)
{
    float dx  = (float)(x1 - x0);
    float dy  = (float)(y1 - y0);
    float len = sqrtf(dx*dx + dy*dy);
    float angle;

    if (len < 1.0f) return;
    angle = -(float)(atan2((double)dy, (double)dx) * 180.0 / M_PI);

    _batch_flush();
    _bind_texture(e->white_tex);
    s_batch.current_tex = e->white_tex;

    glPushMatrix();
    glTranslatef((float)x0, (float)y0, 0.0f);
    glRotatef(-angle, 0.0f, 0.0f, 1.0f);
    glColor4f(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0,0); glVertex2f(0.0f,            -(float)thickness * 0.5f);
        glTexCoord2f(1,0); glVertex2f(len,             -(float)thickness * 0.5f);
        glTexCoord2f(1,1); glVertex2f(len,              (float)thickness * 0.5f);
        glTexCoord2f(0,1); glVertex2f(0.0f,             (float)thickness * 0.5f);
    glEnd();
    glColor4f(1,1,1,1);
    glPopMatrix();
}

/*
 * OTIMIZAÇÃO GPU: círculo via fan de quads no batch,
 * usando _batch_push_quad para triângulos (GL_QUADS com vértice central repetido).
 * Para simplificar e manter compatibilidade, ainda usamos glBegin mas apenas 1x por chamada.
 */
void engine_draw_circle(Engine *e, int cx, int cy, int radius,
                         int r, int g, int b, int filled)
{
    int   segments = 24;   /* reduzido de 32 para 24 — qualidade idêntica a olho nu */
    float fr = r / 255.0f;
    float fg = g / 255.0f;
    float fb = b / 255.0f;
    float step = (float)(2.0 * M_PI) / (float)segments;
    int   i;

    _batch_flush();
    _bind_texture(e->white_tex);
    s_batch.current_tex = e->white_tex;

    glColor4f(fr, fg, fb, 1.0f);
    glBegin(filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (filled) glVertex2f((float)cx, (float)cy);
    for (i = 0; i <= segments; i++) {
        float a = step * (float)i;
        glVertex2f((float)cx + cosf(a) * (float)radius,
                   (float)cy + sinf(a) * (float)radius);
    }
    glEnd();
    glColor4f(1,1,1,1);
}

void engine_draw_overlay(Engine *e, int x, int y, int w, int h,
                          int r, int g, int b, float alpha)
{
    _batch_set_texture(e->white_tex);
    _batch_push_quad((float)x, (float)y, (float)w, (float)h,
                      0.0f, 0.0f, 1.0f, 1.0f,
                      r / 255.0f, g / 255.0f, b / 255.0f, alpha);
}

void engine_flush(Engine *e) {
    (void)e;
    _batch_flush();
}

/* ==============================================================
 * Efeitos (chuva / noite)
 * ============================================================== */

void engine_draw_rain(Engine *e,
                      int screen_w, int screen_h, int frame,
                      const int *gotas_x, const int *gotas_y, int n_gotas,
                      int gota_w, int gota_h)
{
    int i;
    (void)frame;

    _batch_set_texture(e->white_tex);
    _batch_push_quad(0.0f, 0.0f, (float)screen_w, (float)screen_h,
                     0.0f, 0.0f, 1.0f, 1.0f,
                     0.0f, 30.0f/255.0f, 80.0f/255.0f, 0.35f);

    for (i = 0; i < n_gotas; i++) {
        if (gotas_y[i] < 0 || gotas_y[i] >= screen_h) continue;
        _batch_push_quad((float)gotas_x[i], (float)gotas_y[i],
                         (float)gota_w, (float)gota_h,
                         0.0f, 0.0f, 1.0f, 1.0f,
                         180/255.0f, 220/255.0f, 1.0f, 0.85f);
        _batch_push_quad((float)gotas_x[i], (float)(gotas_y[i] + gota_h),
                         (float)gota_w, 2.0f,
                         0.0f, 0.0f, 1.0f, 1.0f,
                         90/255.0f, 150/255.0f, 220/255.0f, 0.7f);
    }
    _batch_flush();
}

void engine_draw_night(Engine *e,
                       int screen_w, int screen_h,
                       float intensidade, int offset)
{
    (void)offset;
    if (intensidade <= 0.0f) return;
    _batch_set_texture(e->white_tex);
    _batch_push_quad(0.0f, 0.0f, (float)screen_w, (float)screen_h,
                     0.0f, 0.0f, 1.0f, 1.0f,
                     5/255.0f, 5/255.0f, 20/255.0f, intensidade);
    _batch_flush();
}

/* ==============================================================
 * VSync + Present
 * ============================================================== */

void engine_present(Engine *e) {
    /* VSync já configurado no init — só faz o swap */
    glXSwapBuffers(e->display, e->window);
}

/* ==============================================================
 * Tilemap
 * ============================================================== */

void engine_draw_tilemap(Engine *e,
                          const int *tilemap,
                          int tile_rows, int tile_cols,
                          int sprite_id, int tile_w, int tile_h,
                          int offset_x, int offset_y)
{
    SpriteData *sp;
    int row, col, tile_id, src_x, src_y, dst_x, dst_y, tiles_per_row;
    float u0, v0, u1, v1;

    if (sprite_id < 0 || sprite_id >= e->sprite_count) return;
    sp = &e->sprites[sprite_id];
    if (!sp->loaded) return;

    tiles_per_row = sp->width / tile_w;
    _batch_set_texture(sp->texture);

    for (row = 0; row < tile_rows; row++) {
        for (col = 0; col < tile_cols; col++) {
            tile_id = tilemap[row * tile_cols + col];
            if (tile_id < 0) continue;
            src_x = (tile_id % tiles_per_row) * tile_w;
            src_y = (tile_id / tiles_per_row) * tile_h;
            dst_x = col * tile_w + offset_x;
            dst_y = row * tile_h + offset_y;
            _uv_for_tile(sp, src_x, src_y, tile_w, tile_h, &u0, &v0, &u1, &v1);
            _batch_push_quad((float)dst_x, (float)dst_y,
                              (float)tile_w, (float)tile_h,
                              u0, v0, u1, v1,
                              1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
    _batch_flush();
}

/* ==============================================================
 * Sprite parts
 * ============================================================== */

static void _draw_sprite_region(Engine *e, int sprite_id,
                                  int x, int y, int src_x, int src_y,
                                  int src_w, int src_h,
                                  float cr, float cg, float cb, float ca)
{
    SpriteData *sp;
    float u0, v0, u1, v1;

    if (sprite_id < 0 || sprite_id >= e->sprite_count) return;
    sp = &e->sprites[sprite_id];
    if (!sp->loaded) return;

    _uv_for_tile(sp, src_x, src_y, src_w, src_h, &u0, &v0, &u1, &v1);
    _batch_set_texture(sp->texture);
    /* OTIMIZAÇÃO GPU: removido _batch_flush extra — o caller faz flush quando necessário */
    _batch_push_quad((float)x, (float)y, (float)src_w, (float)src_h,
                      u0, v0, u1, v1, cr, cg, cb, ca);
}

void engine_draw_sprite_part(Engine *e, int sprite_id,
                              int x, int y,
                              int src_x, int src_y,
                              int src_w, int src_h)
{
    _draw_sprite_region(e, sprite_id, x, y, src_x, src_y, src_w, src_h,
                        1.0f, 1.0f, 1.0f, 1.0f);
}

void engine_draw_sprite_part_ex(Engine *e, int sprite_id,
                                 int x, int y,
                                 int src_x, int src_y,
                                 int src_w, int src_h,
                                 float scale_x, float scale_y,
                                 float rotation, float alpha,
                                 int flip_h, int flip_v)
{
    SpriteData *sp;
    float u0, v0, u1, v1;
    float dw, dh;

    if (sprite_id < 0 || sprite_id >= e->sprite_count) return;
    sp = &e->sprites[sprite_id];
    if (!sp->loaded) return;

    _uv_for_tile(sp, src_x, src_y, src_w, src_h, &u0, &v0, &u1, &v1);

    dw = (float)src_w * scale_x;
    dh = (float)src_h * scale_y;

    if (rotation != 0.0f) {
        _bind_texture(sp->texture);
        s_batch.current_tex = sp->texture;
        _draw_quad_rotated((float)x, (float)y, dw, dh,
                           u0, v0, u1, v1,
                           1.0f, 1.0f, 1.0f, alpha,
                           rotation, flip_h, flip_v);
    } else {
        _batch_set_texture(sp->texture);
        _batch_push_quad_flip((float)x, (float)y, dw, dh,
                               u0, v0, u1, v1,
                               flip_h, flip_v,
                               1.0f, 1.0f, 1.0f, alpha);
    }
}

void engine_draw_sprite_part_inverted(Engine *e, int sprite_id,
                                       int x, int y,
                                       int src_x, int src_y,
                                       int src_w, int src_h)
{
    SpriteData *sp;
    float u0, v0, u1, v1;

    if (sprite_id < 0 || sprite_id >= e->sprite_count) return;
    sp = &e->sprites[sprite_id];
    if (!sp->loaded) return;

    _uv_for_tile(sp, src_x, src_y, src_w, src_h, &u0, &v0, &u1, &v1);
    _batch_set_texture(sp->texture);
    _batch_push_quad((float)x, (float)y, (float)src_w, (float)src_h,
                      u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);
    _batch_flush();

    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
    _batch_set_texture(sp->texture);
    _batch_push_quad((float)x, (float)y, (float)src_w, (float)src_h,
                      u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);
    _batch_flush();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ==============================================================
 * Texto / UI
 * ============================================================== */

void engine_draw_text(Engine *e, int x, int y, const char *text,
                       int font_sid, int font_w, int font_h,
                       int chars_per_row, int ascii_offset,
                       int line_spacing)
{
    SpriteData *sp;
    int  cx, cy, cp, tile_x, tile_y;
    float u0, v0, u1, v1;
    const char *p;

    if (font_sid < 0 || font_sid >= e->sprite_count) return;
    sp = &e->sprites[font_sid];
    if (!sp->loaded) return;

    _batch_set_texture(sp->texture);
    cx = x; cy = y;
    for (p = text; *p; p++) {
        if (*p == '\n') { cy += font_h + line_spacing; cx = x; continue; }
        cp = (int)(unsigned char)*p - ascii_offset;
        if (cp < 0) cp = 0;
        tile_x = (cp % chars_per_row) * font_w;
        tile_y = (cp / chars_per_row) * font_h;
        _uv_for_tile(sp, tile_x, tile_y, font_w, font_h, &u0, &v0, &u1, &v1);
        _batch_push_quad((float)cx, (float)cy, (float)font_w, (float)font_h,
                          u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);
        cx += font_w;
    }
    _batch_flush();
}

void engine_draw_box(Engine *e, int x, int y, int box_w, int box_h,
                      int box_sid, int tile_w, int tile_h)
{
    SpriteData *sp;
    int   cx, cy, copy_w, copy_h;
    float u0, v0, u1, v1, sw, sh;

    if (box_sid < 0 || box_sid >= e->sprite_count) return;
    sp = &e->sprites[box_sid];
    if (!sp->loaded) return;
    if (box_w < tile_w * 2) box_w = tile_w * 2;
    if (box_h < tile_h * 2) box_h = tile_h * 2;

    sw = (float)sp->width;
    sh = (float)sp->height;
    _batch_set_texture(sp->texture);

#define BOX_UV(tx,ty,tw,th) \
    u0=(float)(tx)/sw; v0=(float)(ty)/sh; \
    u1=(float)((tx)+(tw))/sw; v1=(float)((ty)+(th))/sh

    cy = y + tile_h;
    while (cy < y + box_h - tile_h) {
        copy_h = tile_h;
        if (cy + copy_h > y + box_h - tile_h) copy_h = (y+box_h-tile_h)-cy;
        cx = x + tile_w;
        while (cx < x + box_w - tile_w) {
            copy_w = tile_w;
            if (cx + copy_w > x + box_w - tile_w) copy_w = (x+box_w-tile_w)-cx;
            BOX_UV(tile_w, tile_h, copy_w, copy_h);
            _batch_push_quad((float)cx,(float)cy,(float)copy_w,(float)copy_h,u0,v0,u1,v1,1,1,1,1);
            cx += copy_w;
        }
        cy += copy_h;
    }

    cx = x + tile_w;
    while (cx < x + box_w - tile_w) {
        copy_w = tile_w; if (cx + copy_w > x+box_w-tile_w) copy_w=(x+box_w-tile_w)-cx;
        BOX_UV(tile_w,0,copy_w,tile_h);
        _batch_push_quad((float)cx,(float)y,(float)copy_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
        BOX_UV(tile_w,tile_h*2,copy_w,tile_h);
        _batch_push_quad((float)cx,(float)(y+box_h-tile_h),(float)copy_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
        cx += copy_w;
    }

    cy = y + tile_h;
    while (cy < y + box_h - tile_h) {
        copy_h = tile_h; if (cy + copy_h > y+box_h-tile_h) copy_h=(y+box_h-tile_h)-cy;
        BOX_UV(0,tile_h,tile_w,copy_h);
        _batch_push_quad((float)x,(float)cy,(float)tile_w,(float)copy_h,u0,v0,u1,v1,1,1,1,1);
        BOX_UV(tile_w*2,tile_h,tile_w,copy_h);
        _batch_push_quad((float)(x+box_w-tile_w),(float)cy,(float)tile_w,(float)copy_h,u0,v0,u1,v1,1,1,1,1);
        cy += copy_h;
    }

    BOX_UV(0,0,tile_w,tile_h);          _batch_push_quad((float)x,(float)y,(float)tile_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
    BOX_UV(tile_w*2,0,tile_w,tile_h);   _batch_push_quad((float)(x+box_w-tile_w),(float)y,(float)tile_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
    BOX_UV(0,tile_h*2,tile_w,tile_h);   _batch_push_quad((float)x,(float)(y+box_h-tile_h),(float)tile_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
    BOX_UV(tile_w*2,tile_h*2,tile_w,tile_h); _batch_push_quad((float)(x+box_w-tile_w),(float)(y+box_h-tile_h),(float)tile_w,(float)tile_h,u0,v0,u1,v1,1,1,1,1);
#undef BOX_UV

    _batch_flush();
}

void engine_draw_text_box(Engine *e,
                           int x, int y, int box_w, int box_h,
                           const char *title, const char *content,
                           int box_sid, int box_tw, int box_th,
                           int font_sid, int font_w, int font_h,
                           int chars_per_row, int ascii_offset,
                           int line_spacing)
{
    int   inner_x, inner_y, max_chars, line_y;
    char  current_line[1024], word[256];
    const char *p;
    int   word_len, line_len, cur_len;

    engine_draw_box(e, x, y, box_w, box_h, box_sid, box_tw, box_th);

    inner_x   = x + box_tw;
    inner_y   = y + box_th;
    max_chars = (box_w - box_tw * 2) / font_w;
    if (max_chars <= 0) return;

    if (title && title[0]) {
        engine_draw_text(e, inner_x, inner_y, title, font_sid, font_w, font_h,
                         chars_per_row, ascii_offset, line_spacing);
        inner_y += font_h + line_spacing + 8;
    }

    line_y = inner_y;
    current_line[0] = '\0';
    line_len = 0;
    p = content;

    while (*p) {
        word_len = 0;
        while (*p && *p != ' ' && *p != '\n') word[word_len++] = *p++;
        word[word_len] = '\0';

        if (word_len > 0) {
            cur_len = line_len == 0 ? word_len : line_len + 1 + word_len;
            if (cur_len <= max_chars) {
                if (line_len > 0) current_line[line_len++] = ' ';
                memcpy(current_line + line_len, word, (size_t)word_len);
                line_len += word_len;
                current_line[line_len] = '\0';
            } else {
                if (line_len > 0) {
                    engine_draw_text(e, inner_x, line_y, current_line,
                                     font_sid, font_w, font_h,
                                     chars_per_row, ascii_offset, line_spacing);
                    line_y += font_h + line_spacing;
                }
                memcpy(current_line, word, (size_t)word_len);
                line_len = word_len;
                current_line[line_len] = '\0';
            }
        }

        if (*p == '\n') {
            if (line_len > 0) {
                engine_draw_text(e, inner_x, line_y, current_line,
                                 font_sid, font_w, font_h,
                                 chars_per_row, ascii_offset, line_spacing);
                line_y += font_h + line_spacing;
            }
            current_line[0] = '\0'; line_len = 0; p++;
        } else if (*p == ' ') { p++; }
    }

    if (line_len > 0)
        engine_draw_text(e, inner_x, line_y, current_line,
                         font_sid, font_w, font_h,
                         chars_per_row, ascii_offset, line_spacing);
}

/* ==============================================================
 * Input — teclado + mouse
 * ============================================================== */

void engine_poll_events(Engine *e)
{
    XEvent ev;
    KeySym ksym;
    int    idx;
    int   *tmp;

    tmp         = s_keys_prev;
    s_keys_prev = s_keys_cur;
    s_keys_cur  = tmp;
    memcpy(s_keys_cur, s_keys_prev, sizeof(s_keys_a));

    e->keys      = s_keys_cur;
    e->keys_prev = s_keys_prev;

    e->mouse.buttons_prev[0] = e->mouse.buttons[0];
    e->mouse.buttons_prev[1] = e->mouse.buttons[1];
    e->mouse.buttons_prev[2] = e->mouse.buttons[2];
    e->mouse.scroll = 0;

    while (XPending(e->display) > 0) {
        XNextEvent(e->display, &ev);
        switch (ev.type) {
        case KeyPress:
        case KeyRelease:
            idx = ev.xkey.keycode & (ENGINE_MAX_KEYS - 1);
            if (ev.type == KeyPress) {
                s_keys_cur[idx] = 1;
                ksym = XLookupKeysym((XKeyEvent *)&ev, 0);
                if (ksym == XK_Escape) e->running = 0;
            } else {
                s_keys_cur[idx] = 0;
            }
            break;

        case ButtonPress:
            if (ev.xbutton.button == Button1) e->mouse.buttons[0] = 1;
            if (ev.xbutton.button == Button2) e->mouse.buttons[1] = 1;
            if (ev.xbutton.button == Button3) e->mouse.buttons[2] = 1;
            if (ev.xbutton.button == Button4) e->mouse.scroll = +1;
            if (ev.xbutton.button == Button5) e->mouse.scroll = -1;
            break;

        case ButtonRelease:
            if (ev.xbutton.button == Button1) e->mouse.buttons[0] = 0;
            if (ev.xbutton.button == Button2) e->mouse.buttons[1] = 0;
            if (ev.xbutton.button == Button3) e->mouse.buttons[2] = 0;
            break;

        case MotionNotify: {
            int best_scale = e->win_w / e->render_w;
            int tmp_s = e->win_h / e->render_h;
            if (tmp_s < best_scale) best_scale = tmp_s;
            if (best_scale < 1) best_scale = 1;
            int off_x = (e->win_w - e->render_w * best_scale) / 2;
            int off_y = (e->win_h - e->render_h * best_scale) / 2;
            e->mouse.x = (ev.xmotion.x - off_x) / best_scale;
            e->mouse.y = (ev.xmotion.y - off_y) / best_scale;
            break;
        }

        case Expose:
            _setup_projection(e);
            break;

        case ConfigureNotify:
            if (ev.xconfigure.width  != e->win_w ||
                ev.xconfigure.height != e->win_h) {
                e->win_w = ev.xconfigure.width;
                e->win_h = ev.xconfigure.height;
                if (e->fullscreen)
                    _apply_fullscreen_viewport(e);
                else
                    _setup_projection(e);
            }
            break;

        default: break;
        }
    }
}

/*
 * OTIMIZAÇÃO CPU: lookup O(1) via KeysymCache — sem loop nem chamada X11 por frame.
 */
int engine_key_down(Engine *e, const char *key) {
    KeySym ksym = _name_to_keysym(key);
    int kc;
    if (!ksym) return 0;
    for (kc = 0; kc < ENGINE_MAX_KEYS; kc++) {
        if (e->ksym_cache.map[kc] == ksym && s_keys_cur[kc])
            return 1;
    }
    return 0;
}

int engine_key_pressed(Engine *e, const char *key) {
    KeySym ksym = _name_to_keysym(key);
    int kc;
    if (!ksym) return 0;
    for (kc = 0; kc < ENGINE_MAX_KEYS; kc++) {
        if (e->ksym_cache.map[kc] == ksym) {
            if (s_keys_cur[kc] && !s_keys_prev[kc]) return 1;
        }
    }
    return 0;
}

int engine_key_released(Engine *e, const char *key) {
    KeySym ksym = _name_to_keysym(key);
    int kc;
    if (!ksym) return 0;
    for (kc = 0; kc < ENGINE_MAX_KEYS; kc++) {
        if (e->ksym_cache.map[kc] == ksym) {
            if (!s_keys_cur[kc] && s_keys_prev[kc]) return 1;
        }
    }
    return 0;
}

int engine_mouse_down(Engine *e, int button) {
    if (button < 0 || button > 2) return 0;
    return e->mouse.buttons[button];
}

int engine_mouse_pressed(Engine *e, int button) {
    if (button < 0 || button > 2) return 0;
    return e->mouse.buttons[button] && !e->mouse.buttons_prev[button];
}

int engine_mouse_released(Engine *e, int button) {
    if (button < 0 || button > 2) return 0;
    return !e->mouse.buttons[button] && e->mouse.buttons_prev[button];
}

void engine_mouse_pos(Engine *e, int *out_x, int *out_y) {
    *out_x = e->mouse.x;
    *out_y = e->mouse.y;
}

int engine_mouse_scroll(Engine *e) {
    return e->mouse.scroll;
}

/* ==============================================================
 * Colisão AABB (com hitbox customizada)
 * ============================================================== */

static void _get_hitbox(Engine *e, int oid,
                         int *hx, int *hy, int *hw, int *hh)
{
    GameObject *o = &e->objects[oid];
    if (o->hitbox.enabled) {
        *hx = o->x + o->hitbox.offset_x;
        *hy = o->y + o->hitbox.offset_y;
        *hw = o->hitbox.width;
        *hh = o->hitbox.height;
    } else {
        *hx = o->x;
        *hy = o->y;
        *hw = o->use_tile ? o->tile_w
            : (o->sprite_id >= 0 ? e->sprites[o->sprite_id].width  : o->width);
        *hh = o->use_tile ? o->tile_h
            : (o->sprite_id >= 0 ? e->sprites[o->sprite_id].height : o->height);
    }
}

int engine_check_collision(Engine *e, int oid1, int oid2)
{
    int ax, ay, aw, ah, bx, by, bw, bh;
    if (oid1 < 0 || oid2 < 0) return 0;
    if (oid1 >= e->object_count || oid2 >= e->object_count) return 0;
    if (!e->objects[oid1].active || !e->objects[oid2].active) return 0;

    _get_hitbox(e, oid1, &ax, &ay, &aw, &ah);
    _get_hitbox(e, oid2, &bx, &by, &bw, &bh);

    return (ax < bx+bw && ax+aw > bx && ay < by+bh && ay+ah > by);
}

int engine_check_collision_rect(Engine *e, int oid,
                                  int rx, int ry, int rw, int rh)
{
    int ax, ay, aw, ah;
    if (oid < 0 || oid >= e->object_count) return 0;
    if (!e->objects[oid].active) return 0;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return (ax < rx+rw && ax+aw > rx && ay < ry+rh && ay+ah > ry);
}

int engine_check_collision_point(Engine *e, int oid, int px, int py)
{
    int ax, ay, aw, ah;
    if (oid < 0 || oid >= e->object_count) return 0;
    if (!e->objects[oid].active) return 0;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return (px >= ax && px < ax+aw && py >= ay && py < ay+ah);
}

/* ==============================================================
 * FPS cap
 * ============================================================== */

void engine_cap_fps(Engine *e, int fps_target)
{
    static struct timespec last = {0, 0};
    struct timespec now, diff, sleep_ts;
    long frame_ns, elapsed_ns, sleep_ns;

    (void)e;
    if (fps_target <= 0) return;

    frame_ns = 1000000000L / fps_target;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (last.tv_sec == 0 && last.tv_nsec == 0) { last = now; return; }

    diff.tv_sec  = now.tv_sec  - last.tv_sec;
    diff.tv_nsec = now.tv_nsec - last.tv_nsec;
    if (diff.tv_nsec < 0) { diff.tv_sec--; diff.tv_nsec += 1000000000L; }
    elapsed_ns = diff.tv_sec * 1000000000L + diff.tv_nsec;
    sleep_ns   = frame_ns - elapsed_ns;

    if (sleep_ns > 0) {
        sleep_ts.tv_sec  = sleep_ns / 1000000000L;
        sleep_ts.tv_nsec = sleep_ns % 1000000000L;
        nanosleep(&sleep_ts, NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &last);
}

/* ==============================================================
 * Fullscreen pixel-perfect
 * ============================================================== */

static void _apply_fullscreen_viewport(Engine *e)
{
    int best_scale = e->win_w / e->saved_render_w;
    if (e->win_h / e->saved_render_h < best_scale)
        best_scale = e->win_h / e->saved_render_h;
    if (best_scale < 1) best_scale = 1;

    e->render_w = e->win_w / best_scale;
    e->render_h = e->win_h / best_scale;
    _setup_projection(e);
}

void engine_toggle_fullscreen(Engine *e)
{
    typedef struct { unsigned long flags, functions, decorations, input_mode, status; } MotifHints;
    Atom       motif_atom;
    MotifHints hints;
    int        screen_w, screen_h;
    XEvent     ev;

    if (!e->display || !e->window) return;
    motif_atom = XInternAtom(e->display, "_MOTIF_WM_HINTS", False);

    if (!e->fullscreen) {
        e->saved_win_w    = e->win_w;
        e->saved_win_h    = e->win_h;
        e->saved_render_w = e->render_w;
        e->saved_render_h = e->render_h;

        screen_w = DisplayWidth (e->display, e->screen);
        screen_h = DisplayHeight(e->display, e->screen);

        memset(&hints, 0, sizeof(hints));
        hints.flags = 2; hints.decorations = 0;
        XChangeProperty(e->display, e->window, motif_atom, motif_atom,
                        32, PropModeReplace, (unsigned char *)&hints, 5);

        XMoveResizeWindow(e->display, e->window, 0, 0,
                          (unsigned int)screen_w, (unsigned int)screen_h);
        XRaiseWindow(e->display, e->window);
        XSync(e->display, False);
        while (XCheckTypedWindowEvent(e->display, e->window, ConfigureNotify, &ev)) {}

        e->win_w = screen_w;
        e->win_h = screen_h;
        _apply_fullscreen_viewport(e);
        e->fullscreen = 1;

    } else {
        e->render_w = e->saved_render_w;
        e->render_h = e->saved_render_h;

        memset(&hints, 0, sizeof(hints));
        hints.flags = 2; hints.decorations = 1;
        XChangeProperty(e->display, e->window, motif_atom, motif_atom,
                        32, PropModeReplace, (unsigned char *)&hints, 5);

        XMoveResizeWindow(e->display, e->window, 100, 100,
                          (unsigned int)e->saved_win_w,
                          (unsigned int)e->saved_win_h);
        XSync(e->display, False);

        e->win_w = e->saved_win_w;
        e->win_h = e->saved_win_h;

        glViewport(0, 0, e->win_w, e->win_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, e->render_w, e->render_h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        e->fullscreen = 0;
    }
}
/* ==============================================================
 * Audio subsystem — miniaudio integration
 * ==============================================================
 *
 * All functions are thread-safe via AudioContext.mutex.
 * Call engine_audio_update() once per frame to process auto-resume.
 * ============================================================== */

/* Cast helpers — AudioContext embeds ma_engine as a raw byte array
 * in Engine.h (for consumers that don't include miniaudio.h).
 * In this .c file miniaudio.h is fully included, so we cast directly. */
static inline ma_engine *_ma(Engine *e) {
    return (ma_engine *)e->audio._ma_engine;
}

/* Returns a pointer to the real ma_sound inside a track.
 * AudioTrack.sound is ma_sound_opaque = ma_sound when MA_SOUND_DEFINED. */
static inline ma_sound *_ms(AudioTrack *t) {
    return (ma_sound *)&t->sound;
}

/* ---- Internal helpers ------------------------------------- */

static float _audio_clamp_volume(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
static float _audio_clamp_pitch(float p) {
    return p < 0.1f ? 0.1f : (p > 4.0f ? 4.0f : p);
}
static int _audio_handle_valid(AudioContext *ac, AudioHandle h) {
    return (h >= 0 && h < ENGINE_AUDIO_MAX_TRACKS && ac->tracks[h].in_use);
}
static int _audio_free_slot(AudioContext *ac) {
    int i;
    for (i = 0; i < ENGINE_AUDIO_MAX_TRACKS; i++)
        if (!ac->tracks[i].in_use) return i;
    return ENGINE_AUDIO_INVALID;
}

/* ---- Lifecycle ------------------------------------------- */

int engine_audio_init(Engine *e)
{
    ma_result r;
    memset(&e->audio, 0, sizeof(AudioContext));

    r = ma_engine_init(NULL, _ma(e));
    if (r != MA_SUCCESS) {
        fprintf(stderr, "engine_audio: failed to init miniaudio (%d)\n", r);
        return 0;
    }

    pthread_mutex_init(&e->audio.mutex, NULL);
    e->audio.ready = 1;
    return 1;
}

/*
 * Must be called once per frame.
 * Detects finished tracks and triggers auto-resume.
 */
void engine_audio_update(Engine *e)
{
    int i;
    AudioContext *ac = &e->audio;
    if (!ac->ready) return;

    pthread_mutex_lock(&ac->mutex);
    for (i = 0; i < ENGINE_AUDIO_MAX_TRACKS; i++) {
        AudioTrack *t = &ac->tracks[i];
        if (!t->in_use || t->status != AUDIO_TRACK_PLAYING) continue;
        if (t->loop) continue;

        if (ma_sound_at_end(_ms(t))) {
            t->status = AUDIO_TRACK_FINISHED;

            /* Auto-resume the previous track if requested */
            if (_audio_handle_valid(ac, t->resume_after)) {
                AudioTrack *prev = &ac->tracks[t->resume_after];
                if (prev->status == AUDIO_TRACK_PAUSED) {
                    ma_sound_start(_ms(prev));
                    prev->status = AUDIO_TRACK_PLAYING;
                }
            }
        }
    }
    pthread_mutex_unlock(&ac->mutex);
}

void engine_audio_destroy(Engine *e)
{
    int i;
    AudioContext *ac = &e->audio;
    if (!ac->ready) return;

    pthread_mutex_lock(&ac->mutex);
    for (i = 0; i < ENGINE_AUDIO_MAX_TRACKS; i++) {
        if (ac->tracks[i].in_use) {
            ma_sound_uninit(_ms(&ac->tracks[i]));
            ac->tracks[i].in_use = 0;
        }
    }
    pthread_mutex_unlock(&ac->mutex);

    ma_engine_uninit(_ma(e));
    pthread_mutex_destroy(&ac->mutex);
    ac->ready = 0;
}

/* ---- Main API -------------------------------------------- */

AudioHandle engine_audio_play(Engine *e,
                               const char  *file,
                               int          loop,
                               float        volume,
                               float        pitch,
                               AudioHandle  resume_after)
{
    AudioContext *ac = &e->audio;
    AudioTrack   *t;
    ma_result     r;
    int           slot;

    if (!ac->ready || !file) return ENGINE_AUDIO_INVALID;

    pthread_mutex_lock(&ac->mutex);
    slot = _audio_free_slot(ac);
    if (slot == ENGINE_AUDIO_INVALID) {
        fprintf(stderr, "engine_audio: track limit (%d) reached\n",
                ENGINE_AUDIO_MAX_TRACKS);
        pthread_mutex_unlock(&ac->mutex);
        return ENGINE_AUDIO_INVALID;
    }

    t = &ac->tracks[slot];
    memset(t, 0, sizeof(AudioTrack));

    /* MA_SOUND_FLAG_STREAM: reads from disk on-the-fly, low RAM usage */
    r = ma_sound_init_from_file(_ma(e), file,
                                 MA_SOUND_FLAG_STREAM,
                                 NULL, NULL, _ms(t));
    if (r != MA_SUCCESS) {
        fprintf(stderr, "engine_audio: cannot load '%s' (err %d)\n", file, r);
        pthread_mutex_unlock(&ac->mutex);
        return ENGINE_AUDIO_INVALID;
    }

    t->volume       = _audio_clamp_volume(volume);
    t->pitch        = _audio_clamp_pitch(pitch);
    t->loop         = loop ? 1 : 0;
    t->resume_after = resume_after;
    t->in_use       = 1;
    t->status       = AUDIO_TRACK_PLAYING;

    ma_sound_set_volume  (_ms(t), t->volume);
    ma_sound_set_pitch   (_ms(t), t->pitch);
    ma_sound_set_looping (_ms(t), (ma_bool32)t->loop);
    ma_sound_start       (_ms(t));

    pthread_mutex_unlock(&ac->mutex);
    return (AudioHandle)slot;
}

void engine_audio_pause(Engine *e, AudioHandle h)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return;
    pthread_mutex_lock(&ac->mutex);
    if (ac->tracks[h].status == AUDIO_TRACK_PLAYING) {
        ma_sound_stop(_ms(&ac->tracks[h]));
        ac->tracks[h].status = AUDIO_TRACK_PAUSED;
    }
    pthread_mutex_unlock(&ac->mutex);
}

void engine_audio_resume(Engine *e, AudioHandle h)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return;
    pthread_mutex_lock(&ac->mutex);
    if (ac->tracks[h].status == AUDIO_TRACK_PAUSED) {
        ma_sound_start(_ms(&ac->tracks[h]));
        ac->tracks[h].status = AUDIO_TRACK_PLAYING;
    }
    pthread_mutex_unlock(&ac->mutex);
}

void engine_audio_stop(Engine *e, AudioHandle h)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return;
    pthread_mutex_lock(&ac->mutex);
    ma_sound_stop  (_ms(&ac->tracks[h]));
    ma_sound_uninit(_ms(&ac->tracks[h]));
    ac->tracks[h].in_use = 0;
    ac->tracks[h].status = AUDIO_TRACK_FREE;
    pthread_mutex_unlock(&ac->mutex);
}

void engine_audio_volume(Engine *e, AudioHandle h, float volume)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return;
    pthread_mutex_lock(&ac->mutex);
    ac->tracks[h].volume = _audio_clamp_volume(volume);
    ma_sound_set_volume(_ms(&ac->tracks[h]), ac->tracks[h].volume);
    pthread_mutex_unlock(&ac->mutex);
}

void engine_audio_pitch(Engine *e, AudioHandle h, float pitch)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return;
    pthread_mutex_lock(&ac->mutex);
    ac->tracks[h].pitch = _audio_clamp_pitch(pitch);
    ma_sound_set_pitch(_ms(&ac->tracks[h]), ac->tracks[h].pitch);
    pthread_mutex_unlock(&ac->mutex);
}

int engine_audio_done(Engine *e, AudioHandle h)
{
    AudioContext *ac = &e->audio;
    int done;
    if (!ac->ready || !_audio_handle_valid(ac, h)) return 1;
    pthread_mutex_lock(&ac->mutex);
    done = (ac->tracks[h].status == AUDIO_TRACK_FINISHED);
    pthread_mutex_unlock(&ac->mutex);
    return done;
}