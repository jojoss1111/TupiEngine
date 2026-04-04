// RendererVK.cpp
/*
 * Implementação do backend Vulkan 2D (RendererVK) — versão otimizada.
 *
 * Compile com:
 *   g++ -std=c++17 -DENGINE_BACKEND_VK RendererVK.cpp Engine_renderer.cpp \
 *       $(pkg-config --cflags --libs vulkan sdl3)    \
 *       -lglslang -lSPIRV -lpng -lpthread -o mygame
 *
 * Coloque vk_mem_alloc.h no include path (AMD GPUOpen, MIT license).
 */

#ifdef ENGINE_BACKEND_VK

/* VMA: a implementação é single-header — define aqui, uma vez só. */
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "RendererVK.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>

/* Shaders padrao pre-compilados em SPIR-V (gerado por compile_shaders.sh).
 * Se o arquivo nao existir, _create_pipeline() usa glslang como fallback. */
#if __has_include("shaders_embedded.hpp")
#  include "shaders_embedded.hpp"
#  define VK_HAS_EMBEDDED_SHADERS 1
#else
#  define VK_HAS_EMBEDDED_SHADERS 0
#endif

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

/* SIMD: SSE4.1 para calcular posição dos 4 vértices em paralelo. */
#if defined(__SSE4_1__) || defined(__AVX__)
#  include <immintrin.h>
#  define VK_USE_SIMD 1
#else
#  define VK_USE_SIMD 0
#endif

/* =========================================================================
 * SPIR-V embutido — NÃO USADO
 *
 * Os shaders são compilados em runtime via glslang (_compile_glsl).
 * Os arrays abaixo foram removidos pois continham apenas cabeçalhos
 * incompletos que causariam falha silenciosa se usados como fallback.
 * Se quiser embutir SPIR-V pré-compilado no futuro, gere com:
 *   glslc quad.vert -o quad.vert.spv
 *   glslc quad.frag -o quad.frag.spv
 *   xxd -i quad.vert.spv  →  copie para cá como s_vert_spv[]
 * ====================================================================== */
const uint32_t RendererVK::s_vert_spv[]  = {};
const size_t   RendererVK::s_vert_spv_size = 0;
const uint32_t RendererVK::s_frag_spv[]  = {};
const size_t   RendererVK::s_frag_spv_size = 0;

/* =========================================================================
 * Validation layers
 * ====================================================================== */
#ifdef NDEBUG
static constexpr bool k_enable_validation = false;
#else
static constexpr bool k_enable_validation = true;
#endif

/* arquivo onde o pipeline cache é salvo entre execuções */
static constexpr const char *k_pipeline_cache_file = "vk_pipeline_cache.bin";

static VKAPI_ATTR VkBool32 VKAPI_CALL _vk_debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[VK] %s\n", data->pMessage);
    return VK_FALSE;
}

/* =========================================================================
 * Macro VK_CHECK
 * ====================================================================== */
#define VK_CHECK(expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error %d em %s:%d\n", \
                    (int)_r, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

/* =========================================================================
 * Ortho column-major para NDC Vulkan (Y para baixo)
 * ====================================================================== */
static void _ortho(float l, float r, float b, float t,
                   float n, float f, float out[16])
{
    memset(out, 0, 64);
    out[0]  =  2.f / (r - l);
    out[5]  =  2.f / (t - b);
    out[10] = -2.f / (f - n);
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[14] = -(f + n) / (f - n);
    out[15] =  1.f;
}

/* =========================================================================
 * init()
 * ====================================================================== */
bool RendererVK::init(Engine *e, int win_w, int win_h,
                      const char *title, int scale)
{
    m_win_w = win_w * scale;
    m_win_h = win_h * scale;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false;
    }
    m_window = SDL_CreateWindow(title, m_win_w, m_win_h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false;
    }

    if (!_create_instance(title))          return false;
    if (!_pick_physical_device())          return false;
    if (!_create_device())                 return false;
    if (!_create_vma())                    return false; /* inicializa alocador de memória */
    if (!_create_swapchain())              return false;
    if (!_create_render_pass())            return false;
    if (!_create_framebuffers())           return false;
    if (!_create_bindless_layout())        return false; /* layout do array bindless */
    if (!_create_bindless_pool_and_set())  return false; /* pool e descriptor set únicos */
    if (!_create_pipeline_layout())        return false;
    if (!_create_pipeline_cache())         return false; /* cache persiste em disco */
    if (!_create_pipeline(false, &m_pipeline))     return false;
    if (!_create_pipeline(true,  &m_pipeline_inv)) return false;
    _save_pipeline_cache();
    if (!_create_command_pool())           return false;
    if (!_create_vertex_ring_buffer())     return false; /* vértices mapeados permanentemente */
    if (!_create_index_buffer())           return false;
    if (!_create_sync_objects())           return false;
    if (!_create_white_texture())          return false;

    VkCommandBufferAllocateInfo cb_ai{};
    cb_ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_ai.commandPool        = m_cmd_pool;
    cb_ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_ai.commandBufferCount = VK_MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &cb_ai, m_cmd_bufs));

    setup_projection(e);
    m_cur_tex = m_white_tex;
    fprintf(stdout, "RendererVK: inicializado (%dx%d).\n", m_win_w, m_win_h);
    return true;
}

/* =========================================================================
 * destroy()
 * ====================================================================== */
void RendererVK::destroy(Engine *e)
{
    if (!m_device) return;
    vkDeviceWaitIdle(m_device);

    for (int i = 0; i < VK_MAX_TEXTURES; ++i)
        if (m_tex_slots[i].in_use) delete_texture((unsigned int)i);
    for (int i = 0; i < ENGINE_MAX_FBOS; ++i)
        if (m_fbo_slots[i].in_use) fbo_destroy(e, (FboHandle)i);
    for (int i = 0; i < ENGINE_MAX_SHADERS; ++i)
        if (m_shader_slots[i].in_use) shader_destroy(e, (ShaderHandle)i);

    /* ring buffer de vértices — um por frame em flight */
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (m_vb[i])
            vmaDestroyBuffer(m_vma, m_vb[i], m_vb_alloc[i]);
    }
    /* index buffer */
    if (m_ib) vmaDestroyBuffer(m_vma, m_ib, m_ib_alloc);

    if (m_pipeline)      vkDestroyPipeline(m_device, m_pipeline,      nullptr);
    if (m_pipeline_inv)  vkDestroyPipeline(m_device, m_pipeline_inv,  nullptr);
    if (m_pipe_layout)   vkDestroyPipelineLayout(m_device, m_pipe_layout, nullptr);

    /* salva pipeline cache no disco antes de destruir */
    _save_pipeline_cache();
    if (m_pipeline_cache) vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);

    /* descriptor set bindless global */
    if (m_desc_pool)   vkDestroyDescriptorPool      (m_device, m_desc_pool,   nullptr);
    if (m_desc_layout) vkDestroyDescriptorSetLayout  (m_device, m_desc_layout, nullptr);

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (m_img_available[i]) vkDestroySemaphore(m_device, m_img_available[i], nullptr);
        if (m_render_done[i])   vkDestroySemaphore(m_device, m_render_done[i],   nullptr);
        if (m_in_flight[i])     vkDestroyFence    (m_device, m_in_flight[i],     nullptr);
    }
    if (m_cmd_pool) vkDestroyCommandPool(m_device, m_cmd_pool, nullptr);

    _destroy_swapchain();
    if (m_render_pass) vkDestroyRenderPass(m_device, m_render_pass, nullptr);

    /* VMA deve ser destruído antes do device */
    if (m_vma) vmaDestroyAllocator(m_vma);

    if (m_device)  vkDestroyDevice(m_device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (k_enable_validation && m_debug_msg) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, m_debug_msg, nullptr);
    }
    if (m_instance) vkDestroyInstance(m_instance, nullptr);

    /* finaliza glslang (inicializado uma única vez, finalizado aqui) */
    if (m_glslang_init) {
        glslang::FinalizeProcess();
        m_glslang_init = false;
    }

    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

/* =========================================================================
 * clear()
 * ====================================================================== */
void RendererVK::clear(Engine *e)
{
    vkWaitForFences(m_device, 1, &m_in_flight[m_frame_idx], VK_TRUE, UINT64_MAX);

    VkResult res = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_img_available[m_frame_idx], VK_NULL_HANDLE, &m_image_idx);

    if (res == VK_ERROR_OUT_OF_DATE_KHR) { _recreate_swapchain(); return; }

    vkResetFences(m_device, 1, &m_in_flight[m_frame_idx]);

    VkCommandBuffer cb = m_cmd_bufs[m_frame_idx];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    m_in_frame    = true;
    m_batch_count = 0;
    m_active_fbo  = nullptr;
    _begin_render_pass();
}

/* =========================================================================
 * flush() / present()
 * ====================================================================== */
void RendererVK::flush(Engine *e) { _flush_batch(); }

void RendererVK::present(Engine *e)
{
    if (!m_in_frame) return;
    _flush_batch();
    _end_render_pass();

    VkCommandBuffer cb = m_cmd_bufs[m_frame_idx];
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &m_img_available[m_frame_idx];
    si.pWaitDstStageMask    = &wait_stage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_render_done[m_frame_idx];
    vkQueueSubmit(m_gfx_queue, 1, &si, m_in_flight[m_frame_idx]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_render_done[m_frame_idx];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &m_image_idx;
    VkResult res = vkQueuePresentKHR(m_gfx_queue, &pi);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        _recreate_swapchain();

    m_frame_idx = (m_frame_idx + 1) % VK_MAX_FRAMES_IN_FLIGHT;
    m_in_frame  = false;
    m_in_pass   = false;
}

/* =========================================================================
 * set_vsync()
 * ====================================================================== */
void RendererVK::set_vsync(bool enable)
{
    if (m_vsync == enable) return;
    m_vsync = enable;
    vkDeviceWaitIdle(m_device);
    _recreate_swapchain();
}

/* =========================================================================
 * upload_texture()
 *
 * Aloca staging buffer + imagem via VMA.
 * Registra a textura no array bindless.
 * ====================================================================== */
unsigned int RendererVK::upload_texture(const unsigned char *rgba,
                                         unsigned int w, unsigned int h)
{
    unsigned int slot_idx = _alloc_texture_slot();
    if (slot_idx >= (unsigned int)VK_MAX_TEXTURES) {
        fprintf(stderr, "RendererVK: sem slots de textura.\n");
        return 0;
    }
    VkTextureSlot &s = m_tex_slots[slot_idx];
    s.width  = w;
    s.height = h;

    /* staging buffer host-visible para copiar pixels */
    VkDeviceSize  size = (VkDeviceSize)w * h * 4;
    VkBuffer      stg_buf;
    VmaAllocation stg_alloc;
    void         *stg_ptr = nullptr;
    _vma_create_buffer(size,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VMA_MEMORY_USAGE_CPU_ONLY,
                       VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       stg_buf, stg_alloc, &stg_ptr);
    memcpy(stg_ptr, rgba, (size_t)size);
    /* Flush explícito: garante visibilidade para a GPU em memórias não-coherent
     * (ex: ARM Mali, alguns iGPUs Intel). Em memória coherent (AMD, NVIDIA)
     * vmaFlushAllocation é no-op — sem custo. */
    vmaFlushAllocation(m_vma, stg_alloc, 0, VK_WHOLE_SIZE);

    /* imagem final GPU-only */
    _vma_create_image(w, h,
                      VK_FORMAT_R8G8B8A8_SRGB,
                      VK_IMAGE_TILING_OPTIMAL,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      s.image, s.alloc);

    _transition_image_layout(s.image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    _copy_buffer_to_image(stg_buf, s.image, w, h);
    _transition_image_layout(s.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /* libera staging: VMA cuida da memória */
    vmaDestroyBuffer(m_vma, stg_buf, stg_alloc);

    s.view = _create_image_view(s.image, VK_FORMAT_R8G8B8A8_SRGB);

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_NEAREST;
    si.minFilter    = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    vkCreateSampler(m_device, &si, nullptr, &s.sampler);

    /* registra no array bindless global */
    _bindless_write(slot_idx, s.view, s.sampler);

    s.in_use = true;
    return slot_idx;
}

/* =========================================================================
 * delete_texture()
 *
 * VMA libera a memória automaticamente.
 * O slot bindless é redirecionado para a textura branca
 * para evitar acesso a recurso já liberado.
 * ====================================================================== */
void RendererVK::delete_texture(unsigned int handle)
{
    if (handle >= (unsigned int)VK_MAX_TEXTURES) return;
    VkTextureSlot &s = m_tex_slots[handle];
    if (!s.in_use) return;

    vkDeviceWaitIdle(m_device);

    /* redireciona o slot para a textura branca — evita acesso a recurso liberado */
    if (handle != m_white_tex && m_white_tex < VK_MAX_TEXTURES) {
        VkTextureSlot &w = m_tex_slots[m_white_tex];
        if (w.in_use) _bindless_write(handle, w.view, w.sampler);
    }

    if (s.sampler) vkDestroySampler  (m_device, s.sampler, nullptr);
    if (s.view)    vkDestroyImageView(m_device, s.view,    nullptr);
    /* VMA libera imagem e memória juntos */
    if (s.image)   vmaDestroyImage(m_vma, s.image, s.alloc);

    s = VkTextureSlot{};
}

/* =========================================================================
 * set_texture()
 *
 * Não dispara flush ao trocar textura.
 * O índice vai dentro de cada vértice; o batch mistura texturas livremente.
 * ====================================================================== */
void RendererVK::set_texture(Engine *e, unsigned int tex_handle)
{
    /* índice vai no vértice; não precisa de flush ao trocar textura */
    m_cur_tex = (tex_handle < (unsigned int)VK_MAX_TEXTURES) ? tex_handle : m_white_tex;
}

/* =========================================================================
 * push_quad()
 *
 * Grava um quad no vertex buffer do frame atual (ring buffer mapeado).
 * - tex_idx vai dentro do vértice: um único draw pode misturar texturas.
 * - SIMD SSE4.1 calcula posição dos 4 vértices em paralelo quando disponível;
 *   fallback escalar automático em CPUs sem SSE4.1.
 * ====================================================================== */
void RendererVK::push_quad(Engine *e, const QuadParams &q)
{
    if (m_batch_count >= VK_MAX_QUADS) _flush_batch();

    const float u0 = q.flip_h ? q.u1 : q.u0;
    const float u1 = q.flip_h ? q.u0 : q.u1;
    const float v0 = q.flip_v ? q.v1 : q.v0;
    const float v1 = q.flip_v ? q.v0 : q.v1;

    const float cx = q.dx + q.dw * 0.5f;
    const float cy = q.dy + q.dh * 0.5f;
    const float hw = q.dw * 0.5f;
    const float hh = q.dh * 0.5f;

    /* cosf/sinf calculados uma vez por quad */
    const float rad = q.rotation * (3.14159265f / 180.f);
    const float cs  = cosf(rad);
    const float sn  = sinf(rad);

    VkQuadVertex *vb = m_vb_ptrs[m_frame_idx] + m_batch_count * 4;

#if VK_USE_SIMD
    /*
     * Layout local dos 4 vértices (antes da rotação):
     *   lx = { -hw,  hw,  hw, -hw }
     *   ly = { -hh, -hh,  hh,  hh }
     *
     * Após rotação:
     *   px[i] = cx + lx[i]*cs - ly[i]*sn
     *   py[i] = cy + lx[i]*sn + ly[i]*cs
     *
     * Calculamos as 4 coordenadas x e as 4 y de uma só vez com SSE.
     */
    const __m128 v_lx  = _mm_set_ps(-hw,  hw,  hw, -hw);  /* ordem inversa: [0]=−hw */
    const __m128 v_ly  = _mm_set_ps(-hh, -hh,  hh,  hh);  /* idem */
    const __m128 v_cs  = _mm_set1_ps(cs);
    const __m128 v_sn  = _mm_set1_ps(sn);
    const __m128 v_cx  = _mm_set1_ps(cx);
    const __m128 v_cy  = _mm_set1_ps(cy);

    /* px = cx + lx*cs - ly*sn */
    const __m128 v_px  = _mm_add_ps(v_cx,
                             _mm_sub_ps(_mm_mul_ps(v_lx, v_cs),
                                        _mm_mul_ps(v_ly, v_sn)));
    /* py = cy + lx*sn + ly*cs */
    const __m128 v_py  = _mm_add_ps(v_cy,
                             _mm_add_ps(_mm_mul_ps(v_lx, v_sn),
                                        _mm_mul_ps(v_ly, v_cs)));

    /* extrai os 4 floats e preenche os vértices */
    float px_arr[4], py_arr[4];
    _mm_storeu_ps(px_arr, v_px);
    _mm_storeu_ps(py_arr, v_py);

    /* _mm_set_ps usa ordem inversa [3..0]; remap corrige */
    const int remap[4] = { 3, 2, 1, 0 };
    const float us[4]  = { u0, u1, u1, u0 };
    const float vs[4]  = { v0, v0, v1, v1 };

    for (int i = 0; i < 4; ++i) {
        const int ri      = remap[i];
        vb[i].x       = px_arr[ri];
        vb[i].y       = py_arr[ri];
        vb[i].u       = us[i];
        vb[i].v       = vs[i];
        vb[i].r       = q.r;
        vb[i].g       = q.g;
        vb[i].b       = q.b;
        vb[i].a       = q.a;
        vb[i].tex_idx = m_cur_tex;
    }

#else  /* fallback escalar */
    const float lx[4] = { -hw,  hw,  hw, -hw };
    const float ly[4] = { -hh, -hh,  hh,  hh };
    const float us[4] = { u0, u1, u1, u0 };
    const float vs[4] = { v0, v0, v1, v1 };

    for (int i = 0; i < 4; ++i) {
        vb[i].x       = cx + lx[i] * cs - ly[i] * sn;
        vb[i].y       = cy + lx[i] * sn + ly[i] * cs;
        vb[i].u       = us[i];
        vb[i].v       = vs[i];
        vb[i].r       = q.r;
        vb[i].g       = q.g;
        vb[i].b       = q.b;
        vb[i].a       = q.a;
        vb[i].tex_idx = m_cur_tex;
    }
#endif

    ++m_batch_count;
}

/* =========================================================================
 * resize() / toggle_fullscreen() / setup_projection()
 * ====================================================================== */
void RendererVK::resize(Engine *e, int new_w, int new_h)
{
    m_win_w = new_w; m_win_h = new_h;
    vkDeviceWaitIdle(m_device);
    _recreate_swapchain();
    setup_projection(e);
}

void RendererVK::toggle_fullscreen(Engine *e)
{
    m_fullscreen = !m_fullscreen;
    SDL_SetWindowFullscreen(m_window, m_fullscreen);
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    resize(e, w, h);
}

void RendererVK::setup_projection(Engine *e)
{
    _build_ortho(0.f, (float)m_win_w, (float)m_win_h, 0.f);
}

/* =========================================================================
 * camera_push() / camera_pop()
 * ====================================================================== */
void RendererVK::camera_push(Engine *e)
{
    if (m_camera_pushed) return;
    m_push_saved    = m_push;
    m_camera_pushed = true;

    const float cx   = e->camera.x;
    const float cy   = e->camera.y;
    const float zoom = e->camera.zoom > 0.f ? e->camera.zoom : 1.f;
    _build_ortho(cx + e->camera.shake_x,
                 cx + e->camera.shake_x + (float)m_win_w / zoom,
                 cy + e->camera.shake_y + (float)m_win_h / zoom,
                 cy + e->camera.shake_y);
}

void RendererVK::camera_pop(Engine *e)
{
    if (!m_camera_pushed) return;
    _flush_batch();
    m_push          = m_push_saved;
    m_camera_pushed = false;
}

/* =========================================================================
 * draw_line_raw() / draw_circle_raw()
 * ====================================================================== */
void RendererVK::draw_line_raw(Engine *e,
                                float x0, float y0, float x1, float y1,
                                float r, float g, float b, float thickness)
{
    set_texture(e, m_white_tex);

    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;

    QuadParams q{};
    q.dx = (x0 + x1) * 0.5f - len * 0.5f;
    q.dy = (y0 + y1) * 0.5f - thickness * 0.5f;
    q.dw = len; q.dh = thickness;
    q.u0 = q.v0 = 0.f; q.u1 = q.v1 = 1.f;
    q.r = r; q.g = g; q.b = b; q.a = 1.f;
    q.rotation = atan2f(dy, dx) * (180.f / 3.14159265f);
    push_quad(e, q);
}

void RendererVK::draw_circle_raw(Engine *e,
                                  float cx, float cy, float radius,
                                  float r, float g, float b, bool filled)
{
    set_texture(e, m_white_tex);
    const int   segs  = 32;
    const float step  = 2.f * 3.14159265f / (float)segs;
    const float thick = filled ? radius : 2.f;

    /* reutiliza o ponto final de cada segmento como início do próximo
     * → 33 cos/sin em vez de 64 */
    float x0 = cx + cosf(0.f) * radius;
    float y0 = cy + sinf(0.f) * radius;

    for (int i = 1; i <= segs; ++i) {
        const float a1 = i * step;
        const float x1 = cx + cosf(a1) * radius;
        const float y1 = cy + sinf(a1) * radius;

        draw_line_raw(e, x0, y0, x1, y1, r, g, b, thick);

        x0 = x1;   /* reutiliza ponto final como início do próximo segmento */
        y0 = y1;
    }
}

/* =========================================================================
 * set_blend_inverted() / set_blend_normal()
 * ====================================================================== */
void RendererVK::set_blend_inverted(Engine *e) { _flush_batch(); m_blend_inv = true; }
void RendererVK::set_blend_normal  (Engine *e) { _flush_batch(); m_blend_inv = false; }

/* =========================================================================
 * set_clear_color()
 * ====================================================================== */
void RendererVK::set_clear_color(float r, float g, float b)
{
    m_clear_r = r; m_clear_g = g; m_clear_b = b;
}

/* =========================================================================
 * poll_events()
 * ====================================================================== */
void RendererVK::poll_events(Engine *e)
{
    /* Rotaciona estado de teclado: cur → prev */
    memcpy(m_keys_prev, m_keys_cur, sizeof(m_keys_cur));

    /* Rotaciona estado de mouse: buttons → buttons_prev
     * engine_mouse_pressed/released usam buttons_prev internamente. */
    memcpy(e->mouse.buttons_prev, e->mouse.buttons, sizeof(e->mouse.buttons));
    e->mouse.scroll = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            e->running = 0;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            resize(e, ev.window.data1, ev.window.data2);
            break;
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode kcode = ev.key.key;
            for (int i = 0; i < ENGINE_MAX_KEYS; ++i)
                if (m_key_codes[i] == kcode) { m_keys_cur[i] = 1; break; }
            if (kcode == SDLK_ESCAPE) e->running = 0;
            break;
        }
        case SDL_EVENT_KEY_UP: {
            const SDL_Keycode kcode = ev.key.key;
            for (int i = 0; i < ENGINE_MAX_KEYS; ++i)
                if (m_key_codes[i] == kcode) { m_keys_cur[i] = 0; break; }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int btn = (ev.button.button == SDL_BUTTON_LEFT)   ? ENGINE_MOUSE_LEFT   :
                      (ev.button.button == SDL_BUTTON_MIDDLE) ? ENGINE_MOUSE_MIDDLE :
                      (ev.button.button == SDL_BUTTON_RIGHT)  ? ENGINE_MOUSE_RIGHT  : -1;
            if (btn >= 0) e->mouse.buttons[btn] = 1;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btn = (ev.button.button == SDL_BUTTON_LEFT)   ? ENGINE_MOUSE_LEFT   :
                      (ev.button.button == SDL_BUTTON_MIDDLE) ? ENGINE_MOUSE_MIDDLE :
                      (ev.button.button == SDL_BUTTON_RIGHT)  ? ENGINE_MOUSE_RIGHT  : -1;
            if (btn >= 0) e->mouse.buttons[btn] = 0;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            e->mouse.x = (int)ev.motion.x; e->mouse.y = (int)ev.motion.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            e->mouse.scroll = (ev.wheel.y > 0) ? 1 : -1;
            break;
        default: break;
        }
    }
}

/* =========================================================================
 * key_register() / key_down() / key_pressed() / key_released()
 *
 * Substituem a API legada (e->key_names / e->keys_down / etc.).
 * O slot é um índice 0..ENGINE_MAX_KEYS-1 escolhido pelo jogo.
 * O nome segue a convenção SDL_GetKeyName (ex: "Left", "Space", "A").
 * ====================================================================== */
void RendererVK::key_register(int slot, const char *sdl_key_name)
{
    if (slot < 0 || slot >= ENGINE_MAX_KEYS) return;
    const SDL_Keycode kc = SDL_GetKeyFromName(sdl_key_name);
    if (kc == SDLK_UNKNOWN)
        fprintf(stderr, "RendererVK::key_register: nome desconhecido '%s'\n", sdl_key_name);
    m_key_codes[slot] = kc;
}

int RendererVK::key_down    (int slot) const { return (slot >= 0 && slot < ENGINE_MAX_KEYS) ?  m_keys_cur[slot]                           : 0; }
int RendererVK::key_pressed (int slot) const { return (slot >= 0 && slot < ENGINE_MAX_KEYS) ?  m_keys_cur[slot] && !m_keys_prev[slot]     : 0; }
int RendererVK::key_released(int slot) const { return (slot >= 0 && slot < ENGINE_MAX_KEYS) ? !m_keys_cur[slot] &&  m_keys_prev[slot]     : 0; }

/* =========================================================================
 * FBO — fbo_create()
 *
 * Imagem de cor GPU-only alocada via VMA.
 * Registrado no array bindless como uma textura normal.
 * ====================================================================== */
FboHandle RendererVK::fbo_create(Engine *e, int w, int h)
{
    int slot_idx = -1;
    for (int i = 0; i < ENGINE_MAX_FBOS; ++i)
        if (!m_fbo_slots[i].in_use) { slot_idx = i; break; }
    if (slot_idx < 0) { fprintf(stderr, "RendererVK: sem slots de FBO.\n"); return ENGINE_FBO_INVALID; }

    VkFboSlot &f = m_fbo_slots[slot_idx];
    f.width = w; f.height = h;

    /* imagem de cor off-screen */
    _vma_create_image((uint32_t)w, (uint32_t)h,
                      VK_FORMAT_R8G8B8A8_SRGB,
                      VK_IMAGE_TILING_OPTIMAL,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      f.color_image, f.color_alloc);

    _transition_image_layout(f.color_image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    f.color_view = _create_image_view(f.color_image, VK_FORMAT_R8G8B8A8_SRGB);

    /* Render pass off-screen */
    VkAttachmentDescription att{};
    att.format         = VK_FORMAT_R8G8B8A8_SRGB;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;
    VkRenderPassCreateInfo rp_ci{};
    rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &att;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &sub;
    vkCreateRenderPass(m_device, &rp_ci, nullptr, &f.render_pass);

    VkFramebufferCreateInfo fb_ci{};
    fb_ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass      = f.render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments    = &f.color_view;
    fb_ci.width           = (uint32_t)w;
    fb_ci.height          = (uint32_t)h;
    fb_ci.layers          = 1;
    vkCreateFramebuffer(m_device, &fb_ci, nullptr, &f.framebuffer);

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_NEAREST;
    si.minFilter    = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_device, &si, nullptr, &f.sampler);

    /* registra como textura normal no array bindless */
    unsigned int tex_slot = _alloc_texture_slot();
    VkTextureSlot &ts = m_tex_slots[tex_slot];
    ts.image   = f.color_image; /* compartilhado — não destruir pelo slot */
    ts.view    = f.color_view;
    ts.sampler = f.sampler;
    ts.width   = (uint32_t)w;
    ts.height  = (uint32_t)h;
    ts.in_use  = true;
    _bindless_write(tex_slot, ts.view, ts.sampler);
    f.tex_handle = tex_slot;

    f.in_use = true;
    return (FboHandle)slot_idx;
}

/* =========================================================================
 * FBO — fbo_destroy()
 *
 * Libera a imagem via VMA.
 * ====================================================================== */
void RendererVK::fbo_destroy(Engine *e, FboHandle fh)
{
    if (fh < 0 || fh >= ENGINE_MAX_FBOS) return;
    VkFboSlot &f = m_fbo_slots[fh];
    if (!f.in_use) return;
    vkDeviceWaitIdle(m_device);

    /* Desregistra o slot de textura sem destruir a imagem (feito abaixo) */
    m_tex_slots[f.tex_handle].in_use  = false;
    m_tex_slots[f.tex_handle].image   = VK_NULL_HANDLE;
    m_tex_slots[f.tex_handle].view    = VK_NULL_HANDLE;
    m_tex_slots[f.tex_handle].sampler = VK_NULL_HANDLE;

    if (f.framebuffer) vkDestroyFramebuffer(m_device, f.framebuffer, nullptr);
    if (f.render_pass) vkDestroyRenderPass (m_device, f.render_pass, nullptr);
    if (f.sampler)     vkDestroySampler    (m_device, f.sampler,     nullptr);
    if (f.color_view)  vkDestroyImageView  (m_device, f.color_view,  nullptr);
    if (f.color_image) vmaDestroyImage(m_vma, f.color_image, f.color_alloc);

    f = VkFboSlot{};
}

/* =========================================================================
 * FBO — fbo_bind() / fbo_unbind() / fbo_texture()
 * ====================================================================== */
void RendererVK::fbo_bind(Engine *e, FboHandle fh)
{
    if (fh < 0 || fh >= ENGINE_MAX_FBOS || !m_fbo_slots[fh].in_use) return;
    _flush_batch();
    _end_render_pass();
    m_active_fbo = &m_fbo_slots[fh];
    _build_ortho(0.f, (float)m_active_fbo->width,
                 (float)m_active_fbo->height, 0.f);
    _begin_render_pass();
}

void RendererVK::fbo_unbind(Engine *e)
{
    if (!m_active_fbo) return;
    _flush_batch();
    _end_render_pass();

    /* Barreira: COLOR_ATTACHMENT → SHADER_READ_ONLY */
    VkCommandBuffer cb = m_cmd_bufs[m_frame_idx];
    VkImageMemoryBarrier barrier{};
    barrier.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image            = m_active_fbo->color_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_active_fbo = nullptr;
    setup_projection(e);
    _begin_render_pass();
}

unsigned int RendererVK::fbo_texture(Engine *e, FboHandle fh)
{
    if (fh < 0 || fh >= ENGINE_MAX_FBOS || !m_fbo_slots[fh].in_use) return 0;
    return m_fbo_slots[fh].tex_handle;
}

/* =========================================================================
 * Shaders customizados
 * ====================================================================== */
ShaderHandle RendererVK::shader_create(Engine *e,
                                        const char *vert_src,
                                        const char *frag_src)
{
    int slot_idx = -1;
    for (int i = 0; i < ENGINE_MAX_SHADERS; ++i)
        if (!m_shader_slots[i].in_use) { slot_idx = i; break; }
    if (slot_idx < 0) return ENGINE_SHADER_INVALID;

    std::vector<uint32_t> vert_spv, frag_spv;
    if (!_compile_glsl(vert_src, true,  vert_spv)) return ENGINE_SHADER_INVALID;
    if (!_compile_glsl(frag_src, false, frag_spv)) return ENGINE_SHADER_INVALID;

    auto make_module = [&](const std::vector<uint32_t> &spv) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv.size() * 4;
        ci.pCode    = spv.data();
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(m_device, &ci, nullptr, &m);
        return m;
    };

    VkShaderModule vm = make_module(vert_spv);
    VkShaderModule fm = make_module(frag_spv);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm; stages[1].pName = "main";

    /* pipeline customizada com mesma configuração fixa do padrão */
    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(VkQuadVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       (uint32_t)offsetof(VkQuadVertex, x)       };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       (uint32_t)offsetof(VkQuadVertex, u)       };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(VkQuadVertex, r)       };
    attrs[3] = { 3, 0, VK_FORMAT_R32_UINT,            (uint32_t)offsetof(VkQuadVertex, tex_idx) };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &bind;
    vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1; vp_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rast.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable         = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1; blend.pAttachments = &blend_att;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2; pi.pStages             = stages;
    pi.pVertexInputState   = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vp_state;
    pi.pRasterizationState = &rast;
    pi.pMultisampleState   = &ms;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dyn;
    pi.layout              = m_pipe_layout;
    pi.renderPass          = m_render_pass;
    pi.subpass             = 0;

    VkPipeline pipe = VK_NULL_HANDLE;
    /* usa pipeline cache para acelerar compilação */
    if (vkCreateGraphicsPipelines(m_device, m_pipeline_cache, 1, &pi,
                                  nullptr, &pipe) != VK_SUCCESS) {
        fprintf(stderr, "RendererVK: shader_create — falha ao criar pipeline.\n");
        vkDestroyShaderModule(m_device, vm, nullptr);
        vkDestroyShaderModule(m_device, fm, nullptr);
        return ENGINE_SHADER_INVALID;
    }

    vkDestroyShaderModule(m_device, vm, nullptr);
    vkDestroyShaderModule(m_device, fm, nullptr);

    m_shader_slots[slot_idx].pipeline = pipe;
    m_shader_slots[slot_idx].in_use   = true;
    return (ShaderHandle)slot_idx;
}

void RendererVK::shader_destroy(Engine *e, ShaderHandle sh)
{
    if (sh < 0 || sh >= ENGINE_MAX_SHADERS || !m_shader_slots[sh].in_use) return;
    vkDeviceWaitIdle(m_device);
    if (m_shader_slots[sh].pipeline)
        vkDestroyPipeline(m_device, m_shader_slots[sh].pipeline, nullptr);
    m_shader_slots[sh] = VkShaderSlot{};
}

void RendererVK::shader_use(Engine *e, ShaderHandle sh)
{
    if (sh < 0 || sh >= ENGINE_MAX_SHADERS || !m_shader_slots[sh].in_use) return;
    _flush_batch();
    m_active_pipeline = m_shader_slots[sh].pipeline;
}

void RendererVK::shader_none(Engine *e) { _flush_batch(); m_active_pipeline = VK_NULL_HANDLE; }

void RendererVK::shader_set_int  (Engine*, ShaderHandle, const char*, int)           {}
void RendererVK::shader_set_float(Engine*, ShaderHandle, const char*, float)         {}
void RendererVK::shader_set_vec2 (Engine*, ShaderHandle, const char*, float, float)  {}
void RendererVK::shader_set_vec4 (Engine*, ShaderHandle, const char*, float, float, float, float) {}

/* =========================================================================
 * ---- HELPERS PRIVADOS ---------------------------------------------------
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * _create_vma() — inicializa o alocador de memória Vulkan (VMA)
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_vma()
{
    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice   = m_phys_dev;
    ai.device           = m_device;
    ai.instance         = m_instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
    VK_CHECK(vmaCreateAllocator(&ai, &m_vma));
    return true;
}

/* -------------------------------------------------------------------------
 * _vma_create_buffer()
 *
 * out_mapped: se não-nulo e VMA_ALLOCATION_CREATE_MAPPED_BIT ativo,
 *             retorna ponteiro mapeado permanente (ring buffer de vértices).
 * ---------------------------------------------------------------------- */
bool RendererVK::_vma_create_buffer(VkDeviceSize size,
                                     VkBufferUsageFlags       usage,
                                     VmaMemoryUsage           vma_usage,
                                     VmaAllocationCreateFlags vma_flags,
                                     VkBuffer      &out_buf,
                                     VmaAllocation &out_alloc,
                                     void         **out_mapped)
{
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size        = size;
    buf_ci.usage       = usage;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = vma_usage;
    alloc_ci.flags = vma_flags;

    VmaAllocationInfo alloc_info{};
    VK_CHECK(vmaCreateBuffer(m_vma, &buf_ci, &alloc_ci,
                             &out_buf, &out_alloc, &alloc_info));

    if (out_mapped) *out_mapped = alloc_info.pMappedData;
    return true;
}

/* -------------------------------------------------------------------------
 * _vma_create_image() — cria imagem Vulkan alocada pelo VMA
 * ---------------------------------------------------------------------- */
bool RendererVK::_vma_create_image(uint32_t w, uint32_t h,
                                    VkFormat format,
                                    VkImageTiling tiling,
                                    VkImageUsageFlags usage,
                                    VmaMemoryUsage vma_usage,
                                    VkImage       &out_img,
                                    VmaAllocation &out_alloc)
{
    VkImageCreateInfo img_ci{};
    img_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType     = VK_IMAGE_TYPE_2D;
    img_ci.format        = format;
    img_ci.extent        = { w, h, 1 };
    img_ci.mipLevels     = 1;
    img_ci.arrayLayers   = 1;
    img_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling        = tiling;
    img_ci.usage         = usage;
    img_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = vma_usage;

    VK_CHECK(vmaCreateImage(m_vma, &img_ci, &alloc_ci,
                            &out_img, &out_alloc, nullptr));
    return true;
}

/* -------------------------------------------------------------------------
 * _create_bindless_layout()
 *
 * Array de VK_MAX_TEXTURES samplers no binding 0.
 * PARTIALLY_BOUND: slots não escritos não causam erro de validação.
 * UPDATE_AFTER_BIND: permite atualizar o set enquanto a GPU o usa.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_bindless_layout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = VK_MAX_TEXTURES; /* array inteiro */
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    /* PARTIALLY_BOUND: slots não escritos são ignorados enquanto o shader
     *   não os acessa — necessário para bindless.
     * UPDATE_AFTER_BIND: permite chamar vkUpdateDescriptorSets enquanto o
     *   set já está em uso pela GPU (upload_texture durante um frame ativo).
     *   Requer descriptorBindingUpdateAfterBind no device e a flag
     *   UPDATE_AFTER_BIND_POOL no pool. */
    VkDescriptorBindingFlagsEXT binding_flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags_info{};
    flags_info.sType         =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    flags_info.bindingCount  = 1;
    flags_info.pBindingFlags = &binding_flags;

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
    layout_ci.pNext        = &flags_info;
    layout_ci.bindingCount = 1;
    layout_ci.pBindings    = &binding;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layout_ci,
                                         nullptr, &m_desc_layout));
    return true;
}

/* -------------------------------------------------------------------------
 * _create_bindless_pool_and_set()
 *
 * Cria o pool e aloca o único descriptor set global (m_bindless_set).
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_bindless_pool_and_set()
{
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = VK_MAX_TEXTURES;

    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
    pool_ci.maxSets       = 1; /* apenas um descriptor set global */
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pool_ci, nullptr, &m_desc_pool));

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = m_desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &m_desc_layout;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &alloc_info, &m_bindless_set));

    return true;
}

/* -------------------------------------------------------------------------
 * _bindless_write() — atualiza um slot do array bindless.
 * A próxima draw que usar esse índice já vê o novo sampler.
 * ---------------------------------------------------------------------- */
void RendererVK::_bindless_write(uint32_t    slot_idx,
                                  VkImageView view,
                                  VkSampler   sampler)
{
    VkDescriptorImageInfo img_info{};
    img_info.sampler     = sampler;
    img_info.imageView   = view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_bindless_set;
    write.dstBinding      = 0;
    write.dstArrayElement = slot_idx; /* atualiza apenas este slot */
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

/* -------------------------------------------------------------------------
 * _create_vertex_ring_buffer()
 *
 * Um vertex buffer por frame em flight, mapeado permanentemente.
 * push_quad() escreve direto via ponteiro — zero overhead de API.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_vertex_ring_buffer()
{
    VkDeviceSize size = sizeof(VkQuadVertex) * VK_MAX_QUADS * 4;
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        void *mapped = nullptr;
        if (!_vma_create_buffer(size,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                VMA_MEMORY_USAGE_AUTO,
                                VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                m_vb[i], m_vb_alloc[i], &mapped))
            return false;
        m_vb_ptrs[i] = static_cast<VkQuadVertex *>(mapped);
    }
    return true;
}

/* -------------------------------------------------------------------------
 * _create_index_buffer()
 *
 * Pré-gera os índices dos quads (0,1,2 / 0,2,3 por quad),
 * copia via staging e mantém buffer GPU-only.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_index_buffer()
{
    const uint32_t n = (uint32_t)VK_MAX_QUADS * 6;
    std::vector<uint32_t> indices(n);
    for (uint32_t i = 0; i < (uint32_t)VK_MAX_QUADS; ++i) {
        uint32_t b = i * 6, v = i * 4;
        indices[b+0] = v+0; indices[b+1] = v+1; indices[b+2] = v+2;
        indices[b+3] = v+0; indices[b+4] = v+2; indices[b+5] = v+3;
    }
    VkDeviceSize size = n * sizeof(uint32_t);

    /* Staging (host-visible, mapped) */
    VkBuffer      stg; VmaAllocation stg_alloc; void *stg_ptr = nullptr;
    _vma_create_buffer(size,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VMA_MEMORY_USAGE_CPU_ONLY,
                       VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       stg, stg_alloc, &stg_ptr);
    memcpy(stg_ptr, indices.data(), (size_t)size);

    /* Device-local (GPU_ONLY) */
    _vma_create_buffer(size,
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VMA_MEMORY_USAGE_GPU_ONLY,
                       0,
                       m_ib, m_ib_alloc);

    VkCommandBuffer cb = _begin_one_time_cmd();
    VkBufferCopy    copy{ 0, 0, size };
    vkCmdCopyBuffer(cb, stg, m_ib, 1, &copy);
    _end_one_time_cmd(cb);

    vmaDestroyBuffer(m_vma, stg, stg_alloc);
    return true;
}

/* -------------------------------------------------------------------------
 * _flush_batch()
 *
 * Grava os draw calls diretamente no primary command buffer (inline).
 * Faz flush do vertex buffer antes do draw para garantir visibilidade
 * da GPU em arquiteturas com memória não-coherent.
 * ---------------------------------------------------------------------- */
void RendererVK::_flush_batch()
{
    if (m_batch_count == 0 || !m_in_frame) return;
    if (!m_in_pass) _begin_render_pass();

    VkCommandBuffer cb = m_cmd_bufs[m_frame_idx];

    VkPipeline pipe = (m_active_pipeline != VK_NULL_HANDLE)
                    ? m_active_pipeline
                    : (m_blend_inv ? m_pipeline_inv : m_pipeline);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    /* envia matriz de projeção */
    vkCmdPushConstants(cb, m_pipe_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(VkPushConstants), &m_push);

    /* bind do array global de texturas */
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipe_layout, 0, 1, &m_bindless_set,
                            0, nullptr);

    /* vertex/index buffers do frame atual */
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &m_vb[m_frame_idx], &offset);
    vkCmdBindIndexBuffer  (cb, m_ib, 0, VK_INDEX_TYPE_UINT32);

    /* viewport/scissor dinâmicos */
    VkExtent2D ext = m_active_fbo
        ? VkExtent2D{ (uint32_t)m_active_fbo->width, (uint32_t)m_active_fbo->height }
        : m_sc_extent;
    VkViewport vp{ 0.f, 0.f, (float)ext.width, (float)ext.height, 0.f, 1.f };
    VkRect2D   sc{ {0,0}, ext };
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor (cb, 0, 1, &sc);

    /* flush da memória: no-op em NVIDIA/AMD, necessário em ARM/Intel */
    vmaFlushAllocation(m_vma, m_vb_alloc[m_frame_idx], 0, VK_WHOLE_SIZE);

    vkCmdDrawIndexed(cb, (uint32_t)(m_batch_count * 6), 1, 0, 0, 0);
    m_batch_count = 0;
}

/* -------------------------------------------------------------------------
 * _create_instance()
 *
 * Negocia a versão da API em runtime:
 *   • Prefere Vulkan 1.2 (descriptor indexing no core).
 *   • Aceita 1.1 ou 1.0 — as extensões VK_EXT_descriptor_indexing e
 *     VK_KHR_swapchain existem desde 1.0, então o renderer funciona
 *     em qualquer loader/ICD que suporte pelo menos Vulkan 1.0.
 *
 * A validation layer é ativada apenas se realmente disponível; em builds
 * de desenvolvimento sem o SDK Vulkan completo isso evita VK_ERROR_LAYER_NOT_PRESENT.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_instance(const char *title)
{
    /* --- Negocia versão do loader ---------------------------------------- */
    uint32_t loader_ver = VK_API_VERSION_1_0;
    /* vkEnumerateInstanceVersion existe desde loader 1.1; em 1.0 não existe.
     * Carregamos via vkGetInstanceProcAddr para evitar o warning -Waddress
     * de comparar um símbolo de função com NULL. */
    {
        auto fn = (PFN_vkEnumerateInstanceVersion)
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
        if (fn) fn(&loader_ver);
    }

    uint32_t api_ver;
    if      (loader_ver >= VK_API_VERSION_1_2) api_ver = VK_API_VERSION_1_2;
    else if (loader_ver >= VK_API_VERSION_1_1) api_ver = VK_API_VERSION_1_1;
    else                                        api_ver = VK_API_VERSION_1_0;

    fprintf(stdout, "RendererVK: loader Vulkan %u.%u — usando API %u.%u\n",
            VK_VERSION_MAJOR(loader_ver), VK_VERSION_MINOR(loader_ver),
            VK_VERSION_MAJOR(api_ver),    VK_VERSION_MINOR(api_ver));

    VkApplicationInfo app{};
    app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = title;
    app.apiVersion       = api_ver;

    /* --- Extensões de instância ------------------------------------------ */
    /* SDL3: SDL_Vulkan_GetInstanceExtensions não recebe mais a janela;
     * retorna um array de strings gerenciado pelo SDL (não liberar). */
    uint32_t sdl_ext_count = 0;
    const char * const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    std::vector<const char *> exts(sdl_exts, sdl_exts + sdl_ext_count);

    /* --- Validation layer: verifica disponibilidade antes de ativar ------- */
    bool validation_available = false;
    if (k_enable_validation) {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        for (const auto &lp : available_layers) {
            if (strcmp(lp.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                validation_available = true;
                break;
            }
        }
        if (!validation_available)
            fprintf(stderr, "RendererVK: VK_LAYER_KHRONOS_validation indisponível — validation desativada.\n");
    }

    /* VK_EXT_debug_utils só faz sentido com a layer ativa */
    if (validation_available)
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (validation_available) { ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers; }

    VkResult result = vkCreateInstance(&ci, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "RendererVK: vkCreateInstance falhou (%d).\n"
                        "  Verifique se o driver Vulkan está instalado:\n"
                        "  pacman -S vulkan-icd-loader  (+ vulkan-radeon / vulkan-intel / nvidia-utils)\n",
                (int)result);
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        return false;
    }
    if (validation_available) {
        VkDebugUtilsMessengerCreateInfoEXT dm{};
        dm.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dm.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dm.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dm.pfnUserCallback = _vk_debug_cb;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, &dm, nullptr, &m_debug_msg);
    }
    return true;
}

/* -------------------------------------------------------------------------
 * _pick_physical_device()
 * ---------------------------------------------------------------------- */
bool RendererVK::_pick_physical_device()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (!count) { fprintf(stderr, "RendererVK: nenhuma GPU.\n"); return false; }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

    for (auto &pd : devs) {
        uint32_t qf = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qf);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf, qfs.data());
        for (uint32_t i = 0; i < qf; ++i) {
            VkBool32 ok = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_surface, &ok);
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && ok) {
                m_phys_dev = pd; m_gfx_family = i; return true;
            }
        }
    }
    fprintf(stderr, "RendererVK: nenhuma fila gráfica+present.\n");
    return false;
}

/* -------------------------------------------------------------------------
 * _create_device()
 *
 * Habilita shaderSampledImageArrayDynamicIndexing (necessário para
 * indexar o array de samplers bindless no fragment shader).
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_device()
{
    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_gfx_family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    /* extensões necessárias para swapchain e bindless textures */
    const char *dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };

    /* permite indexar o array de samplers dinamicamente no shader */
    VkPhysicalDeviceFeatures feats{};
    feats.shaderSampledImageArrayDynamicIndexing = VK_TRUE;

    /* Habilita features necessárias para bindless textures:
     * - NonUniformIndexing: permite indexar o array de samplers com índice
     *   diferente por fragment (nonuniformEXT no GLSL).
     * - PartiallyBound: slots do array que ainda não foram escritos não
     *   causam erro de validação enquanto o shader não os acessa.
     * - RuntimeDescriptorArray: tamanho do array pode ser definido no shader.
     * Nota: UPDATE_AFTER_BIND não é campo direto da struct EXT; é controlado
     * pelas flags do pool e do layout (UPDATE_AFTER_BIND_POOL_BIT). */
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexing_feats{};
    indexing_feats.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    indexing_feats.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    indexing_feats.descriptorBindingPartiallyBound           = VK_TRUE;
    indexing_feats.runtimeDescriptorArray                    = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = &indexing_feats;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = dev_exts;
    ci.pEnabledFeatures        = &feats;

    VK_CHECK(vkCreateDevice(m_phys_dev, &ci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_gfx_family, 0, &m_gfx_queue);
    return true;
}

/* -------------------------------------------------------------------------
 * _create_swapchain()
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_swapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_phys_dev, m_surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_phys_dev, m_surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_phys_dev, m_surface, &fmt_count, fmts.data());
    m_sc_format = fmts[0].format;
    VkColorSpaceKHR cs = fmts[0].colorSpace;
    for (auto &f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { m_sc_format = f.format; cs = f.colorSpace; break; }

    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_phys_dev, m_surface, &pm_count, nullptr);
    std::vector<VkPresentModeKHR> pms(pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_phys_dev, m_surface, &pm_count, pms.data());
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsync)
        for (auto p : pms) if (p == VK_PRESENT_MODE_IMMEDIATE_KHR) { pm = p; break; }

    m_sc_extent = caps.currentExtent;
    if (m_sc_extent.width == UINT32_MAX) {
        m_sc_extent.width  = (uint32_t)std::max(
            (int)caps.minImageExtent.width,
            std::min((int)caps.maxImageExtent.width, m_win_w));
        m_sc_extent.height = (uint32_t)std::max(
            (int)caps.minImageExtent.height,
            std::min((int)caps.maxImageExtent.height, m_win_h));
    }

    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sc_ci{};
    sc_ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc_ci.surface          = m_surface;
    sc_ci.minImageCount    = img_count;
    sc_ci.imageFormat      = m_sc_format;
    sc_ci.imageColorSpace  = cs;
    sc_ci.imageExtent      = m_sc_extent;
    sc_ci.imageArrayLayers = 1;
    sc_ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc_ci.preTransform     = caps.currentTransform;
    sc_ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc_ci.presentMode      = pm;
    sc_ci.clipped          = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(m_device, &sc_ci, nullptr, &m_swapchain));

    uint32_t real = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &real, nullptr);
    m_sc_images.resize(real);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &real, m_sc_images.data());
    m_sc_views.resize(real);
    for (uint32_t i = 0; i < real; ++i)
        m_sc_views[i] = _create_image_view(m_sc_images[i], m_sc_format);
    return true;
}

void RendererVK::_destroy_swapchain()
{
    for (auto fb : m_sc_fbs)   vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto iv : m_sc_views) vkDestroyImageView  (m_device, iv, nullptr);
    m_sc_fbs.clear(); m_sc_views.clear(); m_sc_images.clear();
    if (m_swapchain) { vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

bool RendererVK::_recreate_swapchain()
{
    vkDeviceWaitIdle(m_device);
    _destroy_swapchain();
    return _create_swapchain() && _create_framebuffers();
}

/* -------------------------------------------------------------------------
 * _create_render_pass()
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_render_pass()
{
    VkAttachmentDescription att{};
    att.format         = m_sc_format;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = &att;
    ci.subpassCount    = 1; ci.pSubpasses   = &sub;
    ci.dependencyCount = 1; ci.pDependencies= &dep;
    VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_render_pass));
    return true;
}

/* -------------------------------------------------------------------------
 * _create_framebuffers()
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_framebuffers()
{
    m_sc_fbs.resize(m_sc_views.size());
    for (size_t i = 0; i < m_sc_views.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_render_pass;
        ci.attachmentCount = 1; ci.pAttachments = &m_sc_views[i];
        ci.width           = m_sc_extent.width;
        ci.height          = m_sc_extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(m_device, &ci, nullptr, &m_sc_fbs[i]));
    }
    return true;
}

/* -------------------------------------------------------------------------
 * _create_pipeline_layout()
 *
 * Push constant: matriz de projeção (64 bytes).
 * tex_idx é atributo de vértice, não push constant.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_pipeline_layout()
{
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(VkPushConstants);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &m_desc_layout;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pc;
    VK_CHECK(vkCreatePipelineLayout(m_device, &ci, nullptr, &m_pipe_layout));
    return true;
}

/* -------------------------------------------------------------------------
 * _create_pipeline()
 *
 * Vertex input inclui tex_idx (uint, location 3) para bindless.
 * Shaders compilados em runtime via glslang, ou via shaders_embedded.hpp
 * se gerado por compile_shaders.sh.
 * ---------------------------------------------------------------------- */

/* GLSL do vertex shader padrão — quad 2D com suporte bindless.
 * tex_idx passado como flat uint por vértice: sem interpolação, sem perda. */
static const char *k_vert_glsl = R"GLSL(
#version 450
layout(push_constant) uniform PC { mat4 proj; } pc;
layout(location=0) in vec2  inPos;
layout(location=1) in vec2  inUV;
layout(location=2) in vec4  inColor;
layout(location=3) in uint  inTexIdx;
layout(location=0) out vec2      outUV;
layout(location=1) out vec4      outColor;
layout(location=2) out flat uint outTexIdx;
void main() {
    gl_Position = pc.proj * vec4(inPos, 0.0, 1.0);
    outUV     = inUV;
    outColor  = inColor;
    outTexIdx = inTexIdx;
}
)GLSL";

/* GLSL do fragment shader padrão — array bindless de samplers.
 * GL_EXT_nonuniform_qualifier requer SPIR-V 1.3 (Vulkan 1.1).
 * _compile_glsl usa EShTargetVulkan_1_1 / EShTargetSpv_1_3 para suportar. */
static const char *k_frag_glsl = R"GLSL(
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set=0, binding=0) uniform sampler2D textures[128];
layout(location=0) in vec2      inUV;
layout(location=1) in vec4      inColor;
layout(location=2) in flat uint inTexIdx;
layout(location=0) out vec4 fragColor;
void main() {
    fragColor = texture(textures[nonuniformEXT(inTexIdx)], inUV) * inColor;
}
)GLSL";

bool RendererVK::_create_pipeline(bool inverted, VkPipeline *out)
{
    /* Carrega shaders:
     * 1ª opção — SPIR-V pré-compilado embutido (shaders_embedded.hpp).
     *            Não requer glslang instalado no sistema do usuário.
     * 2ª opção — compila em runtime via glslang (fallback de desenvolvimento). */
    std::vector<uint32_t> vert_spv, frag_spv;

#if VK_HAS_EMBEDDED_SHADERS
    vert_spv.assign(k_vert_spv_embedded,
                    k_vert_spv_embedded + k_vert_spv_embedded_size / sizeof(uint32_t));
    frag_spv.assign(k_frag_spv_embedded,
                    k_frag_spv_embedded + k_frag_spv_embedded_size / sizeof(uint32_t));
    fprintf(stdout, "RendererVK: usando shaders SPIR-V pré-compilados.\n");
#else
    fprintf(stdout, "RendererVK: compilando shaders via glslang (fallback).\n");
    if (!_compile_glsl(k_vert_glsl, true,  vert_spv)) {
        fprintf(stderr, "RendererVK: falha ao compilar vertex shader padrão.\n"
                        "  Dica: execute ./compile_shaders.sh para gerar shaders_embedded.hpp\n");
        return false;
    }
    if (!_compile_glsl(k_frag_glsl, false, frag_spv)) {
        fprintf(stderr, "RendererVK: falha ao compilar fragment shader padrão.\n");
        return false;
    }
#endif

    auto make_module = [&](const std::vector<uint32_t> &spv) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv.size() * sizeof(uint32_t);
        ci.pCode    = spv.data();
        VkShaderModule m = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device, &ci, nullptr, &m) != VK_SUCCESS)
            fprintf(stderr, "RendererVK: vkCreateShaderModule falhou.\n");
        return m;
    };
    VkShaderModule vm = make_module(vert_spv);
    VkShaderModule fm = make_module(frag_spv);
    if (!vm || !fm) {
        if (vm) vkDestroyShaderModule(m_device, vm, nullptr);
        if (fm) vkDestroyShaderModule(m_device, fm, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm; stages[1].pName = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(VkQuadVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    /* 4 atributos: posição, UV, cor RGBA, índice da textura */
    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       (uint32_t)offsetof(VkQuadVertex, x)       };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       (uint32_t)offsetof(VkQuadVertex, u)       };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(VkQuadVertex, r)       };
    attrs[3] = { 3, 0, VK_FORMAT_R32_UINT,            (uint32_t)offsetof(VkQuadVertex, tex_idx) };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &bind;
    vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rast.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable    = VK_TRUE;
    if (!inverted) {
        blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;
    } else {
        blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1; blend.pAttachments = &blend_att;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2; pi.pStages             = stages;
    pi.pVertexInputState   = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vp;
    pi.pRasterizationState = &rast;
    pi.pMultisampleState   = &ms;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dyn;
    pi.layout              = m_pipe_layout;
    pi.renderPass          = m_render_pass;
    pi.subpass             = 0;
    /* pipeline cache: compilação instantânea se já em disco */
    VK_CHECK(vkCreateGraphicsPipelines(m_device, m_pipeline_cache, 1, &pi, nullptr, out));

    vkDestroyShaderModule(m_device, vm, nullptr);
    vkDestroyShaderModule(m_device, fm, nullptr);
    return true;
}

/* -------------------------------------------------------------------------
 * _create_pipeline_cache() / _save_pipeline_cache()
 *
 * Persiste o pipeline compilado em disco. Na primeira execução fica vazio;
 * nas seguintes o driver carrega do cache e cria a pipeline quase que
 * instantaneamente.
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_pipeline_cache()
{
    std::vector<uint8_t> initial_data;

    /* Tenta carregar o cache de uma execução anterior */
    if (FILE *f = fopen(k_pipeline_cache_file, "rb")) {
        fseek(f, 0, SEEK_END);
        size_t sz = (size_t)ftell(f);
        rewind(f);
        initial_data.resize(sz);
        if (fread(initial_data.data(), 1, sz, f) != sz)
            initial_data.clear(); /* leitura parcial — descarta */
        fclose(f);
        if (!initial_data.empty())
            fprintf(stdout, "RendererVK: pipeline cache carregado (%zu bytes).\n", sz);
    }

    VkPipelineCacheCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = initial_data.size();
    ci.pInitialData    = initial_data.empty() ? nullptr : initial_data.data();
    VK_CHECK(vkCreatePipelineCache(m_device, &ci, nullptr, &m_pipeline_cache));
    return true;
}

void RendererVK::_save_pipeline_cache()
{
    if (!m_pipeline_cache || !m_device) return;

    size_t sz = 0;
    if (vkGetPipelineCacheData(m_device, m_pipeline_cache, &sz, nullptr) != VK_SUCCESS
        || sz == 0)
        return;

    std::vector<uint8_t> data(sz);
    if (vkGetPipelineCacheData(m_device, m_pipeline_cache, &sz, data.data()) != VK_SUCCESS)
        return;

    if (FILE *f = fopen(k_pipeline_cache_file, "wb")) {
        fwrite(data.data(), 1, sz, f);
        fclose(f);
        fprintf(stdout, "RendererVK: pipeline cache salvo (%zu bytes).\n", sz);
    }
}

/* -------------------------------------------------------------------------
 * _create_command_pool() / _create_sync_objects() / _create_white_texture()
 * ---------------------------------------------------------------------- */
bool RendererVK::_create_command_pool()
{
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_gfx_family;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_cmd_pool));
    return true;
}

bool RendererVK::_create_sync_objects()
{
    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fen_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fen_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(m_device, &sem_ci, nullptr, &m_img_available[i]));
        VK_CHECK(vkCreateSemaphore(m_device, &sem_ci, nullptr, &m_render_done[i]));
        VK_CHECK(vkCreateFence    (m_device, &fen_ci, nullptr, &m_in_flight[i]));
    }
    return true;
}

bool RendererVK::_create_white_texture()
{
    const unsigned char w[4] = { 255, 255, 255, 255 };
    m_white_tex = upload_texture(w, 1, 1);
    return m_white_tex < (unsigned int)VK_MAX_TEXTURES;
}

/* -------------------------------------------------------------------------
 * _create_image_view()
 * ---------------------------------------------------------------------- */
VkImageView RendererVK::_create_image_view(VkImage img, VkFormat format)
{
    VkImageViewCreateInfo ci{};
    ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image                           = img;
    ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ci.format                          = format;
    ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.subresourceRange.baseMipLevel   = 0;
    ci.subresourceRange.levelCount     = 1;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount     = 1;
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(m_device, &ci, nullptr, &view);
    return view;
}

/* -------------------------------------------------------------------------
 * _begin_one_time_cmd() / _end_one_time_cmd()
 * ---------------------------------------------------------------------- */
VkCommandBuffer RendererVK::_begin_one_time_cmd()
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = m_cmd_pool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(m_device, &ai, &cb);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

void RendererVK::_end_one_time_cmd(VkCommandBuffer cb)
{
    vkEndCommandBuffer(cb);

    /* fence individual: não trava a fila inteira, espera só este transfer */
    VkFenceCreateInfo fen_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_device, &fen_ci, nullptr, &fence);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(m_gfx_queue, 1, &si, fence);

    /* Aguarda somente este fence, não toda a fila */
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);

    vkFreeCommandBuffers(m_device, m_cmd_pool, 1, &cb);
}

/* -------------------------------------------------------------------------
 * _transition_image_layout()
 * ---------------------------------------------------------------------- */
void RendererVK::_transition_image_layout(VkImage img,
                                           VkImageLayout old_l,
                                           VkImageLayout new_l)
{
    VkCommandBuffer cb = _begin_one_time_cmd();
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = old_l;
    b.newLayout           = new_l;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkPipelineStageFlags src = 0, dst = 0;
    if (old_l == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_l == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_l == VK_IMAGE_LAYOUT_UNDEFINED &&
               new_l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (old_l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               new_l == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        src = dst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    _end_one_time_cmd(cb);
}

/* -------------------------------------------------------------------------
 * _copy_buffer_to_image()
 * ---------------------------------------------------------------------- */
void RendererVK::_copy_buffer_to_image(VkBuffer buf, VkImage img,
                                        uint32_t w, uint32_t h)
{
    VkCommandBuffer cb = _begin_one_time_cmd();
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cb, buf, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    _end_one_time_cmd(cb);
}

/* -------------------------------------------------------------------------
 * _alloc_texture_slot()
 * ---------------------------------------------------------------------- */
unsigned int RendererVK::_alloc_texture_slot()
{
    for (unsigned int i = 0; i < (unsigned int)VK_MAX_TEXTURES; ++i)
        if (!m_tex_slots[i].in_use) return i;
    return (unsigned int)VK_MAX_TEXTURES;
}

/* -------------------------------------------------------------------------
 * _begin_render_pass() / _end_render_pass()
 * ---------------------------------------------------------------------- */
void RendererVK::_begin_render_pass()
{
    if (m_in_pass) return;
    VkCommandBuffer cb = m_cmd_bufs[m_frame_idx];

    VkClearValue cv{};
    cv.color = {{ m_clear_r, m_clear_g, m_clear_b, 1.f }};

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &cv;
    if (m_active_fbo) {
        rp.renderPass    = m_active_fbo->render_pass;
        rp.framebuffer   = m_active_fbo->framebuffer;
        rp.renderArea.extent = { (uint32_t)m_active_fbo->width,
                                 (uint32_t)m_active_fbo->height };
    } else {
        rp.renderPass    = m_render_pass;
        rp.framebuffer   = m_sc_fbs[m_image_idx];
        rp.renderArea.extent = m_sc_extent;
    }
    /* draw calls inline no primary command buffer */
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = (float)rp.renderArea.extent.width;
    vp.height   = (float)rp.renderArea.extent.height;
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{ {0,0}, rp.renderArea.extent };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    m_in_pass = true;
}

void RendererVK::_end_render_pass()
{
    if (!m_in_pass) return;
    vkCmdEndRenderPass(m_cmd_bufs[m_frame_idx]);
    m_in_pass = false;
}

/* -------------------------------------------------------------------------
 * _build_ortho()
 * ---------------------------------------------------------------------- */
void RendererVK::_build_ortho(float l, float r, float b, float t)
{
    _ortho(l, r, b, t, -1.f, 1.f, m_push.proj);
}

/* -------------------------------------------------------------------------
 * _compile_glsl() — compila GLSL para SPIR-V via glslang.
 *
 * InitializeProcess/FinalizeProcess são chamados apenas uma vez:
 * aqui na primeira compilação, e em destroy() no shutdown.
 * ---------------------------------------------------------------------- */
bool RendererVK::_compile_glsl(const char *src, bool is_vert,
                                std::vector<uint32_t> &out_spirv)
{
    /* inicializa glslang na primeira compilação */
    if (!m_glslang_init) {
        glslang::InitializeProcess();
        m_glslang_init = true;
    }

    EShLanguage stage = is_vert ? EShLangVertex : EShLangFragment;
    glslang::TShader shader(stage);
    const char *srcs[] = { src };
    shader.setStrings(srcs, 1);

    /* Vulkan 1.1 / SPIR-V 1.3: necessário para GL_EXT_nonuniform_qualifier
     * (indexação não-uniforme de arrays de samplers — bindless textures).
     * O loader do usuário é 1.4, então 1.1 é sempre suportado. */
    shader.setEnvInput (glslang::EShSourceGlsl, stage,
                        glslang::EShClientVulkan, 110);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,    glslang::EShTargetSpv_1_3);

    /* GetDefaultResources() retorna os limites completos do Vulkan;
     * elimina a lista manual que estava incompleta (causava o erro
     * 'limitations' com nonuniformEXT). */
    const TBuiltInResource *res = GetDefaultResources();

    if (!shader.parse(res, 110, false, EShMsgDefault)) {
        fprintf(stderr, "glslang: %s\n", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(EShMsgDefault)) {
        fprintf(stderr, "glslang link: %s\n", prog.getInfoLog());
        return false;
    }
    glslang::GlslangToSpv(*prog.getIntermediate(stage), out_spirv);
    /* FinalizeProcess() é chamado em destroy() */
    return !out_spirv.empty();
}

#endif /* ENGINE_BACKEND_VK */