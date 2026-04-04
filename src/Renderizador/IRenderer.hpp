//IRenderer.hpp
/*
 * Todo backend (OpenGL, Direct3D 11) implementa esta classe pura.
 * A Engine mantém um ponteiro para IRenderer e nunca conhece o backend
 * concreto — o código do jogo e o FFI Lua também não interagem com ela.
 *
 * Fluxo de um frame típico:
 *   poll_events()          — processa input e eventos da janela
 *   clear()                — limpa o framebuffer
 *   camera_push()          — aplica transformação da câmera
 *     [push_quad() do mundo]
 *   flush()                — envia o batch à GPU
 *   camera_pop()           — remove a transformação
 *     [push_quad() de UI/HUD]
 *   flush()
 *   present()              — swap buffers / exibe o frame
 */

#pragma once
#ifndef IRENDERER_HPP
#define IRENDERER_HPP
#ifdef __cplusplus

#include <cstdint>

/* Engine é definido em Engine.hpp (incluído antes deste header) */

/* FboHandle, ShaderHandle, ENGINE_FBO_INVALID e ENGINE_SHADER_INVALID
 * são definidos em Engine.hpp, que deve ser incluído antes deste header. */

/* Descreve um quad 2D para o batch de renderização. */
struct QuadParams {
    float dx, dy;       /* topo-esquerdo destino em pixels virtuais          */
    float dw, dh;       /* largura e altura destino                          */
    float u0, v0;       /* UV topo-esquerdo  (0..1)                          */
    float u1, v1;       /* UV baixo-direito  (0..1)                          */
    float r, g, b, a;   /* cor/tint e alpha  (0..1)                          */
    float rotation;     /* graus; pivô no centro; 0 = sem rotação            */
    int   flip_h;       /* espelhar horizontalmente (troca u0 ↔ u1)          */
    int   flip_v;       /* espelhar verticalmente   (troca v0 ↔ v1)          */
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    /* --- Ciclo de vida ---------------------------------------------------- */
    virtual bool init   (Engine *e, int win_w, int win_h,
                         const char *title, int scale) = 0;
    virtual void destroy(Engine *e)                    = 0;

    /* --- Controle de frame ------------------------------------------------ */
    virtual void clear  (Engine *e) = 0;   /* limpa com set_clear_color()    */
    virtual void flush  (Engine *e) = 0;   /* submete o batch à GPU          */
    virtual void present(Engine *e) = 0;   /* swap buffers                   */
    virtual void set_vsync(bool enable) = 0;

    /* --- Texturas --------------------------------------------------------- */
    virtual unsigned int upload_texture(const unsigned char *rgba,
                                        unsigned int w,
                                        unsigned int h) = 0;
    virtual void delete_texture(unsigned int handle) = 0;

    /* --- Batch de quads --------------------------------------------------- */
    /*
     * set_texture() — define a textura dos próximos push_quad().
     *   Pode disparar um flush interno se a textura mudar.
     *
     * push_quad() — acumula um quad no batch.
     *   O backend emite um draw call quando o batch enche ou a textura muda.
     */
    virtual void set_texture(Engine *e, unsigned int tex_handle) = 0;
    virtual void push_quad  (Engine *e, const QuadParams &q)     = 0;

    /* --- Janela e projeção ------------------------------------------------ */
    virtual void resize          (Engine *e, int new_w, int new_h) = 0;
    virtual void toggle_fullscreen(Engine *e)                      = 0;
    virtual void setup_projection (Engine *e)                      = 0;

    /* --- Câmera ----------------------------------------------------------- */
    virtual void camera_push(Engine *e) = 0;  /* aplica transformação        */
    virtual void camera_pop (Engine *e) = 0;  /* restaura estado anterior    */

    /* --- Primitivas diretas ----------------------------------------------- */
    /* Disparam um flush interno antes de emitir o draw.
     * Usadas por engine_draw_line() e engine_draw_circle(). */
    virtual void draw_line_raw  (Engine *e,
                                 float x0, float y0,
                                 float x1, float y1,
                                 float r, float g, float b,
                                 float thickness) = 0;

    virtual void draw_circle_raw(Engine *e,
                                 float cx, float cy,
                                 float radius,
                                 float r, float g, float b,
                                 bool filled) = 0;

    /* --- Modos de blend --------------------------------------------------- */
    virtual void set_blend_inverted(Engine *e) = 0;  /* efeito negativo/XOR  */
    virtual void set_blend_normal  (Engine *e) = 0;  /* alpha padrão         */

    /* --- Cor de fundo ----------------------------------------------------- */
    /* Armazenada internamente; aplicada em clear() no próximo frame. */
    virtual void set_clear_color(float r, float g, float b) = 0;

    /* --- Input e janela --------------------------------------------------- */
    virtual void poll_events(Engine *e) = 0;

    /* --- FBO (Framebuffer Object / Render Target) ------------------------- */
    /*
     * fbo_create()  — cria FBO com textura de cor RGBA w×h.
     *                 Retorna ENGINE_FBO_INVALID em falha.
     * fbo_destroy() — libera todos os recursos.
     * fbo_bind()    — redireciona rendering para este FBO; ajusta viewport.
     * fbo_unbind()  — restaura a tela e a projeção.
     * fbo_texture() — retorna a textura de cor do FBO para uso em push_quad().
     */
    virtual FboHandle    fbo_create (Engine *e, int w, int h) = 0;
    virtual void         fbo_destroy(Engine *e, FboHandle fh) = 0;
    virtual void         fbo_bind   (Engine *e, FboHandle fh) = 0;
    virtual void         fbo_unbind (Engine *e)               = 0;
    virtual unsigned int fbo_texture(Engine *e, FboHandle fh) = 0;

    /* --- Shaders ---------------------------------------------------------- */
    /*
     * shader_create()   — compila vert_src + frag_src (GLSL ou HLSL).
     *                     Retorna ENGINE_SHADER_INVALID em falha (erros no stderr).
     * shader_destroy()  — libera o programa.
     * shader_use()      — ativa para os próximos draw calls.
     * shader_none()     — restaura o pipeline padrão.
     * shader_set_*      — envia uniforms/constants ao shader ativo.
     */
    virtual ShaderHandle shader_create (Engine *e,
                                        const char *vert_src,
                                        const char *frag_src)       = 0;
    virtual void         shader_destroy(Engine *e, ShaderHandle sh) = 0;
    virtual void         shader_use    (Engine *e, ShaderHandle sh) = 0;
    virtual void         shader_none   (Engine *e)                  = 0;
    virtual void         shader_set_int  (Engine *e, ShaderHandle sh,
                                          const char *name, int   v) = 0;
    virtual void         shader_set_float(Engine *e, ShaderHandle sh,
                                          const char *name, float v) = 0;
    virtual void         shader_set_vec2 (Engine *e, ShaderHandle sh,
                                          const char *name,
                                          float x, float y)          = 0;
    virtual void         shader_set_vec4 (Engine *e, ShaderHandle sh,
                                          const char *name,
                                          float x, float y,
                                          float z, float w)          = 0;
};

/*
 * engine_create_renderer() — factory de backends.
 * Instancia o IRenderer correto para o backend_id informado.
 * Retorna nullptr se o backend não foi compilado neste binário.
 */
IRenderer *engine_create_renderer(int backend_id);

#endif /* __cplusplus */
#endif /* IRENDERER_HPP */