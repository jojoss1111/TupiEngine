// RendererDX11.cpp
// Implementação do backend de renderização Direct3D 11.
// Responsável pela criação da janela Win32, inicialização do dispositivo D3D11,
// gerenciamento de texturas, envio de lotes de quads e controle de eventos.

#ifdef ENGINE_BACKEND_DX11

#include "RendererDX11.hpp"
#include "Engine.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

/* ============================================================================
 * Shaders HLSL embutidos
 * ----------------------------------------------------------------------------
 * Vertex Shader:
 *   Recebe posição 2D em pixels virtuais (espaço da engine, Y+ para baixo).
 *   Aplica transformação de câmera (deslocamento e zoom) via constant buffer.
 *   Converte para NDC:
 *     x_ndc = x_px * inv_w - 1
 *     y_ndc = 1 - y_px * inv_h
 *   A inversão do eixo Y é necessária porque o D3D11 adota Y+ para cima no
 *   espaço NDC, enquanto a engine utiliza Y+ para baixo.
 *
 * Pixel Shader:
 *   Realiza amostragem da Texture2D e multiplica pelo canal de cor do vértice.
 * ========================================================================== */

static const char* k_hlsl_vs = RHLSL(
cbuffer OrthoVS : register(b0)
{
    float inv_w;   // 2 / render_w
    float inv_h;   // 2 / render_h
    float cam_tx;  // deslocamento X da câmera em pixels
    float cam_ty;  // deslocamento Y da câmera em pixels
    float cam_zoom;
    float pad0, pad1, pad2;
};

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };

VSOut main(VSIn v)
{
    VSOut o;
    float2 p = (v.pos - float2(cam_tx, cam_ty)) * cam_zoom;
    // Conversão para NDC: x em [-1,1], Y invertido (Y+ = baixo na engine)
    o.pos = float4(p.x * inv_w - 1.0f, 1.0f - p.y * inv_h, 0.0f, 1.0f);
    o.uv  = v.uv;
    o.col = v.col;
    return o;
}
)HLSL;

static const char* k_hlsl_ps = RHLSL(
Texture2D    tex     : register(t0);
SamplerState sampler_ : register(s0);

struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };

float4 main(PSIn i) : SV_TARGET
{
    return tex.Sample(sampler_, i.uv) * i.col;
}
)HLSL;

/* ============================================================================
 * Funções auxiliares internas
 * ========================================================================== */

// Libera com segurança um objeto COM, verificando nulidade antes de chamar Release().
static inline void _safe_release(IUnknown *p) { if (p) p->Release(); }

// Retorna o índice do primeiro slot de textura disponível no pool interno.
// Retorna UINT32_MAX caso não haja slots livres.
uint32_t RendererDX11::_alloc_tex_slot()
{
    for (uint32_t i = 0; i < DX11_MAX_TEXTURES; ++i)
        if (!textures_[i].in_use) return i;
    return UINT32_MAX;
}

// Converte um handle público de textura (base 1) para o índice interno (base 0).
// Retorna UINT32_MAX para handles inválidos.
uint32_t RendererDX11::_handle_to_idx(unsigned int handle)
{
    if (handle == 0 || handle > DX11_MAX_TEXTURES) return UINT32_MAX;
    return handle - 1;
}

/* ============================================================================
 * Procedimento de janela Win32 (WndProc)
 * ========================================================================== */
LRESULT CALLBACK RendererDX11::_wnd_proc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp)
{
    RendererDX11 *self = reinterpret_cast<RendererDX11*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // O redimensionamento é tratado em poll_events(), após o Engine estar pronto.
        return 0;
    default:
        break;
    }
    (void)self;
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ============================================================================
 * _create_window
 * Registra a classe de janela Win32 e cria a janela principal da aplicação.
 * Associa o ponteiro 'this' à janela para uso no WndProc via GWLP_USERDATA.
 * ========================================================================== */
bool RendererDX11::_create_window(int win_w, int win_h, const char *title)
{
    hinstance_ = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = _wnd_proc;
    wc.hInstance     = hinstance_;
    wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "EngineDX11";
    RegisterClassExA(&wc);

    RECT rc = { 0, 0, win_w, win_h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExA(
        0, "EngineDX11", title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hinstance_, nullptr);

    if (!hwnd_) {
        fprintf(stderr, "RendererDX11: CreateWindowExA falhou (0x%lx)\n",
                GetLastError());
        return false;
    }

    // Armazena o ponteiro da instância para recuperação no WndProc.
    SetWindowLongPtrA(hwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    return true;
}

/* ============================================================================
 * _create_device_and_swapchain
 * Inicializa o dispositivo D3D11 e a swap chain com duplo buffer (flip model).
 * Tenta feature levels 11.0, 10.1 e 10.0, nessa ordem de preferência.
 * Em builds de debug, ativa a camada de validação D3D11.
 * ========================================================================== */
bool RendererDX11::_create_device_and_swapchain(int win_w, int win_h)
{
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount       = 2;
    scd.BufferDesc.Width  = static_cast<UINT>(win_w);
    scd.BufferDesc.Height = static_cast<UINT>(win_h);
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator   = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd_;
    scd.SampleDesc.Count = 1;
    scd.Windowed     = TRUE;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL out_level = D3D_FEATURE_LEVEL_10_0;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, feature_levels, 3,
        D3D11_SDK_VERSION, &scd,
        &swapchain_, &device_, &out_level, &ctx_);

    if (FAILED(hr)) {
        fprintf(stderr, "RendererDX11: D3D11CreateDeviceAndSwapChain falhou (0x%lx)\n", hr);
        return false;
    }
    return true;
}

/* ============================================================================
 * _create_rtv / _release_rtv
 * Cria e libera a Render Target View associada ao back buffer da swap chain.
 * ========================================================================== */
bool RendererDX11::_create_rtv()
{
    ID3D11Texture2D *back_buf = nullptr;
    HRESULT hr = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&back_buf));
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(back_buf, nullptr, &rtv_);
    back_buf->Release();
    return SUCCEEDED(hr);
}

void RendererDX11::_release_rtv()
{
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    _safe_release(rtv_);
    rtv_ = nullptr;
}

/* ============================================================================
 * _create_shaders
 * Compila os shaders HLSL em tempo de execução e cria os objetos de shader
 * e o input layout correspondente ao vértice DxVertex.
 * ========================================================================== */
bool RendererDX11::_create_shaders()
{
    ID3DBlob *vs_blob = nullptr, *ps_blob = nullptr, *err = nullptr;
    HRESULT hr;

    // Compilação do vertex shader.
    hr = D3DCompile(k_hlsl_vs, strlen(k_hlsl_vs), "vs", nullptr, nullptr,
                    "main", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &vs_blob, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "RendererDX11 VS: %s\n",
                err ? static_cast<char*>(err->GetBufferPointer()) : "?");
        _safe_release(err);
        return false;
    }

    hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                      vs_blob->GetBufferSize(),
                                      nullptr, &vs_);
    if (FAILED(hr)) { _safe_release(vs_blob); return false; }

    // Definição do input layout: posição 2D, coordenadas UV e cor RGBA.
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(DxVertex,x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(DxVertex,u), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DxVertex,r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = device_->CreateInputLayout(layout, 3,
                                     vs_blob->GetBufferPointer(),
                                     vs_blob->GetBufferSize(),
                                     &input_layout_);
    _safe_release(vs_blob);
    if (FAILED(hr)) return false;

    // Compilação do pixel shader.
    hr = D3DCompile(k_hlsl_ps, strlen(k_hlsl_ps), "ps", nullptr, nullptr,
                    "main", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &ps_blob, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "RendererDX11 PS: %s\n",
                err ? static_cast<char*>(err->GetBufferPointer()) : "?");
        _safe_release(err);
        return false;
    }
    hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                     ps_blob->GetBufferSize(),
                                     nullptr, &ps_);
    _safe_release(ps_blob);
    return SUCCEEDED(hr);
}

/* ============================================================================
 * _create_states
 * Inicializa os estados fixos do pipeline:
 *   - Blend normal: alpha pré-multiplicado (src_alpha / 1 - src_alpha).
 *   - Blend invertido: equivalente ao GL_ONE_MINUS_DST_COLOR / GL_ZERO.
 *   - Rasterizador: sem culling e sem depth clipping.
 *   - Sampler: filtragem por ponto (pixel art, sem interpolação).
 *   - Constant buffer do vertex shader (OrthoVS, 48 bytes alinhados a 16).
 * ========================================================================== */
bool RendererDX11::_create_states()
{
    HRESULT hr;

    // Blend normal: src_alpha / 1 - src_alpha.
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable           = TRUE;
        bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device_->CreateBlendState(&bd, &bs_normal_);
        if (FAILED(hr)) return false;
    }

    // Blend invertido: ONE_MINUS_DST_COLOR / ZERO (equivalente ao OpenGL).
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable           = TRUE;
        bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_INV_DEST_COLOR;
        bd.RenderTarget[0].DestBlend             = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device_->CreateBlendState(&bd, &bs_inverted_);
        if (FAILED(hr)) return false;
    }

    // Rasterizador: sem culling de faces e sem depth clipping
    // (equivalente ao OpenGL sem GL_DEPTH_TEST).
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode              = D3D11_FILL_SOLID;
        rd.CullMode              = D3D11_CULL_NONE;
        rd.DepthClipEnable       = FALSE;
        rd.ScissorEnable         = FALSE;
        rd.MultisampleEnable     = FALSE;
        hr = device_->CreateRasterizerState(&rd, &rs_);
        if (FAILED(hr)) return false;
    }

    // Sampler com filtragem por ponto (adequado para pixel art, sem interpolação).
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        hr = device_->CreateSamplerState(&sd, &sampler_);
        if (FAILED(hr)) return false;
    }

    // Constant buffer para o vertex shader (OrthoVS — 48 bytes, alinhado a 16 bytes).
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sizeof(DxOrthoVS);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&bd, nullptr, &cb_vs_);
        if (FAILED(hr)) return false;
    }

    return true;
}

/* ============================================================================
 * _create_buffers
 * Cria o vertex buffer dinâmico e o index buffer estático.
 * O index buffer contém o padrão (0,1,2, 0,2,3) repetido para cada quad,
 * permitindo renderizar lotes de quads com um único DrawIndexed.
 * ========================================================================== */
bool RendererDX11::_create_buffers()
{
    HRESULT hr;

    // Vertex buffer dinâmico — sobrescrito a cada frame via Map/Unmap.
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sizeof(DxVertex) * DX11_MAX_VERT;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&bd, nullptr, &vb_);
        if (FAILED(hr)) return false;
    }

    // Index buffer estático com padrão de dois triângulos por quad (0,1,2, 0,2,3).
    {
        uint16_t indices[DX11_MAX_IDX];
        for (uint32_t q = 0; q < DX11_BATCH_MAX_QUADS; ++q) {
            uint16_t b = static_cast<uint16_t>(q * 4);
            indices[q*6+0] = b;
            indices[q*6+1] = b+1;
            indices[q*6+2] = b+2;
            indices[q*6+3] = b;
            indices[q*6+4] = b+2;
            indices[q*6+5] = b+3;
        }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(indices);
        bd.Usage     = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = indices;
        hr = device_->CreateBuffer(&bd, &sd, &ib_);
        if (FAILED(hr)) return false;
    }

    return true;
}

/* ============================================================================
 * _update_cb
 * Atualiza o constant buffer de projeção ortográfica no vertex shader.
 * Deve ser chamado sempre que os parâmetros de câmera ou projeção forem alterados.
 * ========================================================================== */
void RendererDX11::_update_cb()
{
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx_->Map(cb_vs_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, &ortho_, sizeof(DxOrthoVS));
        ctx_->Unmap(cb_vs_, 0);
    }
    ctx_->VSSetConstantBuffers(0, 1, &cb_vs_);
}

/* ============================================================================
 * IRenderer::init
 * Inicializa todos os subsistemas do renderer na ordem correta:
 * janela → dispositivo/swap chain → RTV → shaders → estados → buffers.
 * Configura o estado fixo do pipeline e cria a textura branca 1×1 de fallback.
 * ========================================================================== */
bool RendererDX11::init(Engine *e, int win_w, int win_h,
                         const char *title, int scale)
{
    (void)scale;
    e->win_w    = win_w;
    e->win_h    = win_h;
    e->render_w = win_w;
    e->render_h = win_h;

    if (!_create_window(win_w, win_h, title))              return false;
    if (!_create_device_and_swapchain(win_w, win_h))       return false;
    if (!_create_rtv())                                    return false;
    if (!_create_shaders())                                return false;
    if (!_create_states())                                 return false;
    if (!_create_buffers())                                return false;

    // Configuração do estado fixo do pipeline de renderização.
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(input_layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    ctx_->RSSetState(rs_);

    float blend_factor[4] = {0,0,0,0};
    ctx_->OMSetBlendState(bs_normal_, blend_factor, 0xFFFFFFFF);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);

    ctx_->IASetIndexBuffer(ib_, DXGI_FORMAT_R16_UINT, 0);

    UINT stride = sizeof(DxVertex), offset = 0;
    ctx_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);

    setup_projection(e);

    // Textura branca 1×1 utilizada como fallback para quads sem textura.
    const unsigned char white[4] = {255,255,255,255};
    e->white_tex = upload_texture(white, 1, 1);

    e->keys      = keys_cur_;
    e->keys_prev = keys_prev_;

    return true;
}

/* ============================================================================
 * IRenderer::destroy
 * Libera todos os recursos D3D11 e destrói a janela Win32.
 * Os recursos são liberados na ordem inversa à da inicialização.
 * ========================================================================== */
void RendererDX11::destroy(Engine *)
{
    if (ctx_) ctx_->ClearState();

    for (uint32_t i = 0; i < DX11_MAX_TEXTURES; ++i) {
        if (!textures_[i].in_use) continue;
        _safe_release(textures_[i].srv);
        _safe_release(textures_[i].tex);
        textures_[i] = {};
    }

    _release_rtv();
    _safe_release(ib_);
    _safe_release(vb_);
    _safe_release(cb_vs_);
    _safe_release(sampler_);
    _safe_release(rs_);
    _safe_release(bs_inverted_);
    _safe_release(bs_normal_);
    _safe_release(input_layout_);
    _safe_release(ps_);
    _safe_release(vs_);
    _safe_release(swapchain_);
    _safe_release(ctx_);
    _safe_release(device_);

    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
}

/* ============================================================================
 * Gerenciamento de texturas
 * ========================================================================== */

// Carrega uma textura RGBA na GPU e retorna o handle público (índice + 1).
// Retorna 0 em caso de falha ou pool esgotado.
unsigned int RendererDX11::upload_texture(const unsigned char *rgba,
                                           unsigned int w, unsigned int h)
{
    uint32_t slot = _alloc_tex_slot();
    if (slot == UINT32_MAX) {
        fprintf(stderr, "RendererDX11: pool de texturas esgotado\n");
        return 0;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_IMMUTABLE;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem     = rgba;
    sd.SysMemPitch = w * 4;

    HRESULT hr = device_->CreateTexture2D(&td, &sd, &textures_[slot].tex);
    if (FAILED(hr)) return 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels       = 1;
    hr = device_->CreateShaderResourceView(textures_[slot].tex, &srvd,
                                            &textures_[slot].srv);
    if (FAILED(hr)) {
        _safe_release(textures_[slot].tex);
        return 0;
    }

    textures_[slot].in_use = true;
    return slot + 1; // Handle público = índice interno + 1.
}

// Libera os recursos D3D11 associados ao handle de textura fornecido.
void RendererDX11::delete_texture(unsigned int handle)
{
    uint32_t idx = _handle_to_idx(handle);
    if (idx == UINT32_MAX || !textures_[idx].in_use) return;
    _safe_release(textures_[idx].srv);
    _safe_release(textures_[idx].tex);
    textures_[idx] = {};
}

/* ============================================================================
 * Ciclo de frame: clear / flush / present
 * ========================================================================== */

// Limpa o render target, redefine o viewport e prepara o estado para o novo frame.
// O constant buffer de câmera é atualizado aqui, pois pode ter mudado no frame anterior.
void RendererDX11::clear(Engine *e)
{
    ctx_->ClearRenderTargetView(rtv_, clear_color_);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width    = static_cast<float>(e->win_w);
    vp.Height   = static_cast<float>(e->win_h);
    vp.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &vp);

    quad_count_  = 0;
    current_tex_ = 0;

    // Atualiza o constant buffer — a câmera pode ter sido alterada no frame anterior.
    _update_cb();
}

// Envia o lote atual de vértices para a GPU e emite a chamada de draw.
// Não realiza o envio se não houver quads ou textura ativa.
void RendererDX11::_flush_internal()
{
    if (quad_count_ == 0 || current_tex_ == 0) return;

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx_->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, vb_cpu_,
               sizeof(DxVertex) * quad_count_ * DX11_VERTS_PER_QUAD);
        ctx_->Unmap(vb_, 0);
    }

    uint32_t idx = _handle_to_idx(current_tex_);
    if (idx != UINT32_MAX && textures_[idx].in_use)
        ctx_->PSSetShaderResources(0, 1, &textures_[idx].srv);

    ctx_->DrawIndexed(quad_count_ * DX11_INDICES_PER_QUAD, 0, 0);
    quad_count_ = 0;
}

void RendererDX11::flush(Engine *)
{
    _flush_internal();
}

// Envia o lote restante e apresenta o frame na tela via swap chain.
void RendererDX11::present(Engine *)
{
    _flush_internal();
    swapchain_->Present(vsync_ ? 1 : 0, 0);
}

void RendererDX11::set_vsync(bool enable) { vsync_ = enable; }

/* ============================================================================
 * set_texture / push_quad
 * ========================================================================== */

// Define a textura ativa para os próximos quads.
// Realiza flush do lote atual se a textura for diferente da anterior.
void RendererDX11::set_texture(Engine *, unsigned int tex)
{
    if (tex == current_tex_) return;
    _flush_internal();
    current_tex_ = tex;
}

// Adiciona um quad ao lote de renderização atual.
// Aplica flip de UV e rotação quando necessário.
// Realiza flush automático ao atingir o limite do lote.
void RendererDX11::push_quad(Engine *, const QuadParams &q)
{
    if (quad_count_ >= DX11_BATCH_MAX_QUADS) _flush_internal();

    float u0 = q.u0, v0 = q.v0, u1 = q.u1, v1 = q.v1;
    if (q.flip_h) { float t = u0; u0 = u1; u1 = t; }
    if (q.flip_v) { float t = v0; v0 = v1; v1 = t; }

    DxVertex *v = vb_cpu_ + quad_count_ * DX11_VERTS_PER_QUAD;

    if (q.rotation == 0.0f) {
        // Caminho rápido: sem rotação.
        v[0] = { q.dx,         q.dy,         u0, v0, q.r, q.g, q.b, q.a };
        v[1] = { q.dx + q.dw,  q.dy,         u1, v0, q.r, q.g, q.b, q.a };
        v[2] = { q.dx + q.dw,  q.dy + q.dh,  u1, v1, q.r, q.g, q.b, q.a };
        v[3] = { q.dx,         q.dy + q.dh,  u0, v1, q.r, q.g, q.b, q.a };
    } else {
        // Caminho com rotação: calcula os vértices em torno do centro do quad.
        float cx  = q.dx + q.dw * 0.5f;
        float cy  = q.dy + q.dh * 0.5f;
        float rad = q.rotation * 3.14159265f / 180.0f;
        float c   = cosf(rad), s = sinf(rad);
        float hw  = q.dw * 0.5f, hh = q.dh * 0.5f;

        auto rot = [&](float ox, float oy, float &rx, float &ry) {
            rx = cx + ox*c - oy*s;
            ry = cy + ox*s + oy*c;
        };
        float x0,y0, x1,y1, x2,y2, x3,y3;
        rot(-hw, -hh, x0, y0);
        rot( hw, -hh, x1, y1);
        rot( hw,  hh, x2, y2);
        rot(-hw,  hh, x3, y3);

        v[0] = { x0, y0, u0, v0, q.r, q.g, q.b, q.a };
        v[1] = { x1, y1, u1, v0, q.r, q.g, q.b, q.a };
        v[2] = { x2, y2, u1, v1, q.r, q.g, q.b, q.a };
        v[3] = { x3, y3, u0, v1, q.r, q.g, q.b, q.a };
    }

    ++quad_count_;
}

/* ============================================================================
 * Projeção ortográfica
 * ========================================================================== */

// Configura a projeção ortográfica padrão, sem câmera ativa.
void RendererDX11::setup_projection(Engine *e)
{
    ortho_.inv_w    = 2.0f / static_cast<float>(e->render_w);
    ortho_.inv_h    = 2.0f / static_cast<float>(e->render_h);
    ortho_.cam_tx   = 0.0f;
    ortho_.cam_ty   = 0.0f;
    ortho_.cam_zoom = 1.0f;
    _update_cb();
}

// Reconstrói a swap chain e a RTV ao redimensionar a janela.
void RendererDX11::resize(Engine *e, int new_w, int new_h)
{
    _flush_internal();
    _release_rtv();

    e->win_w    = new_w;
    e->win_h    = new_h;
    e->render_w = new_w;
    e->render_h = new_h;

    swapchain_->ResizeBuffers(0, static_cast<UINT>(new_w),
                               static_cast<UINT>(new_h),
                               DXGI_FORMAT_UNKNOWN, 0);
    _create_rtv();
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    setup_projection(e);
}

/* ============================================================================
 * Câmera
 * ========================================================================== */

// Ativa a câmera e aplica os parâmetros de deslocamento e zoom ao constant buffer.
void RendererDX11::camera_push(Engine *e)
{
    if (!e->camera_enabled) return;
    flush(e);
    cam_active_     = true;
    ortho_.cam_tx   = e->camera.x - e->camera.shake_x;
    ortho_.cam_ty   = e->camera.y - e->camera.shake_y;
    ortho_.cam_zoom = e->camera.zoom;
    _update_cb();
}

// Desativa a câmera e restaura a projeção sem transformação de câmera.
void RendererDX11::camera_pop(Engine *e)
{
    if (!e->camera_enabled) return;
    flush(e);
    cam_active_     = false;
    ortho_.cam_tx   = 0.0f;
    ortho_.cam_ty   = 0.0f;
    ortho_.cam_zoom = 1.0f;
    _update_cb();
}

/* ============================================================================
 * Primitivas geométricas diretas
 * ========================================================================== */

// Renderiza uma linha entre dois pontos com espessura definida,
// representada como um quad fino rotacionado na direção do segmento.
void RendererDX11::draw_line_raw(Engine *e,
                                  float x0, float y0, float x1, float y1,
                                  float r,  float g,  float b,  float thickness)
{
    set_texture(e, e->white_tex);

    float dx  = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;

    QuadParams q{};
    q.dx       = x0;
    q.dy       = y0 - thickness * 0.5f;
    q.dw       = len;
    q.dh       = thickness;
    q.u0       = 0; q.v0 = 0; q.u1 = 1; q.v1 = 1;
    q.r = r; q.g = g; q.b = b; q.a = 1.0f;
    q.rotation = -atan2f(dy, dx) * (180.0f / 3.14159265f);
    push_quad(e, q);
}

// Renderiza um círculo preenchido ou em contorno.
// Preenchido: fan de triângulos via quads degenerados (dois vértices no centro).
// Contorno: N segmentos de linha ao longo do perímetro.
void RendererDX11::draw_circle_raw(Engine *e,
                                    float cx, float cy, float radius,
                                    float r,  float g,  float b,
                                    bool filled)
{
    set_texture(e, e->white_tex);

    static constexpr int   SEG  = 24;
    static constexpr float STEP = 2.0f * 3.14159265f / SEG;

    if (filled) {
        // Aproximação de fan via quads degenerados: dois vértices coincidem no centro.
        for (int i = 0; i < SEG; ++i) {
            if (quad_count_ >= DX11_BATCH_MAX_QUADS) _flush_internal();
            float a0 = STEP * i, a1 = STEP * (i + 1);
            DxVertex *v = vb_cpu_ + quad_count_ * 4;
            v[0] = { cx,                    cy,                    0.5f,0.5f, r,g,b,1 };
            v[1] = { cx + cosf(a0)*radius,  cy + sinf(a0)*radius,  0,0,      r,g,b,1 };
            v[2] = { cx + cosf(a1)*radius,  cy + sinf(a1)*radius,  1,1,      r,g,b,1 };
            v[3] = { cx,                    cy,                    0.5f,0.5f, r,g,b,1 };
            ++quad_count_;
        }
    } else {
        // Contorno: segmentos de linha entre vértices consecutivos do perímetro.
        for (int i = 0; i < SEG; ++i) {
            float a0 = STEP * i, a1 = STEP * (i + 1);
            draw_line_raw(e,
                          cx + cosf(a0)*radius, cy + sinf(a0)*radius,
                          cx + cosf(a1)*radius, cy + sinf(a1)*radius,
                          r, g, b, 1.0f);
        }
    }
}

/* ============================================================================
 * Modos de blend
 * ========================================================================== */

// Ativa o modo de blend invertido e realiza flush do lote atual antes da troca.
void RendererDX11::set_blend_inverted(Engine *)
{
    _flush_internal();
    blend_inverted_ = true;
    float bf[4] = {};
    ctx_->OMSetBlendState(bs_inverted_, bf, 0xFFFFFFFF);
}

// Restaura o modo de blend normal e realiza flush do lote atual antes da troca.
void RendererDX11::set_blend_normal(Engine *)
{
    _flush_internal();
    blend_inverted_ = false;
    float bf[4] = {};
    ctx_->OMSetBlendState(bs_normal_, bf, 0xFFFFFFFF);
}

// Define a cor de limpeza do frame (clear color) em formato RGB normalizado.
void RendererDX11::set_clear_color(float r, float g, float b)
{
    clear_color_[0] = r;
    clear_color_[1] = g;
    clear_color_[2] = b;
    clear_color_[3] = 1.0f;
}

/* ============================================================================
 * Tela cheia
 * ========================================================================== */

// Alterna entre os modos janela e tela cheia.
// Em tela cheia, remove as bordas e maximiza sobre o monitor principal.
// Ao retornar para janela, restaura a posição e o tamanho anteriores.
void RendererDX11::toggle_fullscreen(Engine *e)
{
    if (!fullscreen_) {
        GetWindowPlacement(hwnd_, &saved_placement_);
        // Remove bordas e maximiza sobre o monitor.
        SetWindowLongA(hwnd_, GWL_STYLE,
                       GetWindowLongA(hwnd_, GWL_STYLE) &
                       ~(WS_CAPTION | WS_THICKFRAME));

        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
        int w = mi.rcMonitor.right  - mi.rcMonitor.left;
        int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

        SetWindowPos(hwnd_, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top, w, h,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        fullscreen_ = true;
        resize(e, w, h);
    } else {
        SetWindowLongA(hwnd_, GWL_STYLE,
                       GetWindowLongA(hwnd_, GWL_STYLE) |
                       (WS_CAPTION | WS_THICKFRAME));
        SetWindowPlacement(hwnd_, &saved_placement_);
        fullscreen_ = false;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        resize(e, rc.right, rc.bottom);
    }
}

/* ============================================================================
 * Processamento de eventos Win32
 * ========================================================================== */

// Processa a fila de mensagens Win32 e atualiza o estado de entrada do Engine.
// Os buffers de teclado são alternados a cada frame (custo O(1)).
// O redimensionamento da janela é tratado diretamente neste método.
void RendererDX11::poll_events(Engine *e)
{
    // Alternância dos buffers de teclado com custo O(1).
    int *tmp   = keys_prev_;
    keys_prev_ = keys_cur_;
    keys_cur_  = tmp;
    memset(keys_cur_, 0, sizeof(int) * ENGINE_MAX_KEYS);
    e->keys      = keys_cur_;
    e->keys_prev = keys_prev_;

    memcpy(e->mouse.buttons_prev, e->mouse.buttons, sizeof(e->mouse.buttons));
    e->mouse.scroll = 0;

    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            e->running = 0;
        }

        // Processa eventos de entrada antes do dispatch padrão do Windows.
        switch (msg.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UINT vk = static_cast<UINT>(msg.wParam);
            if (vk < ENGINE_MAX_KEYS) keys_cur_[vk] = 1;
            if (vk == VK_ESCAPE) e->running = 0;
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            UINT vk = static_cast<UINT>(msg.wParam);
            if (vk < ENGINE_MAX_KEYS) keys_cur_[vk] = 0;
            break;
        }
        case WM_LBUTTONDOWN: e->mouse.buttons[0] = 1; break;
        case WM_LBUTTONUP:   e->mouse.buttons[0] = 0; break;
        case WM_MBUTTONDOWN: e->mouse.buttons[1] = 1; break;
        case WM_MBUTTONUP:   e->mouse.buttons[1] = 0; break;
        case WM_RBUTTONDOWN: e->mouse.buttons[2] = 1; break;
        case WM_RBUTTONUP:   e->mouse.buttons[2] = 0; break;
        case WM_MOUSEWHEEL:
            e->mouse.scroll = (GET_WHEEL_DELTA_WPARAM(msg.wParam) > 0) ? 1 : -1;
            break;
        case WM_MOUSEMOVE:
            e->mouse.x = LOWORD(msg.lParam);
            e->mouse.y = HIWORD(msg.lParam);
            break;
        case WM_SIZE:
            if (device_ && msg.wParam != SIZE_MINIMIZED) {
                resize(e, LOWORD(msg.lParam), HIWORD(msg.lParam));
            }
            break;
        default:
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

#endif /* ENGINE_BACKEND_DX11 */