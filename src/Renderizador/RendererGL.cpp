// RendererGL.cpp
// Implementação do backend de renderização OpenGL 2.1 + X11.
// Responsável pela criação da janela X11/GLX, inicialização do contexto OpenGL,
// gerenciamento de texturas, envio de lotes de quads e controle de eventos.

#ifdef ENGINE_BACKEND_GL

#include "RendererGL.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* =============================================================================
 * Tabela de mapeamento nome → KeySym para teclas especiais.
 * Busca linear; pequena o suficiente para ser mais rápida que qualquer hash.
 * ============================================================================= */
struct KeyEntry {
    const char *name;
    KeySym      sym;
};

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
KeySym _name_to_keysym(const char *key)
{
    if (!key || !key[0]) return 0;
    if (!key[1]) return static_cast<KeySym>(static_cast<unsigned char>(key[0]));
    for (int i = 0; i < k_special_keys_count; ++i)
        if (strcmp(key, k_special_keys[i].name) == 0)
            return k_special_keys[i].sym;
    return 0;
}

/* =============================================================================
 * RendererGL — utilitário de scale
 * ============================================================================= */
int RendererGL::_best_scale(int win_w, int win_h, int render_w, int render_h)
{
    int sx = win_w / render_w;
    int sy = win_h / render_h;
    int s  = sx < sy ? sx : sy;
    return s > 0 ? s : 1;
}

/* =============================================================================
 * RendererGL::init
 * ============================================================================= */
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

/* =============================================================================
 * RendererGL::destroy
 * ============================================================================= */
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

/* =============================================================================
 * Texturas
 * ============================================================================= */
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

/* =============================================================================
 * Frame
 * ============================================================================= */
void RendererGL::clear(Engine *)   { glClear(GL_COLOR_BUFFER_BIT); }
void RendererGL::flush(Engine *)   { _batch_flush_internal(); }
void RendererGL::present(Engine *) { glXSwapBuffers(display, window); }

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

/* =============================================================================
 * Batch
 * ============================================================================= */
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

/* =============================================================================
 * Projeção / resize
 * ============================================================================= */
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

/* =============================================================================
 * Câmera
 * ============================================================================= */
void RendererGL::camera_push(Engine *e)
{
    if (!e->camera_enabled) return;
    _batch_flush_internal();
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

/* =============================================================================
 * Primitivas raw
 * ============================================================================= */
void RendererGL::draw_line_raw(Engine *e,
                                float x0, float y0, float x1, float y1,
                                float r, float g, float b, float thickness)
{
    const float dx  = x1 - x0;
    const float dy  = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    const float angle  = -(float)(atan2((double)dy, (double)dx) * (180.0 / M_PI));
    const float half_t = thickness * 0.5f;

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

/* =============================================================================
 * Blend modes
 * ============================================================================= */
void RendererGL::set_blend_inverted(Engine *)
{
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
}
void RendererGL::set_blend_normal(Engine *)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void RendererGL::set_clear_color(float r, float g, float b)
{
    glClearColor(r, g, b, 1.0f);
}

/* =============================================================================
 * Fullscreen
 * ============================================================================= */
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

/* =============================================================================
 * Poll events
 * ============================================================================= */
void RendererGL::poll_events(Engine *e)
{
    /* Swap O(1) dos buffers de tecla */
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
            const int s  = _best_scale(e->win_w, e->win_h, e->render_w, e->render_h);
            const int ox = (e->win_w - e->render_w * s) / 2;
            const int oy = (e->win_h - e->render_h * s) / 2;
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

/* =============================================================================
 * FBO
 * ============================================================================= */
FboHandle RendererGL::fbo_create(Engine *e, int w, int h)
{
    if (!fbo_supported) {
        fprintf(stderr, "Engine: FBO não suportado neste driver.\n");
        return ENGINE_FBO_INVALID;
    }
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
    setup_projection(e); /* restaura viewport e projeção da janela */
}

unsigned int RendererGL::fbo_texture(Engine *e, FboHandle fh)
{
    if (fh < 0 || fh >= ENGINE_MAX_FBOS || !e->fbos[fh].in_use) return 0;
    return e->fbos[fh].color_tex;
}

/* =============================================================================
 * Shaders
 * ============================================================================= */
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

void RendererGL::shader_set_vec4(Engine *e, ShaderHandle sh, const char *name,
                                  float x, float y, float z, float w)
{
    if (!shaders_supported || sh < 0 || sh >= ENGINE_MAX_SHADERS || !e->shaders[sh].in_use) return;
    GLuint prog = static_cast<GLuint>(e->shaders[sh].program);
    GLint  loc  = glGetUniformLocation_f(prog, name);
    if (loc >= 0) glUniform4f_f(loc, x, y, z, w);
}

/* =============================================================================
 * Carregamento de procs via glXGetProcAddressARB
 * ============================================================================= */
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
    /* Tenta nomes EXT primeiro, depois sem sufixo (OpenGL 3.0 core) */
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

    fbo_supported = (glGenFramebuffers_f      && glBindFramebuffer_f &&
                     glFramebufferTexture2D_f  && glCheckFramebufferStatus_f &&
                     glDeleteFramebuffers_f);
    if (!fbo_supported)
        fprintf(stderr, "Engine: FBO não disponível (requer GL_EXT_framebuffer_object ou GL 3.0+).\n");
    return fbo_supported;
}

/* =============================================================================
 * Batch — internos
 * ============================================================================= */
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

    /* Quad: 4 vértices — topo-esquerdo, topo-direito, baixo-direito, baixo-esquerdo */
    *p++ = dx;      *p++ = dy;      *p++ = u0; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    *p++ = dx + dw; *p++ = dy;      *p++ = u1; *p++ = v0; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    *p++ = dx + dw; *p++ = dy + dh; *p++ = u1; *p++ = v1; *p++ = r; *p++ = g; *p++ = b; *p++ = a;
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
    glTranslatef(dx + dw * 0.5f, dy + dh * 0.5f, 0.0f);
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

/* =============================================================================
 * KeySym cache
 * ============================================================================= */
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