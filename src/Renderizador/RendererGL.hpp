// RendererGL.hpp
// Backend de renderização OpenGL 2.1 + X11.
// Inclua este header apenas em Engine_renderer.cpp (via #include condicional)
// ou em unidades que precisem instanciar RendererGL diretamente.

#pragma once

#ifdef ENGINE_BACKEND_GL

#include "../Engine.hpp"

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

/* =============================================================================
 * Defines de extensões OpenGL não presentes em GL/gl.h antigos
 * ============================================================================= */
#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER  0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#  define GL_DYNAMIC_DRAW  0x88E8
#endif
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

/* =============================================================================
 * Typedefs de ponteiros de função — VBO (GL 1.5)
 * ============================================================================= */
typedef void (*PFNGLGENBUFFERSPROC)   (GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)   (GLenum, GLuint);
typedef void (*PFNGLBUFFERDATAPROC)   (GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);

/* =============================================================================
 * Typedefs de ponteiros de função — GLSL shaders (GL 2.0)
 * ============================================================================= */
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

/* =============================================================================
 * Typedefs de ponteiros de função — FBO (GL_EXT_framebuffer_object / GL 3.0)
 * ============================================================================= */
typedef void   (*PFNGLGENFRAMEBUFFERSPROC)         (GLsizei, GLuint *);
typedef void   (*PFNGLBINDFRAMEBUFFERPROC)         (GLenum,  GLuint);
typedef void   (*PFNGLFRAMEBUFFERTEXTURE2DPROC)    (GLenum,  GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)  (GLenum);
typedef void   (*PFNGLDELETEFRAMEBUFFERSPROC)      (GLsizei, const GLuint *);

/* =============================================================================
 * BatchState — buffer de vértices para envio em lote de quads
 * ============================================================================= */
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

/* =============================================================================
 * RendererGL — backend OpenGL 2.1 + X11
 *
 * Estratégia de renderização:
 *   • Projeção ortográfica via glOrtho(); fixed-function pipeline.
 *   • Batch de até 4096 quads em um VBO dinâmico (GL_DYNAMIC_DRAW).
 *   • Um flush implícito ocorre quando a textura ativa muda ou o batch enche.
 *   • Rotação usa glPushMatrix/glRotatef fora do batch (draw imediato).
 *   • Input via XNextEvent(); keycode → KeySym resolvido pela KeysymCache.
 * ============================================================================= */
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

    /* Double buffer de teclado: ponteiros trocados a cada frame em O(1) */
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
    void resize         (Engine *e, int new_w, int new_h)   override;
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

#endif /* ENGINE_BACKEND_GL */