/*
 * IRenderer.hpp — Interface abstrata de backend gráfico.
 *
 * Todo backend (OpenGL, Direct3D 11, Vulkan) implementa esta classe pura.
 * A Engine mantém um ponteiro void* (renderer_impl) que é convertido para
 * IRenderer* internamente via static_cast; o código do jogo e o FFI Lua
 * nunca interagem com esta interface diretamente.
 *
 * Fluxo de um frame típico:
 *   1. poll_events()  — processa eventos de janela e input da plataforma.
 *   2. clear()        — limpa o framebuffer com a cor definida em set_clear_color().
 *   3. camera_push()  — aplica a transformação da câmera ao estado de renderização.
 *      [push_quad() chamadas de objetos do mundo]
 *   4. flush()        — envia o batch pendente de quads à GPU.
 *   5. camera_pop()   — remove a transformação da câmera.
 *      [push_quad() de UI/HUD sem câmera]
 *   6. flush()
 *   7. present()      — swap buffers / present da swapchain.
 *
 * QuadParams descreve um quad 2D completo; o backend converte para o
 * formato de vértice específico da sua API.
 */

#pragma once
#ifndef IRENDERER_HPP
#define IRENDERER_HPP
#ifdef __cplusplus

#include <cstdint>

struct Engine;

/* Parâmetros de um quad do batch 2D */
struct QuadParams {
    float dx, dy;       /* posição destino: topo-esquerdo em pixels virtuais   */
    float dw, dh;       /* largura e altura destino                            */
    float u0, v0;       /* UV topo-esquerdo  (0..1)                            */
    float u1, v1;       /* UV baixo-direito  (0..1)                            */
    float r, g, b, a;   /* cor/tint e alpha  (0..1)                            */
    float rotation;     /* graus; pivô no centro do quad; 0 = sem rotação      */
    int   flip_h;       /* espelhar horizontalmente (troca u0 ↔ u1)            */
    int   flip_v;       /* espelhar verticalmente   (troca v0 ↔ v1)            */
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    /* --- Ciclo de vida ---------------------------------------------------- */
    virtual bool init   (Engine *e, int win_w, int win_h,
                         const char *title, int scale) = 0;
    virtual void destroy(Engine *e)                    = 0;

    /* --- Controle de frame ------------------------------------------------ */
    virtual void clear  (Engine *e) = 0;   /* limpa com a cor definida em set_clear_color */
    virtual void flush  (Engine *e) = 0;   /* submete o batch pendente à GPU              */
    virtual void present(Engine *e) = 0;   /* swap buffers / presenta o frame             */
    virtual void set_vsync(bool enable) = 0;

    /* --- Texturas --------------------------------------------------------- */
    virtual unsigned int upload_texture(const unsigned char *rgba,
                                        unsigned int w,
                                        unsigned int h) = 0;
    virtual void delete_texture(unsigned int handle) = 0;

    /* --- Batch de quads --------------------------------------------------- */
    /*
     * set_texture(): define a textura dos próximos push_quad().
     * Pode disparar um flush interno se a textura diferir da atual.
     */
    virtual void set_texture(Engine *e, unsigned int tex_handle) = 0;

    /*
     * push_quad(): acumula um quad no batch.
     * O backend emite um draw call quando o batch enche ou set_texture muda.
     */
    virtual void push_quad(Engine *e, const QuadParams &q) = 0;

    /* --- Janela e projeção ------------------------------------------------ */
    virtual void resize          (Engine *e, int new_w, int new_h) = 0;
    virtual void toggle_fullscreen(Engine *e)                      = 0;
    virtual void setup_projection (Engine *e)                      = 0;

    /* --- Câmera ----------------------------------------------------------- */
    virtual void camera_push(Engine *e) = 0;  /* aplica transformação da câmera  */
    virtual void camera_pop (Engine *e) = 0;  /* restaura estado anterior        */

    /* --- Primitivas de rasterização direta -------------------------------- */
    /*
     * Disparam um flush interno antes de emitir o draw call.
     * Usadas por engine_draw_line() e engine_draw_circle().
     */
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
    virtual void set_blend_inverted(Engine *e) = 0;  /* efeito de negativo/XOR  */
    virtual void set_blend_normal  (Engine *e) = 0;  /* alpha padrão            */

    /* --- Cor de fundo ----------------------------------------------------- */
    /*
     * set_clear_color(): armazena a cor internamente.
     * Chamado por engine_set_background(); aplicada em clear() no próximo frame.
     */
    virtual void set_clear_color(float r, float g, float b) = 0;

    /* --- Input e janela --------------------------------------------------- */
    virtual void poll_events(Engine *e) = 0;
};

/* ===========================================================================
 * engine_create_renderer() — factory de backends
 *
 * Instancia o IRenderer correto para o backend_id informado.
 * Retorna nullptr se o backend não foi compilado neste binário.
 * Implementada em Engine.cpp junto com a lógica da factory.
 * =========================================================================== */
IRenderer *engine_create_renderer(int backend_id);

#endif /* __cplusplus */
#endif /* IRENDERER_HPP */