#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define MA_SOUND_DEFINED

#include "Engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <new>
/* =============================================================================
 * Utilitários internos — cor, matemática e validação de índices
 *
 * Todas as funções abaixo são static e usadas apenas dentro deste arquivo.
 * Os helpers _oid_valid/_sid_valid/_eid_valid/_aid_valid são a única barreira
 * entre a API pública e o acesso aos arrays; todo acesso indexado passa por eles.
 * ============================================================================= */
static inline unsigned long _pack_color(int r, int g, int b){
    return (static_cast<unsigned long>(r & 0xFF) << 16)
         | (static_cast<unsigned long>(g & 0xFF) <<  8)
         |  static_cast<unsigned long>(b & 0xFF);
}

static inline void _unpack_color(unsigned long c, float *out_r, float *out_g, float *out_b){
    *out_r = static_cast<float>((c >> 16) & 0xFF) * (1.0f / 255.0f);
    *out_g = static_cast<float>((c >>  8) & 0xFF) * (1.0f / 255.0f);
    *out_b = static_cast<float>( c        & 0xFF) * (1.0f / 255.0f);
}

static inline float _lerpf (float a, float b, float t)           { return a + (b - a) * t; }
static inline float _clampf(float v, float lo, float hi)         { return v < lo ? lo : (v > hi ? hi : v); }
static inline bool  _oid_valid(const Engine *e, int oid)         { return oid >= 0 && oid < e->object_count; }
static inline bool  _sid_valid(const Engine *e, int sid)         { return sid >= 0 && sid < e->sprite_count; }
static inline bool  _eid_valid(const Engine *e, int eid)         { return eid >= 0 && eid < e->emitter_count; }
static inline bool  _aid_valid(const Engine *e, int aid)         { return aid >= 0 && aid < e->animator_count; }

static struct timespec s_time_origin = {0, 0};
static struct timespec s_time_prev   = {0, 0};

static inline double _ts_to_secs(const struct timespec &t){
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
}
#include <png.h>

/*
 * _load_png_rgba() — carrega um arquivo PNG do disco e retorna RGBA 8 bpp.
 *
 * Todas as variantes de formato suportadas por libpng são normalizadas:
 * paleta → RGB, grayscale → RGB, 16 bpp → 8 bpp, sem canal alpha → alpha=0xFF.
 * O chamador é responsável por liberar o buffer com free().
 * Retorna nullptr em qualquer falha, sem lançar exceção.
 */
static unsigned char *_load_png_rgba(const char *path,unsigned int *out_w,unsigned int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Engine: não foi possível abrir '%s'.\n", path);
        return nullptr;
    }

    png_structp ps = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
    if (!ps) { fclose(fp); return nullptr; }

    png_infop pi = png_create_info_struct(ps);
    if (!pi) {
        png_destroy_read_struct(&ps, nullptr, nullptr);
        fclose(fp);
        return nullptr;
    }

    if (setjmp(png_jmpbuf(ps))) {
        png_destroy_read_struct(&ps, &pi, nullptr);
        fclose(fp);
        return nullptr;
    }

    png_init_io(ps, fp);
    png_read_info(ps, pi);

    const unsigned int img_w      = png_get_image_width (ps, pi);
    const unsigned int img_h      = png_get_image_height(ps, pi);
    const int          bit_depth  = png_get_bit_depth   (ps, pi);
    const int          color_type = png_get_color_type  (ps, pi);

    /* Normaliza o formato para RGBA de 8 bits por canal */
    if (bit_depth == 16)
        png_set_strip_16(ps);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(ps);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(ps);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(ps);
    png_set_tRNS_to_alpha(ps);
    png_set_filler(ps, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(ps, pi);

    const size_t row_bytes = img_w * 4u;
    unsigned char *img_data = static_cast<unsigned char *>(
        malloc(static_cast<size_t>(img_h) * row_bytes));
    png_bytep *rows = static_cast<png_bytep *>(
        malloc(img_h * sizeof(png_bytep)));

    if (!img_data || !rows) {
        free(img_data);
        free(rows);
        png_destroy_read_struct(&ps, &pi, nullptr);
        fclose(fp);
        return nullptr;
    }

    for (unsigned int row = 0; row < img_h; ++row)
        rows[row] = img_data + row * row_bytes;

    png_read_image(ps, rows);
    png_destroy_read_struct(&ps, &pi, nullptr);
    fclose(fp);
    free(rows);

    *out_w = img_w;
    *out_h = img_h;
    return img_data;
}

/*
 * PngCache — cache de um único PNG para engine_load_sprite_region().
 *
 * Quando múltiplas regiões são extraídas do mesmo arquivo (ex.: sprite sheet),
 * evita reler e decodificar o PNG a cada chamada.  Armazena apenas a última
 * imagem acessada; comparação feita por path string.
 */
struct PngCache {
    char           path[512];
    unsigned char *data;
    unsigned int   w, h;
};

static PngCache s_png_cache = { "", nullptr, 0u, 0u };

static unsigned char *_get_png_cached(const char *path,unsigned int *out_w,unsigned int *out_h)
{
    if (s_png_cache.data && strcmp(s_png_cache.path, path) == 0) {
        *out_w = s_png_cache.w;
        *out_h = s_png_cache.h;
        return s_png_cache.data;
    }
    free(s_png_cache.data);
    s_png_cache.data = _load_png_rgba(path, out_w, out_h);
    if (!s_png_cache.data) return nullptr;

    strncpy(s_png_cache.path, path, sizeof(s_png_cache.path) - 1);
    s_png_cache.path[sizeof(s_png_cache.path) - 1] = '\0';
    s_png_cache.w = *out_w;
    s_png_cache.h = *out_h;
    return s_png_cache.data;
}

static void _png_cache_clear()
{
    free(s_png_cache.data);
    s_png_cache.data    = nullptr;
    s_png_cache.path[0] = '\0';
}

/* =============================================================================
 * Backend OpenGL 2.1 + X11
 *
 * Compilado apenas quando ENGINE_BACKEND_GL está definido.
 *
 * Estratégia de renderização:
 *   • Projeção ortográfica via glOrtho(); sem shaders — fixed-function pipeline.
 *   • Batch de até 4096 quads em um VBO dinâmico (GL_DYNAMIC_DRAW).
 *   • Um flush implícito ocorre quando a textura ativa muda ou o batch enche.
 *   • Rotação usa glPushMatrix/glRotatef fora do batch (draw imediato).
 *   • Input via XNextEvent(); keycode → KeySym resolvido pela KeysymCache.
 * ============================================================================= */
#ifdef ENGINE_BACKEND_GL

/*
 * Ponteiros para funções VBO do OpenGL 1.5, carregados dinamicamente via
 * glXGetProcAddressARB.  Necessário porque GL/gl.h do sistema pode não ter
 * os protótipos de GL 1.5 quando compilando contra uma versão mais antiga.
 */
#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER  0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#  define GL_DYNAMIC_DRAW  0x88E8
#endif
/* GLSL / shader */
#ifndef GL_FRAGMENT_SHADER
#  define GL_FRAGMENT_SHADER  0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#  define GL_VERTEX_SHADER    0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#  define GL_COMPILE_STATUS   0x8B81
#endif
#ifndef GL_LINK_STATUS
#  define GL_LINK_STATUS      0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#  define GL_INFO_LOG_LENGTH  0x8B84
#endif
/* FBO */
#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER       0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#  define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#  define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#if !defined(GL_VERSION_1_5)
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif

typedef void (*PFNGLGENBUFFERSPROC)   (GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)   (GLenum, GLuint);
typedef void (*PFNGLBUFFERDATAPROC)   (GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);

/* ---- Tipos de função GLSL (OpenGL 2.0) ----------------------------------- */
typedef GLuint (*PFNGLCREATESHADERPROC)      (GLenum);
#ifndef PFNGLSHADERSOURCEPROC
typedef void   (*PFNGLSHADERSOURCEPROC)      (GLuint, GLsizei, const GLchar *const*, const GLint *);
#endif
typedef void   (*PFNGLCOMPILESHADERPROC)     (GLuint);
typedef void   (*PFNGLGETSHADERIVPROC)       (GLuint, GLenum, GLint *);
typedef void   (*PFNGLGETSHADERINFOLOGPROC)  (GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)     (void);
typedef void   (*PFNGLATTACHSHADERPROC)      (GLuint, GLuint);
typedef void   (*PFNGLLINKPROGRAMPROC)       (GLuint);
typedef void   (*PFNGLGETPROGRAMIVPROC)      (GLuint, GLenum, GLint *);
typedef void   (*PFNGLGETPROGRAMINFOLOGPROC) (GLuint, GLsizei, GLsizei *, char *);
typedef void   (*PFNGLUSEPROGRAMPROC)        (GLuint);
typedef void   (*PFNGLDELETESHADERPROC)      (GLuint);
typedef void   (*PFNGLDELETEPROGRAMPROC)     (GLuint);
typedef GLint  (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const char *);
typedef void   (*PFNGLUNIFORM1IPROC)         (GLint, GLint);
typedef void   (*PFNGLUNIFORM1FPROC)         (GLint, GLfloat);
typedef void   (*PFNGLUNIFORM2FPROC)         (GLint, GLfloat, GLfloat);
typedef void   (*PFNGLUNIFORM4FPROC)         (GLint, GLfloat, GLfloat, GLfloat, GLfloat);

/* ---- Tipos de função FBO (GL_EXT_framebuffer_object / OpenGL 3.0) -------- */
typedef void (*PFNGLGENFRAMEBUFFERSPROC)        (GLsizei, GLuint *);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)        (GLenum,  GLuint);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)   (GLenum,  GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)     (GLsizei, const GLuint *);

/* Capacidade e layout do buffer de vértices do batch */
static constexpr int BATCH_MAX_QUADS         = 4096;
static constexpr int BATCH_FLOATS_PER_VERTEX = 8;    /* x y u v r g b a */
static constexpr int BATCH_VERTICES_PER_QUAD = 4;
static constexpr int BATCH_FLOATS_PER_QUAD   = BATCH_FLOATS_PER_VERTEX * BATCH_VERTICES_PER_QUAD;
static constexpr int BATCH_BUFFER_FLOATS     = BATCH_MAX_QUADS * BATCH_FLOATS_PER_QUAD;

struct BatchState {
    GLuint vbo;
    float  buf[BATCH_BUFFER_FLOATS];
    int    quad_count;
    GLuint current_tex;
};

struct KeyEntry {
    const char *name;
    KeySym      sym;
};

/*
 * Tabela de mapeamento nome → KeySym para teclas especiais.
 * Busca linear; pequena o suficiente para ser mais rápida que qualquer hash.
 * Armazenada em .rodata (constante em tempo de compilação).
 */
static const KeyEntry k_special_keys[] = {
    { "left",      XK_Left      },
    { "right",     XK_Right     },
    { "up",        XK_Up        },
    { "down",      XK_Down      },
    { "space",     XK_space     },
    { "return",    XK_Return    },
    { "escape",    XK_Escape    },
    { "shift",     XK_Shift_L   },
    { "ctrl",      XK_Control_L },
    { "alt",       XK_Alt_L     },
    { "tab",       XK_Tab       },
    { "backspace", XK_BackSpace },
    { "delete",    XK_Delete    },
    { "f1",  XK_F1  }, { "f2",  XK_F2  }, { "f3",  XK_F3  },
    { "f4",  XK_F4  }, { "f5",  XK_F5  }, { "f6",  XK_F6  },
    { "f7",  XK_F7  }, { "f8",  XK_F8  }, { "f9",  XK_F9  },
    { "f10", XK_F10 }, { "f11", XK_F11 }, { "f12", XK_F12 },
};

static constexpr int k_special_keys_count =
    static_cast<int>(sizeof(k_special_keys) / sizeof(k_special_keys[0]));

/*
 * _name_to_keysym() — converte string de tecla para KeySym.
 *
 * Strings de um caractere (ex.: "a", "1") mapeiam diretamente para o codepoint.
 * Nomes especiais (ex.: "left", "space") passam por busca linear na tabela acima.
 * Retorna 0 se o nome não for reconhecido.
 */
static KeySym _name_to_keysym(const char *key)
{
    if (!key || !key[0]) return 0;

    /* Tecla de um caractere: codepoint ASCII == KeySym correspondente */
    if (!key[1]) return static_cast<KeySym>(static_cast<unsigned char>(key[0]));

    /* Teclas especiais: busca linear na k_special_keys[] */
    for (int i = 0; i < k_special_keys_count; ++i)
        if (strcmp(key, k_special_keys[i].name) == 0)
            return k_special_keys[i].sym;

    return 0;
}

class RendererGL final : public IRenderer {
public:
    /* X11 / GLX */
    Display   *display  = nullptr;
    int        screen   = 0;
    Window     window   = 0;
    GLXContext glx_ctx  = nullptr;

    /* Procs VBO */
    PFNGLGENBUFFERSPROC    glGenBuffers_f    = nullptr;
    PFNGLBINDBUFFERPROC    glBindBuffer_f    = nullptr;
    PFNGLBUFFERDATAPROC    glBufferData_f    = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers_f = nullptr;

    /* Procs Shader GLSL */
    PFNGLCREATESHADERPROC       glCreateShader_f       = nullptr;
    PFNGLSHADERSOURCEPROC       glShaderSource_f       = nullptr;
    PFNGLCOMPILESHADERPROC      glCompileShader_f      = nullptr;
    PFNGLGETSHADERIVPROC        glGetShaderiv_f        = nullptr;
    PFNGLGETSHADERINFOLOGPROC   glGetShaderInfoLog_f   = nullptr;
    PFNGLCREATEPROGRAMPROC      glCreateProgram_f      = nullptr;
    PFNGLATTACHSHADERPROC       glAttachShader_f       = nullptr;
    PFNGLLINKPROGRAMPROC        glLinkProgram_f        = nullptr;
    PFNGLGETPROGRAMIVPROC       glGetProgramiv_f       = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC  glGetProgramInfoLog_f  = nullptr;
    PFNGLUSEPROGRAMPROC         glUseProgram_f         = nullptr;
    PFNGLDELETESHADERPROC       glDeleteShader_f       = nullptr;
    PFNGLDELETEPROGRAMPROC      glDeleteProgram_f      = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_f = nullptr;
    PFNGLUNIFORM1IPROC          glUniform1i_f          = nullptr;
    PFNGLUNIFORM1FPROC          glUniform1f_f          = nullptr;
    PFNGLUNIFORM2FPROC          glUniform2f_f          = nullptr;
    PFNGLUNIFORM4FPROC          glUniform4f_f          = nullptr;
    bool                        shaders_supported      = false;

    /* Procs FBO */
    PFNGLGENFRAMEBUFFERSPROC         glGenFramebuffers_f         = nullptr;
    PFNGLBINDFRAMEBUFFERPROC         glBindFramebuffer_f         = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC    glFramebufferTexture2D_f    = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC  glCheckFramebufferStatus_f  = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC      glDeleteFramebuffers_f      = nullptr;
    bool                             fbo_supported               = false;

    BatchState batch       = {};
    bool       batch_ready = false;
    GLuint     last_tex    = 0;

    /* Double buffer de teclado: ponteiros são trocados a cada frame em O(1) */
    /* keys_prev aponta para o buffer do frame anterior sem cópia de memória */
    int  keys_a[ENGINE_MAX_KEYS] = {};
    int  keys_b[ENGINE_MAX_KEYS] = {};
    int *keys_cur  = keys_a;
    int *keys_prev = keys_b;

    /* ---- IRenderer -------------------------------------------------------- */
    bool init           (Engine *e, int win_w, int win_h,
                         const char *title, int scale)      override;
    void destroy        (Engine *e)                         override;
    void clear          (Engine *e)                         override;
    void flush          (Engine *e)                         override;
    void present        (Engine *e)                         override;
    void set_vsync      (bool enable)                       override;
    unsigned int upload_texture(const unsigned char *rgba,
                                unsigned int w,
                                unsigned int h)             override;
    void delete_texture (unsigned int handle)               override;
    void set_texture    (Engine *e, unsigned int tex)       override;
    void push_quad      (Engine *e, const QuadParams &q)    override;
    void resize         (Engine *e, int new_w, int new_h)  override;
    void toggle_fullscreen(Engine *e)                       override;
    void setup_projection (Engine *e)                       override;
    void camera_push    (Engine *e)                         override;
    void camera_pop     (Engine *e)                         override;
    void draw_line_raw  (Engine *e,
                         float x0, float y0, float x1, float y1,
                         float r, float g, float b,
                         float thickness)                   override;
    void draw_circle_raw(Engine *e,
                         float cx, float cy, float radius,
                         float r, float g, float b,
                         bool filled)                       override;
    void set_blend_inverted(Engine *e)                      override;
    void set_blend_normal  (Engine *e)                      override;
    void set_clear_color   (float r, float g, float b)      override;
    void poll_events    (Engine *e)                         override;

    /* FBO */
    FboHandle    fbo_create (Engine *e, int w, int h)       override;
    void         fbo_destroy(Engine *e, FboHandle fh)       override;
    void         fbo_bind   (Engine *e, FboHandle fh)       override;
    void         fbo_unbind (Engine *e)                     override;
    unsigned int fbo_texture(Engine *e, FboHandle fh)       override;

    /* Shaders */
    ShaderHandle shader_create (Engine *e,
                                const char *vert_src,
                                const char *frag_src)       override;
    void         shader_destroy(Engine *e, ShaderHandle sh) override;
    void         shader_use    (Engine *e, ShaderHandle sh) override;
    void         shader_none   (Engine *e)                  override;
    void         shader_set_int  (Engine *e, ShaderHandle sh,
                                  const char *name, int   v) override;
    void         shader_set_float(Engine *e, ShaderHandle sh,
                                  const char *name, float v) override;
    void         shader_set_vec2 (Engine *e, ShaderHandle sh,
                                  const char *name,
                                  float x, float y)          override;
    void         shader_set_vec4 (Engine *e, ShaderHandle sh,
                                  const char *name,
                                  float x, float y,
                                  float z, float w)          override;

private:
    bool _load_vbo_procs();
    bool _load_shader_procs();
    bool _load_fbo_procs();
    void _batch_init();
    void _batch_flush_internal();
    void _batch_set_texture(unsigned int tex);
    void _batch_push_quad_raw(float dx, float dy, float dw, float dh,
                               float u0, float v0, float u1, float v1,
                               float r, float g, float b, float a);
    void _batch_push_quad_flip(float dx, float dy, float dw, float dh,
                                float u0, float v0, float u1, float v1,
                                int flip_h, int flip_v,
                                float r, float g, float b, float a);
    void _draw_quad_rotated(float dx, float dy, float dw, float dh,
                             float u0, float v0, float u1, float v1,
                             float r, float g, float b, float a,
                             float rotation_deg, int flip_h, int flip_v);
    void _apply_fullscreen_viewport(Engine *e);
    void _build_keysym_cache(Engine *e);
    /** Calcula o scale inteiro máximo que cabe em win_w × win_h. */
    static int _best_scale(int win_w, int win_h, int render_w, int render_h);
};

/* ---- RendererGL — utilitário de scale ------------------------------------ */
int RendererGL::_best_scale(int win_w, int win_h, int render_w, int render_h)
{
    int sx = win_w / render_w;
    int sy = win_h / render_h;
    int s  = sx < sy ? sx : sy;
    return s > 0 ? s : 1;
}

/* ---- RendererGL::init ----------------------------------------------------- */
bool RendererGL::init(Engine *e, int win_w, int win_h,
                      const char *title, int scale)
{
    static int visual_attribs[] = {
        GLX_RGBA, GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 0, None
    };
    (void)scale; /* scale não afeta a janela; a projeção ortográfica cuida disso */

    display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "Engine: XOpenDisplay falhou.\n");
        return false;
    }

    screen = DefaultScreen(display);

    XVisualInfo *vi = glXChooseVisual(display, screen, visual_attribs);
    if (!vi) {
        fprintf(stderr, "Engine: glXChooseVisual falhou.\n");
        XCloseDisplay(display);
        display = nullptr;
        return false;
    }

    XSetWindowAttributes swa{};
    swa.colormap   = XCreateColormap(display,
                                      RootWindow(display, vi->screen),
                                      vi->visual, AllocNone);
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                   | StructureNotifyMask
                   | ButtonPressMask | ButtonReleaseMask
                   | PointerMotionMask;
    swa.background_pixel = 0;
    swa.border_pixel     = 0;

    window = XCreateWindow(
        display, RootWindow(display, vi->screen),
        0, 0, static_cast<unsigned int>(win_w), static_cast<unsigned int>(win_h),
        0, vi->depth, InputOutput, vi->visual,
        CWColormap | CWEventMask | CWBackPixel | CWBorderPixel, &swa);

    XStoreName(display, window, title);
    XMapWindow(display, window);

    glx_ctx = glXCreateContext(display, vi, nullptr, GL_TRUE);
    XFree(vi);
    if (!glx_ctx) {
        fprintf(stderr, "Engine: glXCreateContext falhou.\n");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        display = nullptr;
        return false;
    }

    glXMakeCurrent(display, window, glx_ctx);

    if (!_load_vbo_procs()) {
        glXMakeCurrent(display, None, nullptr);
        glXDestroyContext(display, glx_ctx);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        display = nullptr;
        return false;
    }

    /* Tenta carregar shaders e FBOs — falha silenciosa (recursos opcionais) */
    _load_shader_procs();
    _load_fbo_procs();
    e->active_shader = ENGINE_SHADER_INVALID;

    /* Estado OpenGL inicial: blending alpha, sem depth test, sem cull face */
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    setup_projection(e);

    /* Cria uma textura branca 1×1; usada como tint neutro em primitivas sólidas */
    const unsigned char white[4] = {255, 255, 255, 255};
    e->white_tex = upload_texture(white, 1, 1);

    _batch_init();
    set_vsync(true);
    _build_keysym_cache(e);

    e->keys      = keys_cur;
    e->keys_prev = keys_prev;

    XFlush(display);
    return true;
}

/* ---- RendererGL::destroy -------------------------------------------------- */
void RendererGL::destroy(Engine *e)
{
    if (!display) return;
    glXMakeCurrent(display, window, glx_ctx);

    /* Libera FBOs e Shaders antes de destruir sprites e contexto */
    if (fbo_supported) {
        for (int i = 0; i < ENGINE_MAX_FBOS; ++i) {
            if (e->fbos[i].in_use) {
                GLuint fid = static_cast<GLuint>(e->fbos[i].fbo_id);
                GLuint tid = static_cast<GLuint>(e->fbos[i].color_tex);
                glDeleteFramebuffers_f(1, &fid);
                glDeleteTextures(1, &tid);
                e->fbos[i].in_use = 0;
            }
        }
    }
    if (shaders_supported) {
        for (int i = 0; i < ENGINE_MAX_SHADERS; ++i) {
            if (e->shaders[i].in_use) {
                glDeleteProgram_f(static_cast<GLuint>(e->shaders[i].program));
                e->shaders[i].in_use = 0;
            }
        }
    }

    /* Descarrega o VBO antes de destruir o contexto GL */
    if (batch_ready) {
        _batch_flush_internal();
        glDeleteBuffers_f(1, &batch.vbo);
        batch_ready = false;
    }

    for (int i = 0; i < e->sprite_count; ++i) {
        if (e->sprites[i].loaded && e->sprites[i].texture)
            glDeleteTextures(1, &e->sprites[i].texture);
    }

    if (e->white_tex)
        glDeleteTextures(1, reinterpret_cast<GLuint *>(&e->white_tex));

    glXMakeCurrent(display, None, nullptr);
    glXDestroyContext(display, glx_ctx);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    display = nullptr;
}

/* ---- Texturas ------------------------------------------------------------- */
unsigned int RendererGL::upload_texture(const unsigned char *rgba, unsigned int w, unsigned int h)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    last_tex = tex;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return static_cast<unsigned int>(tex);
}

void RendererGL::delete_texture(unsigned int handle)
{
    GLuint t = static_cast<GLuint>(handle);
    glDeleteTextures(1, &t);
}

/* ---- Frame ---------------------------------------------------------------- */
void RendererGL::clear(Engine *)          { glClear(GL_COLOR_BUFFER_BIT); }
void RendererGL::flush(Engine *)          { _batch_flush_internal(); }
void RendererGL::present(Engine *)        { glXSwapBuffers(display, window); }

void RendererGL::set_vsync(bool enable)
{
    /* Tenta GLX_EXT_swap_control primeiro, depois MESA */
    typedef void (*SwapIntervalEXT)(Display *, GLXDrawable, int);
    auto fn_ext = reinterpret_cast<SwapIntervalEXT>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalEXT")));
    if (fn_ext) {
        fn_ext(display, window, enable ? 1 : 0);
        return;
    }
    typedef int (*SwapIntervalMESA)(unsigned int);
    auto fn_mesa = reinterpret_cast<SwapIntervalMESA>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalMESA")));
    if (fn_mesa) fn_mesa(enable ? 1u : 0u);
}

/* ---- Batch ---------------------------------------------------------------- */
void RendererGL::set_texture(Engine *, unsigned int tex) { _batch_set_texture(tex); }

void RendererGL::push_quad(Engine *, const QuadParams &q)
{
    if (q.rotation != 0.0f) {
        _draw_quad_rotated(q.dx, q.dy, q.dw, q.dh,
                           q.u0, q.v0, q.u1, q.v1,
                           q.r,  q.g,  q.b,  q.a,
                           q.rotation, q.flip_h, q.flip_v);
    } else {
        _batch_push_quad_flip(q.dx, q.dy, q.dw, q.dh,
                              q.u0, q.v0, q.u1, q.v1,
                              q.flip_h, q.flip_v,
                              q.r,  q.g,  q.b,  q.a);
    }
}

/* ---- Projeção / resize ---------------------------------------------------- */
void RendererGL::setup_projection(Engine *e)
{
    const int s     = _best_scale(e->win_w, e->win_h, e->render_w, e->render_h);
    const int vp_w  = e->render_w * s;
    const int vp_h  = e->render_h * s;
    const int off_x = (e->win_w - vp_w) / 2;
    const int off_y = (e->win_h - vp_h) / 2;

    glViewport(off_x, off_y, vp_w, vp_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, e->render_w, e->render_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void RendererGL::resize(Engine *e, int new_w, int new_h)
{
    e->win_w = new_w;
    e->win_h = new_h;
    if (e->fullscreen) _apply_fullscreen_viewport(e);
    else               setup_projection(e);
}

/* --- Câmera --------------------------------------------------------------- */
void RendererGL::camera_push(Engine *e)
{
    if (!e->camera_enabled) return;
    glPushMatrix();
    glTranslatef(e->camera.shake_x, e->camera.shake_y, 0.0f);
    glScalef    (e->camera.zoom,    e->camera.zoom,    1.0f);
    glTranslatef(-e->camera.x,     -e->camera.y,       0.0f);
}

void RendererGL::camera_pop(Engine *e)
{
    if (!e->camera_enabled) return;
    _batch_flush_internal();
    glPopMatrix();
}

/* ---- Primitivas raw ------------------------------------------------------- */
void RendererGL::draw_line_raw(Engine *e,
                                float x0, float y0, float x1, float y1,
                                float r, float g, float b, float thickness)
{
    const float dx  = x1 - x0;
    const float dy  = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    const float angle     = -(float)(atan2((double)dy, (double)dx) * (180.0 / M_PI));
    const float half_t    = thickness * 0.5f;

    _batch_flush_internal();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(e->white_tex));
    last_tex          = e->white_tex;
    batch.current_tex = e->white_tex;

    glPushMatrix();
    glTranslatef(x0, y0, 0.0f);
    glRotatef(-angle, 0.0f, 0.0f, 1.0f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(0.0f,  -half_t);
        glTexCoord2f(1, 0); glVertex2f(len,   -half_t);
        glTexCoord2f(1, 1); glVertex2f(len,    half_t);
        glTexCoord2f(0, 1); glVertex2f(0.0f,   half_t);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPopMatrix();
}

void RendererGL::draw_circle_raw(Engine *e,
                                  float cx, float cy, float radius,
                                  float r, float g, float b, bool filled)
{
    static constexpr int   SEG  = 24;
    static constexpr float STEP = static_cast<float>(2.0 * M_PI) / SEG;

    _batch_flush_internal();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(e->white_tex));
    last_tex          = e->white_tex;
    batch.current_tex = e->white_tex;

    glColor4f(r, g, b, 1.0f);
    glBegin(filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (filled) glVertex2f(cx, cy);
    for (int i = 0; i <= SEG; ++i) {
        const float a = STEP * static_cast<float>(i);
        glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ---- Blend modes ---------------------------------------------------------- */
void RendererGL::set_blend_inverted(Engine *)
{
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
}
void RendererGL::set_blend_normal(Engine *)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---- Background color (PATCH 4) ------------------------------------------ */
void RendererGL::set_clear_color(float r, float g, float b)
{
    glClearColor(r, g, b, 1.0f);
}

/* ---- Fullscreen ----------------------------------------------------------- */
void RendererGL::_apply_fullscreen_viewport(Engine *e)
{
    const int s = _best_scale(e->win_w, e->win_h,
                               e->saved_render_w, e->saved_render_h);
    e->render_w = e->win_w / s;
    e->render_h = e->win_h / s;
    setup_projection(e);
}

void RendererGL::toggle_fullscreen(Engine *e)
{
    /* Estrutura Motif para remover/restaurar decorações da janela */
    struct MotifHints { unsigned long flags, functions, decorations, input_mode, status; };

    if (!display || !window) return;

    Atom       motif_atom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    MotifHints hints{};
    XEvent     ev;

    if (!e->fullscreen) {
        /* Salva estado atual e vai para tela cheia */
        e->saved_win_w    = e->win_w;
        e->saved_win_h    = e->win_h;
        e->saved_render_w = e->render_w;
        e->saved_render_h = e->render_h;

        const int screen_w = DisplayWidth (display, screen);
        const int screen_h = DisplayHeight(display, screen);

        hints.flags = 2; hints.decorations = 0;
        XChangeProperty(display, window, motif_atom, motif_atom,
                        32, PropModeReplace,
                        reinterpret_cast<unsigned char *>(&hints), 5);
        XMoveResizeWindow(display, window, 0, 0,
                          static_cast<unsigned int>(screen_w),
                          static_cast<unsigned int>(screen_h));
        XRaiseWindow(display, window);
        XSync(display, False);
        /* Descarta ConfigureNotify pendentes gerados pela mudança de tamanho */
        while (XCheckTypedWindowEvent(display, window, ConfigureNotify, &ev)) {}

        e->win_w = screen_w;
        e->win_h = screen_h;
        _apply_fullscreen_viewport(e);
        e->fullscreen = 1;
    } else {
        /* Restaura janela */
        e->render_w = e->saved_render_w;
        e->render_h = e->saved_render_h;

        hints.flags = 2; hints.decorations = 1;
        XChangeProperty(display, window, motif_atom, motif_atom,
                        32, PropModeReplace,
                        reinterpret_cast<unsigned char *>(&hints), 5);
        XMoveResizeWindow(display, window, 100, 100,
                          static_cast<unsigned int>(e->saved_win_w),
                          static_cast<unsigned int>(e->saved_win_h));
        XSync(display, False);

        e->win_w = e->saved_win_w;
        e->win_h = e->saved_win_h;

        /* Restaura projeção ortográfica diretamente */
        glViewport(0, 0, e->win_w, e->win_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, e->render_w, e->render_h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        e->fullscreen = 0;
    }
}

/* ---- Poll events ---------------------------------------------------------- */
void RendererGL::poll_events(Engine *e)
{
    /* Swap O(1) dos buffers de tecla */
    /* Troca os ponteiros do double-buffer de teclado em O(1) e copia o estado atual
       para o novo buffer "corrente", preservando o frame anterior em keys_prev */
    int *tmp   = keys_prev;
    keys_prev  = keys_cur;
    keys_cur   = tmp;
    memcpy(keys_cur, keys_prev, ENGINE_MAX_KEYS * sizeof(int));

    e->keys      = keys_cur;
    e->keys_prev = keys_prev;

    /* Salva o estado de botões do frame anterior e zera o scroll */
    memcpy(e->mouse.buttons_prev, e->mouse.buttons, sizeof(e->mouse.buttons));
    e->mouse.scroll = 0;

    XEvent ev;
    while (XPending(display) > 0) {
        XNextEvent(display, &ev);
        switch (ev.type) {
        case KeyPress:
        case KeyRelease: {
            const int idx = ev.xkey.keycode & (ENGINE_MAX_KEYS - 1);
            if (ev.type == KeyPress) {
                keys_cur[idx] = 1;
                if (XLookupKeysym(reinterpret_cast<XKeyEvent *>(&ev), 0) == XK_Escape)
                    e->running = 0;
            } else {
                keys_cur[idx] = 0;
            }
            break;
        }
        case ButtonPress:
            if      (ev.xbutton.button == Button1) e->mouse.buttons[0] = 1;
            else if (ev.xbutton.button == Button2) e->mouse.buttons[1] = 1;
            else if (ev.xbutton.button == Button3) e->mouse.buttons[2] = 1;
            else if (ev.xbutton.button == Button4) e->mouse.scroll     = +1;
            else if (ev.xbutton.button == Button5) e->mouse.scroll     = -1;
            break;
        case ButtonRelease:
            if      (ev.xbutton.button == Button1) e->mouse.buttons[0] = 0;
            else if (ev.xbutton.button == Button2) e->mouse.buttons[1] = 0;
            else if (ev.xbutton.button == Button3) e->mouse.buttons[2] = 0;
            break;
        case MotionNotify: {
            const int s    = _best_scale(e->win_w, e->win_h, e->render_w, e->render_h);
            const int ox   = (e->win_w - e->render_w * s) / 2;
            const int oy   = (e->win_h - e->render_h * s) / 2;
            e->mouse.x = (ev.xmotion.x - ox) / s;
            e->mouse.y = (ev.xmotion.y - oy) / s;
            break;
        }
        case Expose:
            setup_projection(e);
            break;
        case ConfigureNotify:
            if (ev.xconfigure.width  != e->win_w ||
                ev.xconfigure.height != e->win_h)
                resize(e, ev.xconfigure.width, ev.xconfigure.height);
            break;
        default:
            break;
        }
    }
}

/* ---- Privados ------------------------------------------------------------- */
/* ---- RendererGL — FBO ----------------------------------------------------- */

FboHandle RendererGL::fbo_create(Engine *e, int w, int h)
{
    if (!fbo_supported) {
        fprintf(stderr, "Engine: FBO não suportado neste driver.\n");
        return ENGINE_FBO_INVALID;
    }
    /* Encontra slot livre */
    int slot = ENGINE_FBO_INVALID;
    for (int i = 0; i < ENGINE_MAX_FBOS; ++i)
        if (!e->fbos[i].in_use) { slot = i; break; }
    if (slot == ENGINE_FBO_INVALID) {
        fprintf(stderr, "Engine: pool de FBOs esgotado.\n");
        return ENGINE_FBO_INVALID;
    }

    /* Cria textura de cor */
    GLuint color_tex;
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    /* Cria FBO e anexa a textura */
    GLuint fbo_id;
    glGenFramebuffers_f(1, &fbo_id);
    glBindFramebuffer_f(GL_FRAMEBUFFER, fbo_id);
    glFramebufferTexture2D_f(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, color_tex, 0);

    GLenum status = glCheckFramebufferStatus_f(GL_FRAMEBUFFER);
    glBindFramebuffer_f(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Engine: FBO incompleto (status=0x%x).\n", status);
        glDeleteFramebuffers_f(1, &fbo_id);
        glDeleteTextures(1, &color_tex);
        return ENGINE_FBO_INVALID;
    }

    FboData &fd   = e->fbos[slot];
    fd.fbo_id     = static_cast<unsigned int>(fbo_id);
    fd.color_tex  = static_cast<unsigned int>(color_tex);
    fd.width      = w;
    fd.height     = h;
    fd.in_use     = 1;
    return static_cast<FboHandle>(slot);
}

void RendererGL::fbo_destroy(Engine *e, FboHandle fh)
{
    if (!fbo_supported || fh < 0 || fh >= ENGINE_MAX_FBOS || !e->fbos[fh].in_use) return;
    _batch_flush_internal();
    FboData &fd = e->fbos[fh];
    GLuint fid  = static_cast<GLuint>(fd.fbo_id);
    GLuint tid  = static_cast<GLuint>(fd.color_tex);
    glDeleteFramebuffers_f(1, &fid);
    glDeleteTextures(1, &tid);
    fd.in_use = 0;
}

void RendererGL::fbo_bind(Engine *e, FboHandle fh)
{
    if (!fbo_supported || fh < 0 || fh >= ENGINE_MAX_FBOS || !e->fbos[fh].in_use) return;
    _batch_flush_internal();
    const FboData &fd = e->fbos[fh];
    glBindFramebuffer_f(GL_FRAMEBUFFER, static_cast<GLuint>(fd.fbo_id));
    glViewport(0, 0, fd.width, fd.height);
    /* Ajusta projeção ortográfica para o tamanho do FBO */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, fd.width, fd.height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void RendererGL::fbo_unbind(Engine *e)
{
    if (!fbo_supported) return;
    _batch_flush_internal();
    glBindFramebuffer_f(GL_FRAMEBUFFER, 0);
    setup_projection(e);   /* restaura viewport e projeção da janela */
}

unsigned int RendererGL::fbo_texture(Engine *e, FboHandle fh)
{
    if (fh < 0 || fh >= ENGINE_MAX_FBOS || !e->fbos[fh].in_use) return 0;
    return e->fbos[fh].color_tex;
}

/* ---- RendererGL — Shaders ------------------------------------------------- */

static GLuint _compile_shader(RendererGL *r, GLenum type, const char *src)
{
    GLuint s = r->glCreateShader_f(type);
    r->glShaderSource_f(s, 1, &src, nullptr);
    r->glCompileShader_f(s);
    GLint ok = 0;
    r->glGetShaderiv_f(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        r->glGetShaderInfoLog_f(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Engine shader compile error:\n%s\n", log);
        r->glDeleteShader_f(s);
        return 0;
    }
    return s;
}

ShaderHandle RendererGL::shader_create(Engine *e, const char *vert_src, const char *frag_src)
{
    if (!shaders_supported) {
        fprintf(stderr, "Engine: shaders GLSL não suportados.\n");
        return ENGINE_SHADER_INVALID;
    }
    int slot = ENGINE_SHADER_INVALID;
    for (int i = 0; i < ENGINE_MAX_SHADERS; ++i)
        if (!e->shaders[i].in_use) { slot = i; break; }
    if (slot == ENGINE_SHADER_INVALID) {
        fprintf(stderr, "Engine: pool de shaders esgotado.\n");
        return ENGINE_SHADER_INVALID;
    }

    GLuint vs = _compile_shader(this, GL_VERTEX_SHADER,   vert_src);
    GLuint fs = _compile_shader(this, GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader_f(vs);
        if (fs) glDeleteShader_f(fs);
        return ENGINE_SHADER_INVALID;
    }

    GLuint prog = glCreateProgram_f();
    glAttachShader_f(prog, vs);
    glAttachShader_f(prog, fs);
    glLinkProgram_f(prog);

    GLint ok = 0;
    glGetProgramiv_f(prog, GL_LINK_STATUS, &ok);
    glDeleteShader_f(vs);
    glDeleteShader_f(fs);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog_f(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Engine shader link error:\n%s\n", log);
        glDeleteProgram_f(prog);
        return ENGINE_SHADER_INVALID;
    }

    e->shaders[slot].program = static_cast<unsigned int>(prog);
    e->shaders[slot].in_use  = 1;
    return static_cast<ShaderHandle>(slot);
}

void RendererGL::shader_destroy(Engine *e, ShaderHandle sh)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    _batch_flush_internal();
    if (e->active_shader == sh) {
        glUseProgram_f(0);
        e->active_shader = ENGINE_SHADER_INVALID;
    }
    glDeleteProgram_f(static_cast<GLuint>(e->shaders[sh].program));
    e->shaders[sh].in_use = 0;
}

void RendererGL::shader_use(Engine *e, ShaderHandle sh)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    _batch_flush_internal();
    glUseProgram_f(static_cast<GLuint>(e->shaders[sh].program));
    e->active_shader = sh;
}

void RendererGL::shader_none(Engine *e)
{
    if (!shaders_supported) return;
    _batch_flush_internal();
    glUseProgram_f(0);
    e->active_shader = ENGINE_SHADER_INVALID;
}

void RendererGL::shader_set_int(Engine *e, ShaderHandle sh, const char *name, int v)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    GLuint prog = static_cast<GLuint>(e->shaders[sh].program);
    GLint  loc  = glGetUniformLocation_f(prog, name);
    if (loc >= 0) glUniform1i_f(loc, v);
}

void RendererGL::shader_set_float(Engine *e, ShaderHandle sh, const char *name, float v)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    GLuint prog = static_cast<GLuint>(e->shaders[sh].program);
    GLint  loc  = glGetUniformLocation_f(prog, name);
    if (loc >= 0) glUniform1f_f(loc, v);
}

void RendererGL::shader_set_vec2(Engine *e, ShaderHandle sh, const char *name, float x, float y)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    GLuint prog = static_cast<GLuint>(e->shaders[sh].program);
    GLint  loc  = glGetUniformLocation_f(prog, name);
    if (loc >= 0) glUniform2f_f(loc, x, y);
}

void RendererGL::shader_set_vec4(Engine *e, ShaderHandle sh, const char *name, float x, float y, float z, float w)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    GLuint prog = static_cast<GLuint>(e->shaders[sh].program);
    GLint  loc  = glGetUniformLocation_f(prog, name);
    if (loc >= 0) glUniform4f_f(loc, x, y, z, w);
}

/* ---- RendererGL — carregamento de procs ----------------------------------- */

bool RendererGL::_load_vbo_procs()
{
    auto get = [](const char *name) {
        return glXGetProcAddressARB(reinterpret_cast<const GLubyte *>(name));
    };
    glGenBuffers_f    = reinterpret_cast<PFNGLGENBUFFERSPROC>   (get("glGenBuffers"));
    glBindBuffer_f    = reinterpret_cast<PFNGLBINDBUFFERPROC>   (get("glBindBuffer"));
    glBufferData_f    = reinterpret_cast<PFNGLBUFFERDATAPROC>   (get("glBufferData"));
    glDeleteBuffers_f = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(get("glDeleteBuffers"));

    if (!glGenBuffers_f || !glBindBuffer_f || !glBufferData_f || !glDeleteBuffers_f) {
        fprintf(stderr, "Engine: OpenGL VBO não disponível (requer GL 1.5+).\n");
        return false;
    }
    return true;
}

bool RendererGL::_load_shader_procs()
{
    auto get = [](const char *name) {
        return glXGetProcAddressARB(reinterpret_cast<const GLubyte *>(name));
    };
#define LOAD(T, N) N##_f = reinterpret_cast<T>(get(#N))
    LOAD(PFNGLCREATESHADERPROC,       glCreateShader);
    LOAD(PFNGLSHADERSOURCEPROC,       glShaderSource);
    LOAD(PFNGLCOMPILESHADERPROC,      glCompileShader);
    LOAD(PFNGLGETSHADERIVPROC,        glGetShaderiv);
    LOAD(PFNGLGETSHADERINFOLOGPROC,   glGetShaderInfoLog);
    LOAD(PFNGLCREATEPROGRAMPROC,      glCreateProgram);
    LOAD(PFNGLATTACHSHADERPROC,       glAttachShader);
    LOAD(PFNGLLINKPROGRAMPROC,        glLinkProgram);
    LOAD(PFNGLGETPROGRAMIVPROC,       glGetProgramiv);
    LOAD(PFNGLGETPROGRAMINFOLOGPROC,  glGetProgramInfoLog);
    LOAD(PFNGLUSEPROGRAMPROC,         glUseProgram);
    LOAD(PFNGLDELETESHADERPROC,       glDeleteShader);
    LOAD(PFNGLDELETEPROGRAMPROC,      glDeleteProgram);
    LOAD(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    LOAD(PFNGLUNIFORM1IPROC,          glUniform1i);
    LOAD(PFNGLUNIFORM1FPROC,          glUniform1f);
    LOAD(PFNGLUNIFORM2FPROC,          glUniform2f);
    LOAD(PFNGLUNIFORM4FPROC,          glUniform4f);
#undef LOAD
    shaders_supported = (glCreateShader_f  && glCreateProgram_f &&
                          glUseProgram_f   && glGetUniformLocation_f);
    if (!shaders_supported)
        fprintf(stderr, "Engine: shaders GLSL não disponíveis (requer GL 2.0+).\n");
    return shaders_supported;
}

bool RendererGL::_load_fbo_procs()
{
    auto get = [](const char *name) {
        return glXGetProcAddressARB(reinterpret_cast<const GLubyte *>(name));
    };
    /* Tenta nomes ARB primeiro, depois sem sufixo (OpenGL 3.0 core) */
    glGenFramebuffers_f = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(
        get("glGenFramebuffersEXT") ? get("glGenFramebuffersEXT") : get("glGenFramebuffers"));
    glBindFramebuffer_f = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(
        get("glBindFramebufferEXT") ? get("glBindFramebufferEXT") : get("glBindFramebuffer"));
    glFramebufferTexture2D_f = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(
        get("glFramebufferTexture2DEXT") ? get("glFramebufferTexture2DEXT") : get("glFramebufferTexture2D"));
    glCheckFramebufferStatus_f = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(
        get("glCheckFramebufferStatusEXT") ? get("glCheckFramebufferStatusEXT") : get("glCheckFramebufferStatus"));
    glDeleteFramebuffers_f = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(
        get("glDeleteFramebuffersEXT") ? get("glDeleteFramebuffersEXT") : get("glDeleteFramebuffers"));

    fbo_supported = (glGenFramebuffers_f   && glBindFramebuffer_f &&
                     glFramebufferTexture2D_f && glCheckFramebufferStatus_f &&
                     glDeleteFramebuffers_f);
    if (!fbo_supported)
        fprintf(stderr, "Engine: FBO não disponível (requer GL_EXT_framebuffer_object ou GL 3.0+).\n");
    return fbo_supported;
}

void RendererGL::_batch_init()
{
    glGenBuffers_f(1, &batch.vbo);
    batch.quad_count  = 0;
    batch.current_tex = 0;
    batch_ready       = true;
}

void RendererGL::_batch_flush_internal()
{
    if (batch.quad_count == 0) return;

    const int vertex_count = batch.quad_count * BATCH_VERTICES_PER_QUAD;
    glBindBuffer_f(GL_ARRAY_BUFFER, batch.vbo);
    glBufferData_f(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(vertex_count * BATCH_FLOATS_PER_VERTEX * sizeof(float)),
                   batch.buf, GL_DYNAMIC_DRAW);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    const GLsizei stride = BATCH_FLOATS_PER_VERTEX * static_cast<GLsizei>(sizeof(float));
    glVertexPointer  (2, GL_FLOAT, stride, reinterpret_cast<const void *>(0 * sizeof(float)));
    glTexCoordPointer(2, GL_FLOAT, stride, reinterpret_cast<const void *>(2 * sizeof(float)));
    glColorPointer   (4, GL_FLOAT, stride, reinterpret_cast<const void *>(4 * sizeof(float)));

    glDrawArrays(GL_QUADS, 0, vertex_count);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindBuffer_f(GL_ARRAY_BUFFER, 0);

    batch.quad_count = 0;
}

void RendererGL::_batch_set_texture(unsigned int tex)
{
    if (tex == batch.current_tex) return;
    _batch_flush_internal();
    if (tex != last_tex) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex));
        last_tex = tex;
    }
    batch.current_tex = tex;
}

void RendererGL::_batch_push_quad_raw(float dx, float dy, float dw, float dh,
                                       float u0, float v0, float u1, float v1,
                                       float r, float g, float b, float a)
{
    if (batch.quad_count >= BATCH_MAX_QUADS) _batch_flush_internal();

    float *p = batch.buf + batch.quad_count * BATCH_FLOATS_PER_QUAD;

    /* Quad: 4 vértices em ordem CCW — topo-esquerdo, topo-direito, baixo-direito, baixo-esquerdo */
    /* v0: topo-esquerdo */
    *p++ = dx;      *p++ = dy;      *p++ = u0; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    /* v1: topo-direito */
    *p++ = dx + dw; *p++ = dy;      *p++ = u1; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    /* v2: baixo-direito */
    *p++ = dx + dw; *p++ = dy + dh; *p++ = u1; *p++ = v1; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    /* v3: baixo-esquerdo */
    *p++ = dx;      *p++ = dy + dh; *p++ = u0; *p++ = v1; *p++ = r; *p++ = g; *p++ = b; *p++ = a;

    ++batch.quad_count;
}

void RendererGL::_batch_push_quad_flip(float dx, float dy, float dw, float dh,
                                        float u0, float v0, float u1, float v1,
                                        int flip_h, int flip_v,
                                        float r, float g, float b, float a)
{
    const float fu0 = flip_h ? u1 : u0;
    const float fu1 = flip_h ? u0 : u1;
    const float fv0 = flip_v ? v1 : v0;
    const float fv1 = flip_v ? v0 : v1;
    _batch_push_quad_raw(dx, dy, dw, dh, fu0, fv0, fu1, fv1, r, g, b, a);
}

void RendererGL::_draw_quad_rotated(float dx, float dy, float dw, float dh,
                                     float u0, float v0, float u1, float v1,
                                     float r, float g, float b, float a,
                                     float rotation_deg, int flip_h, int flip_v)
{
    const float fu0 = flip_h ? u1 : u0;
    const float fu1 = flip_h ? u0 : u1;
    const float fv0 = flip_v ? v1 : v0;
    const float fv1 = flip_v ? v0 : v1;

    _batch_flush_internal();

    glPushMatrix();
    glTranslatef(dx + dw * 0.5f, dy + dh * 0.5f, 0.0f);   /* move a origem para o centro do quad */
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

void RendererGL::_build_keysym_cache(Engine *e)
{
    memset(e->ksym_cache.map, 0, sizeof(e->ksym_cache.map));
    int kc_min, kc_max;
    XDisplayKeycodes(display, &kc_min, &kc_max);
    for (int kc = kc_min; kc <= kc_max; ++kc)
        e->ksym_cache.map[kc & (ENGINE_MAX_KEYS - 1)] =
            XKeycodeToKeysym(display, static_cast<KeyCode>(kc), 0);
}

#endif /* ENGINE_BACKEND_GL */

/* =============================================================================
 * Factory de renderers
 *
 * engine_create_renderer() instancia o backend concreto baseado em backend_id.
 * Os headers dos backends são incluídos aqui (não em Engine.hpp) para que o
 * código de plataforma (DX11, VK) não vaze para unidades de compilação do jogo.
 * ============================================================================= */

/* Inclui apenas os backends habilitados em tempo de compilação */
#ifdef ENGINE_BACKEND_VK
#  include "RendererVK.hpp"
#endif
#ifdef ENGINE_BACKEND_DX11
#  include "RendererDX11.hpp"
#endif

IRenderer *engine_create_renderer(int backend_id)
{
    switch (backend_id) {
#ifdef ENGINE_BACKEND_GL
    case ENGINE_BACKEND_ID_GL:
        return new (std::nothrow) RendererGL();
#endif
#ifdef ENGINE_BACKEND_DX11
    case ENGINE_BACKEND_ID_DX11:
        return new (std::nothrow) RendererDX11();
#endif
#ifdef ENGINE_BACKEND_VK
    case ENGINE_BACKEND_ID_VK:
        return new (std::nothrow) RendererVK();
#endif
    default:
        fprintf(stderr, "Engine: backend_id=%d não compilado neste binário.\n",
                backend_id);
        return nullptr;
    }
}

/* =============================================================================
 * Macro RENDERER e helpers globais de renderização
 * ============================================================================= */

/* RENDERER(e) — acessa o IRenderer concreto a partir do void* opaco da Engine */
#define RENDERER(e)  (static_cast<IRenderer *>((e)->renderer_impl))

/*
 * _uv_for_tile() — calcula UVs normalizados (0..1) para uma região retangular
 * dentro de uma textura.  tex_w/tex_h em float para evitar conversões no caller.
 */
static inline void _uv_for_tile(float tex_w, float tex_h,
                                  int tile_x, int tile_y,
                                  int tile_w, int tile_h,
                                  float *u0, float *v0,
                                  float *u1, float *v1)
{
    const float inv_w = 1.0f / tex_w;
    const float inv_h = 1.0f / tex_h;
    *u0 = static_cast<float>(tile_x)           * inv_w;
    *v0 = static_cast<float>(tile_y)           * inv_h;
    *u1 = static_cast<float>(tile_x + tile_w)  * inv_w;
    *v1 = static_cast<float>(tile_y + tile_h)  * inv_h;
}

/* Sobrecarga que recebe SpriteData diretamente */
static inline void _uv_for_tile(const SpriteData *sp,
                                  int tile_x, int tile_y,
                                  int tile_w, int tile_h,
                                  float *u0, float *v0,
                                  float *u1, float *v1)
{
    _uv_for_tile(static_cast<float>(sp->width),
                 static_cast<float>(sp->height),
                 tile_x, tile_y, tile_w, tile_h,
                 u0, v0, u1, v1);
}

/* =============================================================================
 * API pública — extern "C"
 *
 * Implementação de todas as funções declaradas em Engine.hpp.
 * Organização: ciclo de vida → sprites → objetos → câmera → colisão
 *              → partículas → animadores → fade → renderização → input → áudio.
 * ============================================================================= */
extern "C" {

/* --- Ciclo de vida --------------------------------------------------------- */

int engine_init(Engine *e, int width, int height,
                const char *title, int scale)
{
    memset(e, 0, sizeof(Engine));

    scale = _clampf(static_cast<float>(scale), 1.0f, 8.0f) > 0
                ? (scale < 1 ? 1 : (scale > 8 ? 8 : scale))
                : 1;

    e->render_w = width;
    e->render_h = height;
    e->scale    = scale;
    e->win_w    = width  * scale;
    e->win_h    = height * scale;
    e->depth    = 24;
    e->running  = 1;

    e->camera.zoom    = 1.0f;
    e->camera_enabled = 0;
    e->fade_alpha     = 0.0f;
    e->fade_target    = 0.0f;
    e->fade_speed     = 1.0f;

    clock_gettime(CLOCK_MONOTONIC, &s_time_origin);
    s_time_prev = s_time_origin;

#if defined(ENGINE_BACKEND_GL)
    e->backend_id = ENGINE_BACKEND_ID_GL;
#elif defined(ENGINE_BACKEND_DX11)
    e->backend_id = ENGINE_BACKEND_ID_DX11;
#elif defined(ENGINE_BACKEND_VK)
    e->backend_id = ENGINE_BACKEND_ID_VK;
#endif

    IRenderer *r = engine_create_renderer(e->backend_id);
    if (!r) return 0;

    e->renderer_impl = r;
    if (!r->init(e, e->win_w, e->win_h, title, scale)) {
        delete r;
        e->renderer_impl = nullptr;
        return 0;
    }
    return 1;
}

void engine_destroy(Engine *e)
{
    if (!e->renderer_impl) return;
    _png_cache_clear();
    IRenderer *r = RENDERER(e);
    r->destroy(e);
    delete r;
    e->renderer_impl = nullptr;
}

void engine_set_background(Engine *e, int r, int g, int b)
{
    e->bg_color = _pack_color(r, g, b);
    /* Delega ao renderer, que armazena a cor e a aplica em clear() do próximo frame */
    if (e->renderer_impl)
        RENDERER(e)->set_clear_color(r * (1.0f / 255.0f),
                                     g * (1.0f / 255.0f),
                                     b * (1.0f / 255.0f));
}

/* --- Temporização ---------------------------------------------------------- */

double engine_get_time(Engine *)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return _ts_to_secs(now) - _ts_to_secs(s_time_origin);
}

double engine_get_delta(Engine *e) { return e->delta_time; }

/* --- Sprites --------------------------------------------------------------- */

int engine_load_sprite(Engine *e, const char *path)
{
    if (e->sprite_count >= ENGINE_MAX_SPRITES) return -1;

    unsigned int w, h;
    unsigned char *data = _load_png_rgba(path, &w, &h);
    if (!data) return -1;

    const int   sid = e->sprite_count;
    SpriteData *sp  = &e->sprites[sid];
    sp->texture = RENDERER(e)->upload_texture(data, w, h);
    sp->width   = static_cast<int>(w);
    sp->height  = static_cast<int>(h);
    sp->loaded  = 1;
    free(data);
    ++e->sprite_count;
    return sid;
}

int engine_load_sprite_region(Engine *e, const char *path,
                               int x, int y, int w, int h)
{
    if (e->sprite_count >= ENGINE_MAX_SPRITES) return -1;
    if (x < 0 || y < 0 || w <= 0 || h <= 0)  return -1;

    unsigned int img_w, img_h;
    const unsigned char *img_data = _get_png_cached(path, &img_w, &img_h);
    if (!img_data) return -1;
    if (static_cast<unsigned int>(x + w) > img_w ||
        static_cast<unsigned int>(y + h) > img_h) return -1;

    const size_t full_row  = img_w * 4u;
    const size_t reg_row   = static_cast<size_t>(w) * 4u;

    unsigned char *region = static_cast<unsigned char *>(
        malloc(static_cast<size_t>(h) * reg_row));
    if (!region) return -1;

    for (int row = 0; row < h; ++row)
        memcpy(region + static_cast<size_t>(row) * reg_row,
               img_data + static_cast<size_t>(y + row) * full_row
                        + static_cast<size_t>(x) * 4u,
               reg_row);

    const int   sid = e->sprite_count;
    SpriteData *sp  = &e->sprites[sid];
    sp->texture = RENDERER(e)->upload_texture(region,
                                               static_cast<unsigned int>(w),
                                               static_cast<unsigned int>(h));
    sp->width  = w;
    sp->height = h;
    sp->loaded = 1;
    free(region);
    ++e->sprite_count;
    return sid;
}

/* Forward declarations — definidas mais abaixo na seção Spatial Grid */
static void _sg_insert_object(Engine *e, int oid);
static void _sg_remove_object(Engine *e, int oid);

/* --- Objetos --------------------------------------------------------------- */

int engine_add_object(Engine *e, int x, int y, int sprite_id,
                       int width, int height, int r, int g, int b)
{
    if (e->object_count >= ENGINE_MAX_OBJECTS) return -1;

    const int oid   = e->object_count++;
    GameObject *obj = &e->objects[oid];
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
    obj->z_order   = oid;
    if (e->sgrid.enabled)
        _sg_insert_object(e, oid);
    return oid;
}

int engine_add_tile_object(Engine *e, int x, int y, int sprite_id,
                            int tile_x, int tile_y, int tile_w, int tile_h)
{
    const int oid = engine_add_object(e, x, y, sprite_id,
                                       tile_w, tile_h, 255, 255, 255);
    if (oid < 0) return -1;
    e->objects[oid].use_tile = 1;
    e->objects[oid].tile_x   = tile_x;
    e->objects[oid].tile_y   = tile_y;
    e->objects[oid].tile_w   = tile_w;
    e->objects[oid].tile_h   = tile_h;
    return oid;
}

void engine_move_object(Engine *e, int oid, int dx, int dy)
{
    if (_oid_valid(e, oid)) {
        if (e->sgrid.enabled) _sg_remove_object(e, oid);
        e->objects[oid].x += dx;
        e->objects[oid].y += dy;
        if (e->sgrid.enabled) _sg_insert_object(e, oid);
    }
}
void engine_set_object_pos(Engine *e, int oid, int x, int y)
{
    if (_oid_valid(e, oid)) {
        if (e->sgrid.enabled) _sg_remove_object(e, oid);
        e->objects[oid].x = x;
        e->objects[oid].y = y;
        if (e->sgrid.enabled) _sg_insert_object(e, oid);
    }
}
void engine_set_object_sprite(Engine *e, int oid, int sprite_id)
{
    if (_oid_valid(e, oid)) e->objects[oid].sprite_id = sprite_id;
}
void engine_get_object_pos(Engine *e, int oid, int *out_x, int *out_y)
{
    if (_oid_valid(e, oid)) { *out_x = e->objects[oid].x; *out_y = e->objects[oid].y; }
    else                    { *out_x = 0; *out_y = 0; }
}
void engine_set_object_tile(Engine *e, int oid, int tile_x, int tile_y)
{
    if (_oid_valid(e, oid)) {
        e->objects[oid].tile_x = tile_x;
        e->objects[oid].tile_y = tile_y;
    }
}
void engine_set_object_flip(Engine *e, int oid, int flip_h, int flip_v)
{
    if (_oid_valid(e, oid)) {
        e->objects[oid].flip_h = flip_h;
        e->objects[oid].flip_v = flip_v;
    }
}
void engine_set_object_scale(Engine *e, int oid, float sx, float sy)
{
    if (_oid_valid(e, oid)) { e->objects[oid].scale_x = sx; e->objects[oid].scale_y = sy; }
}
void engine_set_object_rotation(Engine *e, int oid, float degrees)
{
    if (_oid_valid(e, oid)) e->objects[oid].rotation = degrees;
}
void engine_set_object_alpha(Engine *e, int oid, float alpha)
{
    if (_oid_valid(e, oid)) e->objects[oid].alpha = _clampf(alpha, 0.0f, 1.0f);
}
void engine_set_object_layer(Engine *e, int oid, int layer, int z_order)
{
    if (_oid_valid(e, oid)) {
        e->objects[oid].layer   = layer;
        e->objects[oid].z_order = z_order;
    }
}
void engine_set_object_hitbox(Engine *e, int oid,
                               int offset_x, int offset_y,
                               int width, int height)
{
    if (!_oid_valid(e, oid)) return;
    Hitbox &hb    = e->objects[oid].hitbox;
    hb.offset_x   = offset_x;
    hb.offset_y   = offset_y;
    hb.width      = width;
    hb.height     = height;
    hb.enabled    = 1;
}
void engine_remove_object(Engine *e, int oid)
{
    if (_oid_valid(e, oid)) {
        if (e->sgrid.enabled) _sg_remove_object(e, oid);
        e->objects[oid].active = 0;
    }
}

/* ---- Câmera --------------------------------------------------------------- */

void engine_camera_set(Engine *e, float x, float y)    { e->camera.x = x; e->camera.y = y; }
void engine_camera_move(Engine *e, float dx, float dy) { e->camera.x += dx; e->camera.y += dy; }
void engine_camera_zoom(Engine *e, float zoom)         { e->camera.zoom = zoom < 0.1f ? 0.1f : zoom; }
void engine_camera_enable(Engine *e, int enabled)      { e->camera_enabled = enabled; }

void engine_camera_follow(Engine *e, int oid, float lerp_speed)
{
    if (!_oid_valid(e, oid)) return;
    const float t  = _clampf(lerp_speed, 0.0f, 1.0f);
    const float tx = static_cast<float>(e->objects[oid].x) - static_cast<float>(e->render_w) * 0.5f;
    const float ty = static_cast<float>(e->objects[oid].y) - static_cast<float>(e->render_h) * 0.5f;
    e->camera.x = _lerpf(e->camera.x, tx, t);
    e->camera.y = _lerpf(e->camera.y, ty, t);
}

void engine_camera_shake(Engine *e, float intensity, float duration)
{
    e->camera.shake_intensity = intensity;
    e->camera.shake_duration  = duration;
    e->camera.shake_timer     = duration;
}

void engine_world_to_screen(Engine *e, float wx, float wy, float *sx, float *sy)
{
    *sx = (wx - e->camera.x) * e->camera.zoom + e->camera.shake_x;
    *sy = (wy - e->camera.y) * e->camera.zoom + e->camera.shake_y;
}
void engine_screen_to_world(Engine *e, float sx, float sy, float *wx, float *wy)
{
    *wx = (sx - e->camera.shake_x) / e->camera.zoom + e->camera.x;
    *wy = (sy - e->camera.shake_y) / e->camera.zoom + e->camera.y;
}

/* --- Colisão AABB ---------------------------------------------------------- */

/*
 * _get_hitbox() — retorna a hitbox efetiva do objeto em coordenadas de mundo.
 * Prioridade: hitbox customizada > tile_w/h > sprite width/height > width/height.
 */
static void _get_hitbox(const Engine *e, int oid,
                         int *hx, int *hy, int *hw, int *hh)
{
    const GameObject &o = e->objects[oid];
    if (o.hitbox.enabled) {
        *hx = o.x + o.hitbox.offset_x;
        *hy = o.y + o.hitbox.offset_y;
        *hw = o.hitbox.width;
        *hh = o.hitbox.height;
    } else {
        *hx = o.x;
        *hy = o.y;
        if (o.use_tile) {
            *hw = o.tile_w;
            *hh = o.tile_h;
        } else if (o.sprite_id >= 0 && o.sprite_id < e->sprite_count) {
            *hw = e->sprites[o.sprite_id].width;
            *hh = e->sprites[o.sprite_id].height;
        } else {
            *hw = o.width;
            *hh = o.height;
        }
    }
}

/* Teste AABB puro — usado internamente por todas as funções de colisão */
static inline int _aabb(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh)
{
    return (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by) ? 1 : 0;
}

int engine_check_collision(Engine *e, int oid1, int oid2)
{
    if (!_oid_valid(e, oid1) || !_oid_valid(e, oid2))           return 0;
    if (!e->objects[oid1].active || !e->objects[oid2].active)   return 0;
    int ax, ay, aw, ah, bx, by, bw, bh;
    _get_hitbox(e, oid1, &ax, &ay, &aw, &ah);
    _get_hitbox(e, oid2, &bx, &by, &bw, &bh);
    return _aabb(ax, ay, aw, ah, bx, by, bw, bh);
}

int engine_check_collision_rect(Engine *e, int oid, int rx, int ry, int rw, int rh)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return 0;
    int ax, ay, aw, ah;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return _aabb(ax, ay, aw, ah, rx, ry, rw, rh);
}

int engine_check_collision_point(Engine *e, int oid, int px, int py)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return 0;
    int ax, ay, aw, ah;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return (px >= ax && px < ax + aw && py >= ay && py < ay + ah) ? 1 : 0;
}

/* =============================================================================
 * Spatial Grid — implementação
 *
 * Arquitetura:
 *   • Grid uniforme de COLS×ROWS células, cada uma de cell_size×cell_size px.
 *   • SpatialCell.oids[] é um array fixo (sem heap) de no máximo
 *     ENGINE_SGRID_BUCKET_CAP entradas.
 *   • SpatialObjEntry rastreia em quais células cada objeto está inscrito
 *     (máximo ENGINE_SGRID_OBJ_MAX_CELLS = 4; objetos grandes que cruzam
 *     mais de 4 células são tratados pelo fallback bruto).
 *   • Inserção: clamp das coordenadas da hitbox → loop sobre células cobertas.
 *   • Remoção: percorre obj_cells[oid].cell_idx[] e remove o oid de cada célula.
 *   • Query de rect: itera apenas as células sobrepostas, acumula candidatos
 *     com deduplicação via bitmask em stack (seen[]).
 *
 * Complexidade:
 *   insert/remove: O(k)   onde k = número de células cobertas pelo objeto (≤4)
 *   query:         O(k·b) onde b = ocupação média da célula (ENGINE_SGRID_BUCKET_CAP)
 *   vs. bruto:     O(N)   onde N = ENGINE_MAX_OBJECTS (256)
 * ============================================================================= */

/* Converte coordenada de mundo para índice de coluna/linha; clampado ao grid */
static inline int _sg_col(const SpatialGrid *g, int wx)
{
    int c = wx / g->cell_size;
    return c < 0 ? 0 : (c >= g->cols ? g->cols - 1 : c);
}
static inline int _sg_row(const SpatialGrid *g, int wy)
{
    int r = wy / g->cell_size;
    return r < 0 ? 0 : (r >= g->rows ? g->rows - 1 : r);
}
static inline int _sg_idx(const SpatialGrid *g, int col, int row)
{
    return row * g->cols + col;
}

/* Remove oid de uma célula específica (busca linear; células pequenas) */
static void _sg_cell_remove(SpatialCell *cell, int oid)
{
    for (int i = 0; i < cell->count; ++i) {
        if (cell->oids[i] == oid) {
            /* Substitui pela última entrada e reduz count */
            cell->oids[i] = cell->oids[--cell->count];
            return;
        }
    }
}

/* Insere oid numa célula; retorna 1 se ok, 0 se cheia */
static int _sg_cell_insert(SpatialCell *cell, int oid)
{
    if (cell->count >= ENGINE_SGRID_BUCKET_CAP) return 0;
    cell->oids[cell->count++] = oid;
    return 1;
}

/*
 * _sg_insert_object() — insere um objeto em todas as células que sua hitbox cobre.
 * Limita-se a ENGINE_SGRID_OBJ_MAX_CELLS células; se o objeto for muito grande
 * ultrapassa esse limite e apenas as primeiras células são registradas.
 */
static void _sg_insert_object(Engine *e, int oid)
{
    SpatialGrid *g = &e->sgrid;
    SpatialObjEntry *entry = &g->obj_cells[oid];
    entry->count = 0;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);

    const int c0 = _sg_col(g, hx);
    const int c1 = _sg_col(g, hx + hw - 1);
    const int r0 = _sg_row(g, hy);
    const int r1 = _sg_row(g, hy + hh - 1);

    for (int r = r0; r <= r1 && entry->count < ENGINE_SGRID_OBJ_MAX_CELLS; ++r) {
        for (int c = c0; c <= c1 && entry->count < ENGINE_SGRID_OBJ_MAX_CELLS; ++c) {
            const int idx = _sg_idx(g, c, r);
            if (_sg_cell_insert(&g->cells[idx], oid)) {
                entry->cell_idx[entry->count++] = idx;
            } else {
                fprintf(stderr,
                    "SpatialGrid: célula [%d,%d] cheia (cap=%d). Aumente ENGINE_SGRID_BUCKET_CAP.\n",
                    c, r, ENGINE_SGRID_BUCKET_CAP);
            }
        }
    }
}

/* Remove o objeto de todas as células onde está registrado */
static void _sg_remove_object(Engine *e, int oid)
{
    SpatialGrid *g = &e->sgrid;
    SpatialObjEntry *entry = &g->obj_cells[oid];
    for (int i = 0; i < entry->count; ++i)
        _sg_cell_remove(&g->cells[entry->cell_idx[i]], oid);
    entry->count = 0;
}

/* --- API pública do Spatial Grid ------------------------------------------ */

void engine_sgrid_init(Engine *e, int cell_size)
{
    SpatialGrid *g = &e->sgrid;
    memset(g, 0, sizeof(SpatialGrid));
    g->cell_size = (cell_size > 0) ? cell_size : ENGINE_SGRID_CELL_SIZE;
    g->cols      = ENGINE_SGRID_COLS;
    g->rows      = ENGINE_SGRID_ROWS;
    g->enabled   = 1;
    g->dirty     = 0;
}

void engine_sgrid_destroy(Engine *e)
{
    memset(&e->sgrid, 0, sizeof(SpatialGrid));
    /* enabled permanece 0 após memset */
}

void engine_sgrid_rebuild(Engine *e)
{
    SpatialGrid *g = &e->sgrid;
    if (!g->enabled) return;

    /* Zera todas as células e entradas de objetos */
    for (int i = 0; i < ENGINE_SGRID_TOTAL_CELLS; ++i)
        g->cells[i].count = 0;
    for (int i = 0; i < ENGINE_MAX_OBJECTS; ++i)
        g->obj_cells[i].count = 0;

    /* Reinsere todos os objetos ativos */
    for (int i = 0; i < e->object_count; ++i) {
        if (e->objects[i].active)
            _sg_insert_object(e, i);
    }
    g->dirty = 0;
}

void engine_sgrid_insert_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid) || !e->objects[oid].active) return;
    _sg_insert_object(e, oid);
}

void engine_sgrid_remove_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid)) return;
    _sg_remove_object(e, oid);
}

void engine_sgrid_update_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid)) return;
    _sg_remove_object(e, oid);
    if (e->objects[oid].active)
        _sg_insert_object(e, oid);
}

/*
 * _sgrid_query_rect_internal() — acumula candidatos numa área retangular.
 *
 * Usa um bitmask stack-allocated (seen[]) para deduplicação em O(1):
 *   seen[oid >> 5] & (1 << (oid & 31))
 * ENGINE_MAX_OBJECTS = 256 → seen[8] (32 bytes na stack).
 */
static int _sgrid_query_rect_internal(Engine *e,
                                       int x, int y, int w, int h,
                                       int *out_oids, int cap)
{
    SpatialGrid *g = &e->sgrid;
    const int c0 = _sg_col(g, x);
    const int c1 = _sg_col(g, x + w - 1);
    const int r0 = _sg_row(g, y);
    const int r1 = _sg_row(g, y + h - 1);

    unsigned int seen[(ENGINE_MAX_OBJECTS + 31) / 32] = {};
    int found = 0;

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            const SpatialCell *cell = &g->cells[_sg_idx(g, c, r)];
            for (int k = 0; k < cell->count; ++k) {
                const int oid = cell->oids[k];
                if (oid < 0 || oid >= ENGINE_MAX_OBJECTS) continue;
                const unsigned int mask = 1u << (oid & 31);
                if (seen[oid >> 5] & mask) continue;   /* já visto */
                seen[oid >> 5] |= mask;
                if (found < cap) out_oids[found++] = oid;
            }
        }
    }
    return found;
}

int engine_sgrid_query_rect(Engine *e, int x, int y, int w, int h,
                             int *out_oids, int cap)
{
    if (!e->sgrid.enabled || cap <= 0) return 0;
    return _sgrid_query_rect_internal(e, x, y, w, h, out_oids, cap);
}

int engine_sgrid_query_object(Engine *e, int oid, int *out_oids, int cap)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid) || cap <= 0) return 0;
    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);
    return _sgrid_query_rect_internal(e, hx, hy, hw, hh, out_oids, cap);
}

int engine_sgrid_query_point(Engine *e, int px, int py,
                              int *out_oids, int cap)
{
    if (!e->sgrid.enabled || cap <= 0) return 0;
    return _sgrid_query_rect_internal(e, px, py, 1, 1, out_oids, cap);
}

int engine_sgrid_first_collision(Engine *e, int oid)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return -1;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);

    if (e->sgrid.enabled) {
        /* Caminho rápido: consulta o grid */
        int candidates[ENGINE_SGRID_BUCKET_CAP * ENGINE_SGRID_OBJ_MAX_CELLS];
        const int n = _sgrid_query_rect_internal(e, hx, hy, hw, hh,
                                                   candidates,
                                                   (int)(sizeof(candidates)/sizeof(candidates[0])));
        for (int i = 0; i < n; ++i) {
            const int other = candidates[i];
            if (other == oid || !e->objects[other].active) continue;
            int bx, by, bw, bh;
            _get_hitbox(e, other, &bx, &by, &bw, &bh);
            if (_aabb(hx, hy, hw, hh, bx, by, bw, bh)) return other;
        }
        return -1;
    }

    /* Fallback bruto quando o grid está desativado */
    for (int i = 0; i < e->object_count; ++i) {
        if (i == oid || !e->objects[i].active) continue;
        int bx, by, bw, bh;
        _get_hitbox(e, i, &bx, &by, &bw, &bh);
        if (_aabb(hx, hy, hw, hh, bx, by, bw, bh)) return i;
    }
    return -1;
}

int engine_sgrid_all_collisions(Engine *e, int oid, int *out_oids, int cap)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active || cap <= 0) return 0;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);
    int found = 0;

    if (e->sgrid.enabled) {
        int candidates[ENGINE_SGRID_BUCKET_CAP * ENGINE_SGRID_OBJ_MAX_CELLS];
        const int n = _sgrid_query_rect_internal(e, hx, hy, hw, hh,
                                                   candidates,
                                                   (int)(sizeof(candidates)/sizeof(candidates[0])));
        for (int i = 0; i < n && found < cap; ++i) {
            const int other = candidates[i];
            if (other == oid || !e->objects[other].active) continue;
            int bx, by, bw, bh;
            _get_hitbox(e, other, &bx, &by, &bw, &bh);
            if (_aabb(hx, hy, hw, hh, bx, by, bw, bh))
                out_oids[found++] = other;
        }
        return found;
    }

    /* Fallback bruto */
    for (int i = 0; i < e->object_count && found < cap; ++i) {
        if (i == oid || !e->objects[i].active) continue;
        int bx, by, bw, bh;
        _get_hitbox(e, i, &bx, &by, &bw, &bh);
        if (_aabb(hx, hy, hw, hh, bx, by, bw, bh))
            out_oids[found++] = i;
    }
    return found;
}

/* --- Partículas ------------------------------------------------------------ */

/*
 * _spawn_particle() — inicializa uma partícula em um slot livre do pool.
 *
 * Usa ring buffer a partir de particle_next para distribuir slots uniformemente.
 * particle_count funciona como high-water mark; nunca decresce.
 */
static void _spawn_particle(Engine *e, const ParticleEmitter *em)
{
    /* Percorre o ring buffer a partir do cursor; para no primeiro slot inativo */
    int idx = -1;
    for (int i = 0; i < ENGINE_MAX_PARTICLES; ++i) {
        const int candidate = (e->particle_next + i) % ENGINE_MAX_PARTICLES;
        if (!e->particles[candidate].active) {
            idx = candidate;
            e->particle_next = (candidate + 1) % ENGINE_MAX_PARTICLES;
            /* Atualiza o high-water mark para que o loop de draw cubra este slot */
            if (candidate + 1 > e->particle_count)
                e->particle_count = candidate + 1;
            break;
        }
    }
    if (idx < 0) return;   /* sem slots livres */

    /* Gera três valores aleatórios normalizados em [0, 1] para velocidade e vida */
    const float inv_rand = 1.0f / static_cast<float>(RAND_MAX);
    const float t1 = static_cast<float>(rand()) * inv_rand;
    const float t2 = static_cast<float>(rand()) * inv_rand;
    const float t3 = static_cast<float>(rand()) * inv_rand;

    Particle &p = e->particles[idx];
    p.x    = em->x;
    p.y    = em->y;
    p.vx   = em->vx_min + t1 * (em->vx_max - em->vx_min);
    p.vy   = em->vy_min + t2 * (em->vy_max - em->vy_min);
    p.ax   = em->ax;
    p.ay   = em->ay;
    p.life = p.life_max = em->life_min + t3 * (em->life_max - em->life_min);
    p.size_start = em->size_start;
    p.size_end   = em->size_end;
    p.r0 = em->r0; p.g0 = em->g0; p.b0 = em->b0; p.a0 = em->a0;
    p.r1 = em->r1; p.g1 = em->g1; p.b1 = em->b1; p.a1 = em->a1;
    p.sprite_id = em->sprite_id;
    p.active    = 1;
}

int engine_emitter_add(Engine *e, ParticleEmitter *cfg)
{
    if (e->emitter_count >= 16) return -1;
    const int eid         = e->emitter_count++;
    e->emitters[eid]      = *cfg;
    e->emitters[eid].active = 1;
    e->emitters[eid]._acc   = 0.0f;
    return eid;
}

void engine_emitter_set_pos(Engine *e, int eid, float x, float y)
{
    if (_eid_valid(e, eid)) { e->emitters[eid].x = x; e->emitters[eid].y = y; }
}

void engine_emitter_burst(Engine *e, int eid, int count)
{
    if (!_eid_valid(e, eid)) return;
    for (int i = 0; i < count; ++i)
        _spawn_particle(e, &e->emitters[eid]);
}

void engine_emitter_remove(Engine *e, int eid)
{
    if (_eid_valid(e, eid)) e->emitters[eid].active = 0;
}

void engine_particles_update(Engine *e, float dt)
{
    /* Atualiza emissores */
    for (int eid = 0; eid < e->emitter_count; ++eid) {
        ParticleEmitter &em = e->emitters[eid];
        if (!em.active || em.rate <= 0) continue;
        em._acc += dt;
        const float rate_inv = 1.0f / static_cast<float>(em.rate);
        while (em._acc >= rate_inv) {
            _spawn_particle(e, &em);
            em._acc -= rate_inv;
        }
    }

    /* Atualiza partículas ativas */
    for (int i = 0; i < e->particle_count; ++i) {
        Particle &p = e->particles[i];
        if (!p.active) continue;
        p.vx += p.ax * dt;
        p.vy += p.ay * dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.life -= dt;
        if (p.life <= 0.0f) p.active = 0;
    }
}

void engine_particles_draw(Engine *e)
{
    IRenderer *r = RENDERER(e);
    for (int i = 0; i < e->particle_count; ++i) {
        const Particle &p = e->particles[i];
        if (!p.active) continue;

        const float t    = (p.life_max > 0.0f) ? (1.0f - p.life / p.life_max) : 1.0f;
        const float size = _lerpf(p.size_start, p.size_end, t);
        const unsigned int tex =
            (_sid_valid(e, p.sprite_id) && e->sprites[p.sprite_id].loaded)
                ? e->sprites[p.sprite_id].texture
                : e->white_tex;

        r->set_texture(e, tex);
        QuadParams q{ p.x, p.y, size, size, 0,0,1,1,
                      _lerpf(p.r0, p.r1, t),
                      _lerpf(p.g0, p.g1, t),
                      _lerpf(p.b0, p.b1, t),
                      _lerpf(p.a0, p.a1, t),
                      0, 0, 0 };
        r->push_quad(e, q);
    }
    r->flush(e);
}

/* --- Animadores de sprite -------------------------------------------------- */

int engine_animator_add(Engine *e, int *sprite_ids, int frame_count,
                         float fps, int loop, int object_id)
{
    if (!_aid_valid(e, e->animator_count)) return -1;   /* espaço disponível? */
    if (frame_count <= 0 || frame_count > 32) return -1;

    const int  aid = e->animator_count++;
    Animator  &a   = e->animators[aid];
    memset(&a, 0, sizeof(Animator));
    memcpy(a.sprite_ids, sprite_ids, static_cast<size_t>(frame_count) * sizeof(int));
    a.frame_count = frame_count;
    a.fps         = fps > 0.0f ? fps : 10.0f;
    a.frame_dur   = 1.0f / a.fps;
    a.loop        = loop;
    a.object_id   = object_id;
    return aid;
}

void engine_animator_play (Engine *e, int aid) { if (_aid_valid(e, aid)) e->animators[aid].finished = 0; }
void engine_animator_stop (Engine *e, int aid) { if (_aid_valid(e, aid)) e->animators[aid].finished = 1; }

void engine_animator_reset(Engine *e, int aid)
{
    if (!_aid_valid(e, aid)) return;
    Animator &a      = e->animators[aid];
    a.current_frame  = 0;
    a.timer          = 0.0f;
    a.finished       = 0;
}

int engine_animator_finished(Engine *e, int aid)
{
    return (!_aid_valid(e, aid)) ? 1 : e->animators[aid].finished;
}

void engine_animators_update(Engine *e, float dt)
{
    for (int i = 0; i < e->animator_count; ++i) {
        Animator &a = e->animators[i];
        if (a.finished) continue;
        a.timer += dt;
        if (a.timer < a.frame_dur) continue;
        a.timer -= a.frame_dur;
        if (++a.current_frame >= a.frame_count) {
            if (a.loop) {
                a.current_frame = 0;
            } else {
                a.current_frame = a.frame_count - 1;
                a.finished = 1;
            }
        }
        if (_oid_valid(e, a.object_id))
            e->objects[a.object_id].sprite_id = a.sprite_ids[a.current_frame];
    }
}

/* --- Fade de tela ---------------------------------------------------------- */

void engine_fade_to(Engine *e, float target_alpha, float speed, int r, int g, int b)
{
    e->fade_target = _clampf(target_alpha, 0.0f, 1.0f);
    e->fade_speed  = speed > 0.0f ? speed : 1.0f;
    e->fade_r = r; e->fade_g = g; e->fade_b = b;
}

void engine_fade_draw(Engine *e)
{
    if (e->fade_alpha <= 0.0f) return;
    IRenderer *r = RENDERER(e);
    r->set_texture(e, e->white_tex);
    QuadParams q{ 0.0f, 0.0f,
                  static_cast<float>(e->render_w), static_cast<float>(e->render_h),
                  0,0,1,1,
                  e->fade_r * (1.0f / 255.0f),
                  e->fade_g * (1.0f / 255.0f),
                  e->fade_b * (1.0f / 255.0f),
                  e->fade_alpha, 0, 0, 0 };
    r->push_quad(e, q);
    r->flush(e);
}

int engine_fade_done(Engine *e)
{
    const float diff = e->fade_target - e->fade_alpha;
    return (diff < 0.0f ? -diff : diff) < 0.01f ? 1 : 0;
}

/* --- engine_update() — tick central do frame ------------------------------- *
 *
 * Atualiza temporização, camera shake, fade, partículas e animadores.
 * Deve ser chamado uma vez por frame, tipicamente antes de engine_draw().
 * Se dt <= 0, usa o delta medido internamente; limitado a 100 ms para
 * evitar explosão de simulação física após janelas de pausa.
 * -------------------------------------------------------------------------*/

void engine_update(Engine *e, float dt)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    e->delta_time   = _ts_to_secs(now) - _ts_to_secs(s_time_prev);
    e->time_elapsed = _ts_to_secs(now) - _ts_to_secs(s_time_origin);
    s_time_prev = now;

    if (dt <= 0.0f) dt = static_cast<float>(e->delta_time);
    if (dt > 0.1f)  dt = 0.1f;   /* limita a 100 ms: protege a simulação de physics após pausa */

    /* Decai o shake linearmente ao longo de shake_duration; zera ao terminar */
    Camera &cam = e->camera;
    if (cam.shake_timer > 0.0f) {
        cam.shake_timer -= dt;
        const float amp = cam.shake_intensity *
                          (cam.shake_timer / cam.shake_duration);
        const float inv_500 = 1.0f / 500.0f;
        cam.shake_x = (static_cast<float>(rand() % 1000) * inv_500 - 1.0f) * amp;
        cam.shake_y = (static_cast<float>(rand() % 1000) * inv_500 - 1.0f) * amp;
        if (cam.shake_timer <= 0.0f) {
            cam.shake_x = 0.0f;
            cam.shake_y = 0.0f;
        }
    }

    /* Aproxima fade_alpha do fade_target a uma taxa de fade_speed por segundo */
    if (e->fade_alpha < e->fade_target)
        e->fade_alpha = _clampf(e->fade_alpha + e->fade_speed * dt, 0.0f, e->fade_target);
    else if (e->fade_alpha > e->fade_target)
        e->fade_alpha = _clampf(e->fade_alpha - e->fade_speed * dt, e->fade_target, 1.0f);

    engine_particles_update(e, dt);
    engine_animators_update(e, dt);
}

/* --- Renderização ---------------------------------------------------------- */

void engine_poll_events(Engine *e) { RENDERER(e)->poll_events(e); }
void engine_clear(Engine *e)       { RENDERER(e)->clear(e); }
void engine_flush(Engine *e)       { RENDERER(e)->flush(e); }
void engine_present(Engine *e)     { RENDERER(e)->present(e); }

/* _draw_object() — desenha um único objeto (fatorado para reutilização) */
static void _draw_object(Engine *e, IRenderer *r, int i)
{
    const GameObject &obj = e->objects[i];
    if (!obj.active) return;

    const float ox = static_cast<float>(obj.x);
    const float oy = static_cast<float>(obj.y);

    if (_sid_valid(e, obj.sprite_id)) {
        const SpriteData &sp = e->sprites[obj.sprite_id];
        if (!sp.loaded) return;

        float u0, v0, u1, v1, dw, dh;
        if (obj.use_tile) {
            const int px = obj.tile_x * obj.tile_w;
            const int py = obj.tile_y * obj.tile_h;
            _uv_for_tile(&sp, px, py, obj.tile_w, obj.tile_h, &u0,&v0,&u1,&v1);
            dw = static_cast<float>(obj.tile_w) * obj.scale_x;
            dh = static_cast<float>(obj.tile_h) * obj.scale_y;
        } else {
            u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
            dw = static_cast<float>(sp.width)  * obj.scale_x;
            dh = static_cast<float>(sp.height) * obj.scale_y;
        }
        r->set_texture(e, sp.texture);
        QuadParams q{ ox, oy, dw, dh, u0,v0,u1,v1,
                      1.0f,1.0f,1.0f, obj.alpha,
                      obj.rotation, obj.flip_h, obj.flip_v };
        r->push_quad(e, q);
    } else {
        float cr, cg, cb;
        _unpack_color(obj.color, &cr, &cg, &cb);
        const float dw = static_cast<float>(obj.width)  * obj.scale_x;
        const float dh = static_cast<float>(obj.height) * obj.scale_y;
        r->set_texture(e, e->white_tex);
        QuadParams q{ ox, oy, dw, dh, 0,0,1,1,
                      cr, cg, cb, obj.alpha, 0, 0, 0 };
        r->push_quad(e, q);
    }
}

void engine_draw(Engine *e)
{
    IRenderer *r = RENDERER(e);
    r->camera_push(e);

    /* Constrói índice de ordenação por (layer, z_order, índice original).
     * Usa insertion sort — rápido para N pequeno (ENGINE_MAX_OBJECTS ≤ 256)
     * e estável, preservando a ordem de criação como desempate. */
    int order[ENGINE_MAX_OBJECTS];
    int n = 0;
    for (int i = 0; i < e->object_count; ++i)
        if (e->objects[i].active) order[n++] = i;

    /* Insertion sort estável por (layer, z_order) */
    for (int i = 1; i < n; ++i) {
        const int key = order[i];
        const int kl  = e->objects[key].layer;
        const int kz  = e->objects[key].z_order;
        int j = i - 1;
        while (j >= 0 &&
               (e->objects[order[j]].layer > kl ||
               (e->objects[order[j]].layer == kl && e->objects[order[j]].z_order > kz)))
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    for (int i = 0; i < n; ++i)
        _draw_object(e, r, order[i]);

    r->flush(e);
    r->camera_pop(e);
}

/* engine_draw_layer() — desenha apenas objetos com layer == target_layer.
 * Útil para intercalar objetos entre camadas do mapa sem chamar engine_draw(). */
void engine_draw_layer(Engine *e, int target_layer)
{
    IRenderer *r = RENDERER(e);
    r->camera_push(e);

    int order[ENGINE_MAX_OBJECTS];
    int n = 0;
    for (int i = 0; i < e->object_count; ++i)
        if (e->objects[i].active && e->objects[i].layer == target_layer)
            order[n++] = i;

    for (int i = 1; i < n; ++i) {
        const int key = order[i];
        const int kz  = e->objects[key].z_order;
        int j = i - 1;
        while (j >= 0 && e->objects[order[j]].z_order > kz) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    for (int i = 0; i < n; ++i)
        _draw_object(e, r, order[i]);

    r->flush(e);
    r->camera_pop(e);
}

/* --- Primitivas de desenho 2D ---------------------------------------------- */

void engine_draw_rect(Engine *e, int x, int y, int w, int h, int r, int g, int b)
{
    IRenderer *re = RENDERER(e);
    re->set_texture(e, e->white_tex);
    QuadParams q{ static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h),
                  0,0,1,1, r*(1.0f/255.0f), g*(1.0f/255.0f), b*(1.0f/255.0f), 1.0f, 0,0,0 };
    re->push_quad(e, q);
}

void engine_draw_rect_outline(Engine *e, int x, int y, int w, int h,
                               int r, int g, int b, int t)
{
    engine_draw_rect(e, x,         y,             w, t,         r, g, b);   /* topo   */
    engine_draw_rect(e, x,         y + h - t,     w, t,         r, g, b);   /* base   */
    engine_draw_rect(e, x,         y,             t, h,         r, g, b);   /* esq    */
    engine_draw_rect(e, x + w - t, y,             t, h,         r, g, b);   /* dir    */
}

void engine_draw_line(Engine *e, int x0, int y0, int x1, int y1,
                      int r, int g, int b, int thickness)
{
    RENDERER(e)->draw_line_raw(e,
        static_cast<float>(x0), static_cast<float>(y0),
        static_cast<float>(x1), static_cast<float>(y1),
        r*(1.0f/255.0f), g*(1.0f/255.0f), b*(1.0f/255.0f),
        static_cast<float>(thickness));
}

void engine_draw_circle(Engine *e, int cx, int cy, int radius,
                         int r, int g, int b, int filled)
{
    RENDERER(e)->draw_circle_raw(e,
        static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(radius),
        r*(1.0f/255.0f), g*(1.0f/255.0f), b*(1.0f/255.0f), filled != 0);
}

void engine_draw_overlay(Engine *e, int x, int y, int w, int h,
                          int r, int g, int b, float alpha)
{
    IRenderer *re = RENDERER(e);
    re->set_texture(e, e->white_tex);
    QuadParams q{ static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h),
                  0,0,1,1, r*(1.0f/255.0f), g*(1.0f/255.0f), b*(1.0f/255.0f),
                  alpha, 0,0,0 };
    re->push_quad(e, q);
}

/* ---- Efeitos atmosféricos ------------------------------------------------- */

void engine_draw_rain(Engine *e, int screen_w, int screen_h, int /*frame*/,
                      const int *gotas_x, const int *gotas_y, int n_gotas,
                      int gota_w, int gota_h)
{
    static constexpr float BG_R = 0.0f,       BG_G = 30.0f/255.0f, BG_B = 80.0f/255.0f;
    static constexpr float DR_R = 180/255.0f, DR_G = 220/255.0f,    DR_B = 1.0f;
    static constexpr float SP_R = 90/255.0f,  SP_G = 150/255.0f,    SP_B = 220/255.0f;

    IRenderer *r = RENDERER(e);
    r->set_texture(e, e->white_tex);

    /* Overlay azulado */
    QuadParams bg{ 0,0, static_cast<float>(screen_w), static_cast<float>(screen_h),
                   0,0,1,1, BG_R,BG_G,BG_B,0.35f, 0,0,0 };
    r->push_quad(e, bg);

    for (int i = 0; i < n_gotas; ++i) {
        if (gotas_y[i] < 0 || gotas_y[i] >= screen_h) continue;
        const float fx = static_cast<float>(gotas_x[i]);
        const float fy = static_cast<float>(gotas_y[i]);

        QuadParams drop{ fx, fy,
                         static_cast<float>(gota_w), static_cast<float>(gota_h),
                         0,0,1,1, DR_R,DR_G,DR_B,0.85f, 0,0,0 };
        r->push_quad(e, drop);

        QuadParams splash{ fx, fy + static_cast<float>(gota_h),
                           static_cast<float>(gota_w), 2.0f,
                           0,0,1,1, SP_R,SP_G,SP_B,0.7f, 0,0,0 };
        r->push_quad(e, splash);
    }
    r->flush(e);
}

void engine_draw_night(Engine *e, int screen_w, int screen_h,
                       float intensidade, int /*offset*/)
{
    if (intensidade <= 0.0f) return;
    IRenderer *r = RENDERER(e);
    r->set_texture(e, e->white_tex);
    QuadParams q{ 0,0, static_cast<float>(screen_w), static_cast<float>(screen_h),
                  0,0,1,1, 5/255.0f, 5/255.0f, 20/255.0f, intensidade, 0,0,0 };
    r->push_quad(e, q);
    r->flush(e);
}

/* ---- Tilemap -------------------------------------------------------------- */

void engine_draw_tilemap(Engine *e, const int *tilemap,
                          int tile_rows, int tile_cols,
                          int sprite_id, int tile_w, int tile_h,
                          int offset_x, int offset_y)
{
    if (!_sid_valid(e, sprite_id)) return;
    const SpriteData &sp = e->sprites[sprite_id];
    if (!sp.loaded) return;

    const int  tiles_per_row = sp.width / tile_w;
    IRenderer *r             = RENDERER(e);
    r->set_texture(e, sp.texture);

    for (int row = 0; row < tile_rows; ++row) {
        for (int col = 0; col < tile_cols; ++col) {
            const int tile_id = tilemap[row * tile_cols + col];
            if (tile_id < 0) continue;
            const int src_x = (tile_id % tiles_per_row) * tile_w;
            const int src_y = (tile_id / tiles_per_row) * tile_h;
            float u0, v0, u1, v1;
            _uv_for_tile(&sp, src_x, src_y, tile_w, tile_h, &u0,&v0,&u1,&v1);
            QuadParams q{
                static_cast<float>(col * tile_w + offset_x),
                static_cast<float>(row * tile_h + offset_y),
                static_cast<float>(tile_w), static_cast<float>(tile_h),
                u0,v0,u1,v1, 1,1,1,1, 0,0,0 };
            r->push_quad(e, q);
        }
    }
    r->flush(e);
}

/* ---- Partes de sprite ----------------------------------------------------- */

/**
 * Função interna unificada para desenhar uma região de sprite com cor e alpha.
 * Todas as variantes públicas (sprite_part, sprite_part_ex, etc.) a utilizam.
 */
static void _draw_sprite_region(Engine *e, int sprite_id,
                                  int x, int y, int src_x, int src_y,
                                  int src_w, int src_h,
                                  float cr, float cg, float cb, float ca,
                                  float scale_x  = 1.0f, float scale_y  = 1.0f,
                                  float rotation = 0.0f,
                                  int flip_h     = 0,    int flip_v     = 0)
{
    if (!_sid_valid(e, sprite_id)) return;
    const SpriteData &sp = e->sprites[sprite_id];
    if (!sp.loaded) return;

    float u0, v0, u1, v1;
    _uv_for_tile(&sp, src_x, src_y, src_w, src_h, &u0,&v0,&u1,&v1);

    IRenderer *r = RENDERER(e);
    r->set_texture(e, sp.texture);
    QuadParams q{
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(src_w) * scale_x,
        static_cast<float>(src_h) * scale_y,
        u0,v0,u1,v1, cr,cg,cb,ca, rotation, flip_h, flip_v };
    r->push_quad(e, q);
}

void engine_draw_sprite_part(Engine *e, int sprite_id,
                              int x, int y, int src_x, int src_y,
                              int src_w, int src_h)
{
    _draw_sprite_region(e, sprite_id, x, y, src_x, src_y, src_w, src_h,
                         1.0f, 1.0f, 1.0f, 1.0f);
}

void engine_draw_sprite_part_ex(Engine *e, int sprite_id,
                                 int x, int y,
                                 int src_x, int src_y, int src_w, int src_h,
                                 float scale_x, float scale_y,
                                 float rotation, float alpha,
                                 int flip_h, int flip_v)
{
    _draw_sprite_region(e, sprite_id, x, y, src_x, src_y, src_w, src_h,
                         1.0f, 1.0f, 1.0f, alpha,
                         scale_x, scale_y, rotation, flip_h, flip_v);
}

void engine_draw_sprite_part_inverted(Engine *e, int sprite_id,
                                       int x, int y,
                                       int src_x, int src_y,
                                       int src_w, int src_h)
{
    if (!_sid_valid(e, sprite_id)) return;
    const SpriteData &sp = e->sprites[sprite_id];
    if (!sp.loaded) return;

    float u0, v0, u1, v1;
    _uv_for_tile(&sp, src_x, src_y, src_w, src_h, &u0,&v0,&u1,&v1);

    IRenderer *r = RENDERER(e);
    r->set_texture(e, sp.texture);
    QuadParams q{ static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(src_w), static_cast<float>(src_h),
                  u0,v0,u1,v1, 1,1,1,1, 0,0,0 };
    r->push_quad(e, q);
    r->flush(e);

    r->set_blend_inverted(e);
    r->push_quad(e, q);
    r->flush(e);
    r->set_blend_normal(e);
}

/* --- Texto e UI baseados em bitmap font ------------------------------------ *
 *
 * engine_draw_text() mapeia cada caractere da string para uma região do
 * sprite sheet da fonte, usando a fórmula:
 *   tile_x = (codepoint % chars_per_row) * font_w
 *   tile_y = (codepoint / chars_per_row) * font_h
 * -------------------------------------------------------------------------*/

void engine_draw_text(Engine *e, int x, int y, const char *text,
                       int font_sid, int font_w, int font_h,
                       int chars_per_row, int ascii_offset, int line_spacing)
{
    if (!_sid_valid(e, font_sid)) return;
    const SpriteData &sp = e->sprites[font_sid];
    if (!sp.loaded) return;

    IRenderer *r = RENDERER(e);
    r->set_texture(e, sp.texture);

    int cx = x, cy = y;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            cy += font_h + line_spacing;
            cx  = x;
            continue;
        }
        int cp = static_cast<int>(static_cast<unsigned char>(*p)) - ascii_offset;
        if (cp < 0) cp = 0;
        const int tile_x = (cp % chars_per_row) * font_w;
        const int tile_y = (cp / chars_per_row) * font_h;
        float u0, v0, u1, v1;
        _uv_for_tile(&sp, tile_x, tile_y, font_w, font_h, &u0,&v0,&u1,&v1);
        QuadParams q{ static_cast<float>(cx), static_cast<float>(cy),
                      static_cast<float>(font_w), static_cast<float>(font_h),
                      u0,v0,u1,v1, 1,1,1,1, 0,0,0 };
        r->push_quad(e, q);
        cx += font_w;
    }
    r->flush(e);
}

void engine_draw_box(Engine *e, int x, int y, int box_w, int box_h,
                      int box_sid, int tile_w, int tile_h)
{
    if (!_sid_valid(e, box_sid)) return;
    const SpriteData &sp = e->sprites[box_sid];
    if (!sp.loaded) return;

    if (box_w < tile_w * 2) box_w = tile_w * 2;
    if (box_h < tile_h * 2) box_h = tile_h * 2;

    const float sw = static_cast<float>(sp.width);
    const float sh = static_cast<float>(sp.height);
    IRenderer  *r  = RENDERER(e);
    r->set_texture(e, sp.texture);

    /* Enfileira um quad no batch mapeando uma região do sprite sheet para destino */
    auto box_quad = [&](int tx, int ty, int tw, int th,
                         int dx, int dy, int dw, int dh)
    {
        QuadParams q{
            static_cast<float>(dx), static_cast<float>(dy),
            static_cast<float>(dw), static_cast<float>(dh),
            static_cast<float>(tx)       / sw, static_cast<float>(ty)       / sh,
            static_cast<float>(tx + tw)  / sw, static_cast<float>(ty + th)  / sh,
            1,1,1,1, 0,0,0 };
        r->push_quad(e, q);
    };

    /* Preenche a área interior (sem bordas) do box com o tile central */
    for (int cy = y + tile_h; cy < y + box_h - tile_h; ) {
        const int copy_h = ((cy + tile_h) < (y + box_h - tile_h)) ? tile_h : (y + box_h - tile_h - cy);
        for (int cx = x + tile_w; cx < x + box_w - tile_w; ) {
            const int copy_w = ((cx + tile_w) < (x + box_w - tile_w)) ? tile_w : (x + box_w - tile_w - cx);
            box_quad(tile_w, tile_h, copy_w, copy_h,  cx, cy, copy_w, copy_h);
            cx += copy_w;
        }
        cy += copy_h;
    }

    /* Borda superior e inferior, repetindo o tile horizontal */
    for (int cx = x + tile_w; cx < x + box_w - tile_w; ) {
        const int copy_w = ((cx + tile_w) < (x + box_w - tile_w)) ? tile_w : (x + box_w - tile_w - cx);
        box_quad(tile_w, 0,          copy_w, tile_h, cx, y,               copy_w, tile_h);
        box_quad(tile_w, tile_h * 2, copy_w, tile_h, cx, y + box_h - tile_h, copy_w, tile_h);
        cx += copy_w;
    }

    /* Borda esquerda e direita, repetindo o tile vertical */
    for (int cy = y + tile_h; cy < y + box_h - tile_h; ) {
        const int copy_h = ((cy + tile_h) < (y + box_h - tile_h)) ? tile_h : (y + box_h - tile_h - cy);
        box_quad(0,           tile_h, tile_w, copy_h, x,                cy, tile_w, copy_h);
        box_quad(tile_w * 2,  tile_h, tile_w, copy_h, x + box_w - tile_w, cy, tile_w, copy_h);
        cy += copy_h;
    }

    /* Quatro cantos fixos, sem repetição */
    box_quad(0,          0,          tile_w, tile_h, x,                y,                tile_w, tile_h);
    box_quad(tile_w * 2, 0,          tile_w, tile_h, x + box_w - tile_w, y,                tile_w, tile_h);
    box_quad(0,          tile_h * 2, tile_w, tile_h, x,                y + box_h - tile_h, tile_w, tile_h);
    box_quad(tile_w * 2, tile_h * 2, tile_w, tile_h, x + box_w - tile_w, y + box_h - tile_h, tile_w, tile_h);

    r->flush(e);
}

void engine_draw_text_box(Engine *e, int x, int y, int box_w, int box_h,
                           const char *title, const char *content,
                           int box_sid, int box_tw, int box_th,
                           int font_sid, int font_w, int font_h,
                           int chars_per_row, int ascii_offset, int line_spacing)
{
    engine_draw_box(e, x, y, box_w, box_h, box_sid, box_tw, box_th);

    const int inner_x  = x + box_tw;
    int       inner_y  = y + box_th;
    const int max_chars = (box_w - box_tw * 2) / font_w;
    if (max_chars <= 0) return;

    if (title && title[0]) {
        engine_draw_text(e, inner_x, inner_y, title,
                         font_sid, font_w, font_h,
                         chars_per_row, ascii_offset, line_spacing);
        inner_y += font_h + line_spacing + 8;
    }

    /* Word-wrap: acumula palavras na linha atual; quebra ao exceder max_chars */
    int  line_y = inner_y;
    char current_line[1024];
    char word[256];
    current_line[0] = '\0';
    int  line_len   = 0;
    const char *p   = content;

    while (*p) {
        int word_len = 0;
        while (*p && *p != ' ' && *p != '\n')
            word[word_len++] = *p++;
        word[word_len] = '\0';

        if (word_len > 0) {
            const int new_len = (line_len == 0) ? word_len : line_len + 1 + word_len;
            if (new_len <= max_chars) {
                if (line_len > 0) current_line[line_len++] = ' ';
                memcpy(current_line + line_len, word, static_cast<size_t>(word_len));
                line_len += word_len;
                current_line[line_len] = '\0';
            } else {
                if (line_len > 0) {
                    engine_draw_text(e, inner_x, line_y, current_line,
                                     font_sid, font_w, font_h,
                                     chars_per_row, ascii_offset, line_spacing);
                    line_y += font_h + line_spacing;
                }
                memcpy(current_line, word, static_cast<size_t>(word_len));
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
                current_line[0] = '\0';
                line_len = 0;
            }
            ++p;
        } else if (*p == ' ') {
            ++p;
        }
    }
    if (line_len > 0)
        engine_draw_text(e, inner_x, line_y, current_line,
                         font_sid, font_w, font_h,
                         chars_per_row, ascii_offset, line_spacing);
}

/* --- Input ----------------------------------------------------------------- */

/* Versão interna de poll_events; mantida para chamadas entre subsistemas */
void engine_poll_events_impl(Engine *e) { RENDERER(e)->poll_events(e); }

#ifdef ENGINE_BACKEND_GL

static inline RendererGL *_rgl(Engine *e)
{
    return static_cast<RendererGL *>(e->renderer_impl);
}

int engine_key_down(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && r->keys_cur[kc]) return 1;
    return 0;
}

int engine_key_pressed(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && r->keys_cur[kc] && !r->keys_prev[kc]) return 1;
    return 0;
}

int engine_key_released(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && !r->keys_cur[kc] && r->keys_prev[kc]) return 1;
    return 0;
}

#else
/* Stubs vazios: input de teclado para backends não-X11 é tratado dentro do renderer */
int engine_key_down    (Engine *, const char *) { return 0; }
int engine_key_pressed (Engine *, const char *) { return 0; }
int engine_key_released(Engine *, const char *) { return 0; }
#endif

int  engine_mouse_down    (Engine *e, int b) { return (b < 0 || b > 2) ? 0 :  e->mouse.buttons[b]; }
int  engine_mouse_pressed (Engine *e, int b) { return (b < 0 || b > 2) ? 0 :  e->mouse.buttons[b] && !e->mouse.buttons_prev[b]; }
int  engine_mouse_released(Engine *e, int b) { return (b < 0 || b > 2) ? 0 : !e->mouse.buttons[b] &&  e->mouse.buttons_prev[b]; }
void engine_mouse_pos     (Engine *e, int *x, int *y) { *x = e->mouse.x; *y = e->mouse.y; }
int  engine_mouse_scroll  (Engine *e) { return e->mouse.scroll; }

/* --- Fullscreen e limitador de FPS ----------------------------------------- */

void engine_toggle_fullscreen(Engine *e) { RENDERER(e)->toggle_fullscreen(e); }

void engine_cap_fps(Engine *, int fps_target)
{
    static struct timespec last = {0, 0};

    if (fps_target <= 0) return;

    const long frame_ns = 1000000000L / fps_target;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Primeira chamada do frame: inicializa o timestamp de referência */
    if (last.tv_sec == 0 && last.tv_nsec == 0) { last = now; return; }

    const long elapsed_ns = (now.tv_sec  - last.tv_sec)  * 1000000000L
                          + (now.tv_nsec - last.tv_nsec);
    const long sleep_ns   = frame_ns - elapsed_ns;

    if (sleep_ns > 0) {
        struct timespec ts{ sleep_ns / 1000000000L, sleep_ns % 1000000000L };
        nanosleep(&ts, nullptr);
    }
    clock_gettime(CLOCK_MONOTONIC, &last);
}

/* --- Áudio — wrappers sobre miniaudio -------------------------------------- *
 *
 * _ma() / _ms() convertem os buffers opacos em ponteiros tipados internamente.
 * Todas as funções públicas de áudio adquirem o mutex antes de operar sobre
 * o pool de trilhas, permitindo uso seguro a partir de threads auxiliares.
 * -------------------------------------------------------------------------*/

static inline ma_engine *_ma (Engine *e)      { return reinterpret_cast<ma_engine *>(e->audio._ma_engine); }
static inline ma_sound  *_ms (AudioTrack *t)  { return reinterpret_cast<ma_sound  *>(&t->sound); }

static inline float _audio_clamp_volume(float v) { return v < 0.0f ? 0.0f : (v > 1.0f  ? 1.0f  : v); }
static inline float _audio_clamp_pitch (float p) { return p < 0.1f ? 0.1f : (p > 4.0f  ? 4.0f  : p); }

static inline bool _audio_handle_valid(const AudioContext *ac, AudioHandle h)
{
    return h >= 0 && h < ENGINE_AUDIO_MAX_TRACKS && ac->tracks[h].in_use;
}

static int _audio_free_slot(const AudioContext *ac)
{
    for (int i = 0; i < ENGINE_AUDIO_MAX_TRACKS; ++i)
        if (!ac->tracks[i].in_use) return i;
    return ENGINE_AUDIO_INVALID;
}

int engine_audio_init(Engine *e)
{
    memset(&e->audio, 0, sizeof(AudioContext));
    if (ma_engine_init(nullptr, _ma(e)) != MA_SUCCESS) return 0;
    pthread_mutex_init(&e->audio.mutex, nullptr);
    e->audio.ready = 1;
    return 1;
}

void engine_audio_update(Engine *e)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready) return;
    pthread_mutex_lock(&ac->mutex);
    for (int i = 0; i < ENGINE_AUDIO_MAX_TRACKS; ++i) {
        AudioTrack *t = &ac->tracks[i];
        if (!t->in_use || t->status != AUDIO_TRACK_PLAYING || t->loop) continue;
        if (ma_sound_at_end(_ms(t))) {
            t->status = AUDIO_TRACK_FINISHED;
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
    AudioContext *ac = &e->audio;
    if (!ac->ready) return;
    pthread_mutex_lock(&ac->mutex);
    for (int i = 0; i < ENGINE_AUDIO_MAX_TRACKS; ++i)
        if (ac->tracks[i].in_use) { ma_sound_uninit(_ms(&ac->tracks[i])); ac->tracks[i].in_use = 0; }
    pthread_mutex_unlock(&ac->mutex);
    ma_engine_uninit(_ma(e));
    pthread_mutex_destroy(&ac->mutex);
    ac->ready = 0;
}

AudioHandle engine_audio_play(Engine *e, const char *file, int loop,
                               float volume, float pitch, AudioHandle resume_after)
{
    AudioContext *ac = &e->audio;
    if (!ac->ready || !file) return ENGINE_AUDIO_INVALID;

    pthread_mutex_lock(&ac->mutex);
    const int slot = _audio_free_slot(ac);
    if (slot == ENGINE_AUDIO_INVALID) {
        pthread_mutex_unlock(&ac->mutex);
        return ENGINE_AUDIO_INVALID;
    }

    AudioTrack *t = &ac->tracks[slot];
    memset(t, 0, sizeof(AudioTrack));

    if (ma_sound_init_from_file(_ma(e), file, MA_SOUND_FLAG_STREAM,
                                nullptr, nullptr, _ms(t)) != MA_SUCCESS) {
        pthread_mutex_unlock(&ac->mutex);
        return ENGINE_AUDIO_INVALID;
    }

    t->volume       = _audio_clamp_volume(volume);
    t->pitch        = _audio_clamp_pitch(pitch);
    t->loop         = loop ? 1 : 0;
    t->resume_after = resume_after;
    t->in_use       = 1;
    t->status       = AUDIO_TRACK_PLAYING;

    ma_sound_set_volume (_ms(t), t->volume);
    ma_sound_set_pitch  (_ms(t), t->pitch);
    ma_sound_set_looping(_ms(t), static_cast<ma_bool32>(t->loop));
    ma_sound_start      (_ms(t));

    pthread_mutex_unlock(&ac->mutex);
    return static_cast<AudioHandle>(slot);
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
    if (!ac->ready || !_audio_handle_valid(ac, h)) return 1;
    pthread_mutex_lock(&ac->mutex);
    const int done = (ac->tracks[h].status == AUDIO_TRACK_FINISHED) ? 1 : 0;
    pthread_mutex_unlock(&ac->mutex);
    return done;
}

/* --- FBOs ------------------------------------------------------------------ */

FboHandle engine_fbo_create(Engine *e, int w, int h)
{
    return RENDERER(e)->fbo_create(e, w, h);
}
void engine_fbo_destroy(Engine *e, FboHandle fh)
{
    RENDERER(e)->fbo_destroy(e, fh);
}
void engine_fbo_bind(Engine *e, FboHandle fh)
{
    RENDERER(e)->fbo_bind(e, fh);
}
void engine_fbo_unbind(Engine *e)
{
    RENDERER(e)->fbo_unbind(e);
}
unsigned int engine_fbo_texture(Engine *e, FboHandle fh)
{
    return RENDERER(e)->fbo_texture(e, fh);
}

/* --- Shaders --------------------------------------------------------------- */

ShaderHandle engine_shader_create(Engine *e, const char *vert_src, const char *frag_src)
{
    return RENDERER(e)->shader_create(e, vert_src, frag_src);
}
void engine_shader_destroy(Engine *e, ShaderHandle sh)
{
    RENDERER(e)->shader_destroy(e, sh);
}
void engine_shader_use(Engine *e, ShaderHandle sh)
{
    RENDERER(e)->shader_use(e, sh);
}
void engine_shader_none(Engine *e)
{
    RENDERER(e)->shader_none(e);
}
void engine_shader_set_int(Engine *e, ShaderHandle sh, const char *name, int v)
{
    RENDERER(e)->shader_set_int(e, sh, name, v);
}
void engine_shader_set_float(Engine *e, ShaderHandle sh, const char *name, float v)
{
    RENDERER(e)->shader_set_float(e, sh, name, v);
}
void engine_shader_set_vec2(Engine *e, ShaderHandle sh, const char *name, float x, float y)
{
    RENDERER(e)->shader_set_vec2(e, sh, name, x, y);
}
void engine_shader_set_vec4(Engine *e, ShaderHandle sh, const char *name, float x, float y, float z, float w)
{
    RENDERER(e)->shader_set_vec4(e, sh, name, x, y, z, w);
}
}