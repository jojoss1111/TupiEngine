//RendererDX11.hpp

#pragma once
#ifndef RENDERER_DX11_HPP
#define RENDERER_DX11_HPP

#ifdef ENGINE_BACKEND_DX11

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include "IRenderer.hpp"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>

/* --- Limites do batch ------------------------------------------------------- */
static constexpr uint32_t DX11_BATCH_MAX_QUADS    = 4096;
static constexpr uint32_t DX11_VERTS_PER_QUAD     = 4;
static constexpr uint32_t DX11_INDICES_PER_QUAD   = 6;
static constexpr uint32_t DX11_MAX_VERT           = DX11_BATCH_MAX_QUADS * DX11_VERTS_PER_QUAD;
static constexpr uint32_t DX11_MAX_IDX            = DX11_BATCH_MAX_QUADS * DX11_INDICES_PER_QUAD;
static constexpr uint32_t DX11_MAX_TEXTURES       = 128;

/* --- Pool de FBOs e Shaders ----------------------------------------------- */
static constexpr uint32_t DX11_MAX_FBOS    = 8;
static constexpr uint32_t DX11_MAX_SHADERS = 16;

/*
 * DxFbo — Render Target Object (equivalente ao FBO do OpenGL).
 *
 * Cada FBO possui:
 *   tex  — ID3D11Texture2D com D3D11_BIND_RENDER_TARGET | SHADER_RESOURCE
 *   rtv  — Render Target View para bind como alvo de renderização
 *   srv  — Shader Resource View para uso como textura em shaders
 *   handle — handle público (índice base-1 no pool de texturas compartilhado)
 */
struct DxFbo {
    ID3D11Texture2D*          tex    = nullptr;
    ID3D11RenderTargetView*   rtv    = nullptr;
    ID3D11ShaderResourceView* srv    = nullptr;
    unsigned int              handle = 0;  /* handle no pool de texturas compartilhado */
    int                       width  = 0;
    int                       height = 0;
    bool                      in_use = false;
};

/*
 * DxShaderProgram — par de pixel shaders customizados para efeitos em tela.
 *
 * No DX11, o vertex shader é compartilhado (o mesmo k_hlsl_vs).
 * Apenas o pixel shader é trocado para efeitos como blur, grayscale, CRT, etc.
 * O constant buffer de efeito (cb_effect_) é compartilhado e atualizado por
 * shader_set_float/vec2/vec4 antes de cada draw call de efeito.
 *
 * Para receber os uniforms equivalentes do GLSL, o HLSL usa:
 *   cbuffer EffectCB : register(b1) { float4 u_params[4]; }
 * Cada shader_set_vec4() escreve em u_params[slot] identificado pelo nome.
 */
struct DxShaderProgram {
    ID3D11PixelShader* ps     = nullptr;
    bool               in_use = false;
    /* Mapa de nome de uniform → slot em u_params[] (máx 4 vec4 = 16 floats) */
    struct UniformSlot { char name[32]; int slot; };
    UniformSlot uniforms[8]   = {};
    int         uniform_count = 0;
};

/* Constant buffer compartilhado para efeitos de shader — 4×vec4 = 64 bytes */
struct alignas(16) DxEffectCB {
    float params[16];  /* u_params[0..3] acessíveis no HLSL */
};

/*
 * DxVertex — formato de vértice do batch.
 *
 * Posição em pixels virtuais (transformada para NDC no vertex shader via
 * DxOrthoVS).  UVs normalizados.  Cor como float RGBA para tint por vértice.
 */
struct DxVertex {
    float x, y;         /* posição 2D em pixels virtuais                      */
    float u, v;         /* UV normalizado 0..1                                 */
    float r, g, b, a;   /* cor/tint                                            */
};

/*
 * DxTexture — slot de textura no pool interno.
 *
 * Cada textura carregada ocupa um slot; tex/srv são liberados em delete_texture().
 * in_use == false significa que o slot está disponível para reutilização.
 */
struct DxTexture {
    ID3D11Texture2D*          tex  = nullptr;
    ID3D11ShaderResourceView* srv  = nullptr;
    bool                      in_use = false;
};

/*
 * DxOrthoVS — constant buffer enviado ao vertex shader a cada resize.
 *
 * inv_w/inv_h escalam pixels virtuais para NDC: x_ndc = x * inv_w - 1.
 * cam_tx/cam_ty/cam_zoom são aplicados no vertex shader para evitar
 * recalcular a transformação de câmera na CPU a cada vértice.
 * Alinhado a 16 bytes conforme requisito de cbuffer HLSL.
 */
struct alignas(16) DxOrthoVS {
    float inv_w, inv_h;   /* 2 / render_w  e  2 / render_h                    */
    float cam_tx, cam_ty; /* offset da câmera em pixels virtuais               */
    float cam_zoom;
    float pad[3];         /* padding para alinhamento de 16 bytes              */
};

/* ===========================================================================
 * RendererDX11 — implementação de IRenderer via Direct3D 11
 * =========================================================================*/
class RendererDX11 final : public IRenderer {
public:
    /* --- IRenderer -------------------------------------------------------- */
    bool init           (Engine *e, int win_w, int win_h,
                         const char *title, int scale)         override;
    void destroy        (Engine *e)                            override;

    void clear          (Engine *e)                            override;
    void flush          (Engine *e)                            override;
    void present        (Engine *e)                            override;
    void set_vsync      (bool enable)                          override;

    unsigned int upload_texture(const unsigned char *rgba,
                                unsigned int w,
                                unsigned int h)                override;
    void delete_texture (unsigned int handle)                  override;

    void set_texture    (Engine *e, unsigned int tex)          override;
    void push_quad      (Engine *e, const QuadParams &q)       override;

    void resize         (Engine *e, int new_w, int new_h)      override;
    void toggle_fullscreen(Engine *e)                          override;
    void setup_projection (Engine *e)                          override;

    void camera_push    (Engine *e)                            override;
    void camera_pop     (Engine *e)                            override;

    void draw_line_raw  (Engine *e,
                         float x0, float y0, float x1, float y1,
                         float r, float g, float b,
                         float thickness)                      override;
    void draw_circle_raw(Engine *e,
                         float cx, float cy, float radius,
                         float r, float g, float b,
                         bool filled)                          override;

    void set_blend_inverted(Engine *e)                         override;
    void set_blend_normal  (Engine *e)                         override;
    void set_clear_color   (float r, float g, float b)         override;

    void poll_events    (Engine *e)                            override;

    /* --- FBO -------------------------------------------------------------- */
    FboHandle    fbo_create (Engine *e, int w, int h)          override;
    void         fbo_destroy(Engine *e, FboHandle fh)          override;
    void         fbo_bind   (Engine *e, FboHandle fh)          override;
    void         fbo_unbind (Engine *e)                        override;
    unsigned int fbo_texture(Engine *e, FboHandle fh)          override;

    /* --- Shaders ---------------------------------------------------------- */
    ShaderHandle shader_create (Engine *e,
                                const char *vert_src,
                                const char *frag_src)          override;
    void         shader_destroy(Engine *e, ShaderHandle sh)    override;
    void         shader_use    (Engine *e, ShaderHandle sh)    override;
    void         shader_none   (Engine *e)                     override;
    void         shader_set_int  (Engine *e, ShaderHandle sh,
                                  const char *name, int   v)   override;
    void         shader_set_float(Engine *e, ShaderHandle sh,
                                  const char *name, float v)   override;
    void         shader_set_vec2 (Engine *e, ShaderHandle sh,
                                  const char *name,
                                  float x, float y)            override;
    void         shader_set_vec4 (Engine *e, ShaderHandle sh,
                                  const char *name,
                                  float x, float y,
                                  float z, float w)            override;

    /* Expõe o HWND para que a WndProc estática consiga rotear mensagens */
    HWND hwnd() const { return hwnd_; }

private:
    /* --- Win32 ------------------------------------------------------------ */
    HWND      hwnd_       = nullptr;
    HINSTANCE hinstance_  = nullptr;
    bool      fullscreen_  = false;
    WINDOWPLACEMENT saved_placement_ = {};  /* salvo antes de entrar em fullscreen */

    /* --- D3D11 core ------------------------------------------------------- */
    ID3D11Device*           device_   = nullptr;
    ID3D11DeviceContext*    ctx_      = nullptr;
    IDXGISwapChain*         swapchain_= nullptr;
    ID3D11RenderTargetView* rtv_      = nullptr;

    /* --- Pipeline de renderização ----------------------------------------- */
    ID3D11VertexShader*     vs_          = nullptr;
    ID3D11PixelShader*      ps_          = nullptr;
    ID3D11InputLayout*      input_layout_= nullptr;
    ID3D11Buffer*           cb_vs_       = nullptr;  /* constant buffer DxOrthoVS */

    ID3D11BlendState*       bs_normal_   = nullptr;
    ID3D11BlendState*       bs_inverted_ = nullptr;
    ID3D11RasterizerState*  rs_          = nullptr;
    ID3D11SamplerState*     sampler_     = nullptr;

    /* --- Batch ------------------------------------------------------------- */
    ID3D11Buffer*           vb_      = nullptr;   /* vertex buffer dinâmico        */
    ID3D11Buffer*           ib_      = nullptr;   /* index buffer estático (imutável) */
    DxVertex                vb_cpu_[DX11_MAX_VERT] = {};  /* staging em CPU        */
    uint32_t                quad_count_   = 0;
    uint32_t                current_tex_  = 0;    /* handle da textura ativa       */

    /* --- Pool de texturas -------------------------------------------------- */
    DxTexture               textures_[DX11_MAX_TEXTURES] = {};

    /* --- Pool de FBOs ----------------------------------------------------- */
    DxFbo                   fbos_[DX11_MAX_FBOS]         = {};

    /* --- Pool de shaders -------------------------------------------------- */
    DxShaderProgram         shader_programs_[DX11_MAX_SHADERS] = {};
    ID3D11Buffer*           cb_effect_      = nullptr;  /* cb de uniforms dos shaders */
    DxEffectCB              effect_cb_cpu_  = {};
    ShaderHandle            active_shader_  = -1;

    /* --- Estado ------------------------------------------------------------ */
    float                   clear_color_[4] = {0, 0, 0, 1};
    bool                    vsync_          = true;
    bool                    blend_inverted_ = false;
    DxOrthoVS               ortho_          = {};

    /* --- Câmera ------------------------------------------------------------ */
    bool                    cam_active_ = false;  /* true entre camera_push/camera_pop */

    /* --- Input ------------------------------------------------------------- */
    int                     keys_a_[ENGINE_MAX_KEYS] = {};
    int                     keys_b_[ENGINE_MAX_KEYS] = {};
    int                    *keys_cur_  = keys_a_;
    int                    *keys_prev_ = keys_b_;

    /* --- Helpers de inicialização ------------------------------------------ */
    bool _create_window    (int win_w, int win_h, const char *title);
    bool _create_device_and_swapchain(int win_w, int win_h);
    bool _create_rtv       ();
    bool _create_shaders   ();
    bool _create_states    ();
    bool _create_buffers   ();
    bool _create_effect_cb ();   /* constant buffer para uniforms de shader de efeito */

    void _release_rtv      ();
    void _flush_internal   ();
    void _update_cb        ();
    void _update_effect_cb ();   /* envia effect_cb_cpu_ para a GPU */

    int  _shader_uniform_slot(DxShaderProgram &prog, const char *name);

    uint32_t _alloc_tex_slot();          /* retorna índice de slot livre ou UINT_MAX */
    uint32_t _handle_to_idx (unsigned int handle); /* handle → índice no pool       */

    /*
     * _wnd_proc() — WndProc estática que recupera o ponteiro RendererDX11*
     * via GetWindowLongPtr(GWLP_USERDATA) e delega o processamento ao objeto.
     */
    static LRESULT CALLBACK _wnd_proc(HWND hwnd, UINT msg,
                                       WPARAM wp, LPARAM lp);
};

#endif /* ENGINE_BACKEND_DX11 */
#endif /* RENDERER_DX11_HPP */