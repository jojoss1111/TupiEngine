// RendererVK.hpp
//Backend Vulkan 2D para a engine.


#pragma once
#ifndef RENDERER_VK_HPP
#define RENDERER_VK_HPP

#ifdef ENGINE_BACKEND_VK

#include "../Engine.hpp"
#include "IRenderer.hpp"

#include <vulkan/vulkan.h>

/* VMA — single-header; VMA_IMPLEMENTATION definido apenas em RendererVK.cpp */
#include "vk_mem_alloc.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vector>
#include <cstdint>
#include <cstring>

/* =========================================================================
 * Limites internos
 * ====================================================================== */
static constexpr int VK_MAX_QUADS            = 8192;
static constexpr int VK_MAX_FRAMES_IN_FLIGHT = 2;
static constexpr int VK_MAX_TEXTURES         = 128; /* tamanho do array bindless */

/* =========================================================================
 * VkQuadVertex
 *
 * tex_idx: índice no array bindless de samplers.
 *          Permite que um único draw call misture quads de diferentes
 *          texturas sem nenhum rebind de descriptor set.
 * ====================================================================== */
struct VkQuadVertex {
    float    x, y;        /* posição pixel virtual */
    float    u, v;        /* UV 0..1               */
    float    r, g, b, a;  /* cor/tint              */
    uint32_t tex_idx;     /* índice no array bindless */
};

/* =========================================================================
 * VkPushConstants
 *
 * proj[16]: matriz ortho 4x4 column-major (64 bytes).
 * Enviada uma vez por render pass (câmera/resize); tex_idx foi movido
 * para dentro do vértice, eliminando pushes extras por textura.
 * ====================================================================== */
struct VkPushConstants {
    float proj[16];
};

/* =========================================================================
 * VkTextureSlot — textura gerenciada pelo VMA
 *
 * VmaAllocation substitui VkDeviceMemory.
 * Não há mais VkDescriptorSet por textura; a textura ocupa um slot no
 * array bindless global (m_bindless_set).
 * ====================================================================== */
struct VkTextureSlot {
    VkImage       image   = VK_NULL_HANDLE;
    VmaAllocation alloc   = VK_NULL_HANDLE;
    VkImageView   view    = VK_NULL_HANDLE;
    VkSampler     sampler = VK_NULL_HANDLE;
    uint32_t      width   = 0;
    uint32_t      height  = 0;
    bool          in_use  = false;
};

/* =========================================================================
 * VkFboSlot — framebuffer off-screen alocado via VMA
 * ====================================================================== */
struct VkFboSlot {
    VkImage       color_image = VK_NULL_HANDLE;
    VmaAllocation color_alloc = VK_NULL_HANDLE;
    VkImageView   color_view  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass  render_pass = VK_NULL_HANDLE;
    VkSampler     sampler     = VK_NULL_HANDLE;
    unsigned int  tex_handle  = 0; /* índice em m_tex_slots[] e no array bindless */
    int           width       = 0;
    int           height      = 0;
    bool          in_use      = false;
};

/* =========================================================================
 * VkShaderSlot — pipeline customizada
 * ====================================================================== */
struct VkShaderSlot {
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool       in_use   = false;
};

/* =========================================================================
 * RendererVK
 * ====================================================================== */
class RendererVK : public IRenderer {
public:
    RendererVK()  = default;
    ~RendererVK() override = default;

    bool init   (Engine *e, int win_w, int win_h,
                 const char *title, int scale) override;
    void destroy(Engine *e) override;

    void clear  (Engine *e) override;
    void flush  (Engine *e) override;
    void present(Engine *e) override;
    void set_vsync(bool enable) override;

    unsigned int upload_texture(const unsigned char *rgba,
                                unsigned int w, unsigned int h) override;
    void delete_texture(unsigned int handle) override;

    void set_texture(Engine *e, unsigned int tex_handle) override;
    void push_quad  (Engine *e, const QuadParams &q)     override;

    void resize          (Engine *e, int new_w, int new_h) override;
    void toggle_fullscreen(Engine *e)                      override;
    void setup_projection (Engine *e)                      override;

    void camera_push(Engine *e) override;
    void camera_pop (Engine *e) override;

    void draw_line_raw  (Engine *e,
                         float x0, float y0, float x1, float y1,
                         float r, float g, float b, float thickness) override;
    void draw_circle_raw(Engine *e,
                         float cx, float cy, float radius,
                         float r, float g, float b, bool filled) override;

    void set_blend_inverted(Engine *e) override;
    void set_blend_normal  (Engine *e) override;

    void set_clear_color(float r, float g, float b) override;

    void poll_events(Engine *e) override;

    /* --- Input de teclado (VK gerencia internamente, sem depender de Engine) --- */
    void key_register (int slot, const char *sdl_key_name);
    int  key_down     (int slot) const;
    int  key_pressed  (int slot) const;
    int  key_released (int slot) const;

    FboHandle    fbo_create (Engine *e, int w, int h) override;
    void         fbo_destroy(Engine *e, FboHandle fh) override;
    void         fbo_bind   (Engine *e, FboHandle fh) override;
    void         fbo_unbind (Engine *e)               override;
    unsigned int fbo_texture(Engine *e, FboHandle fh) override;

    ShaderHandle shader_create (Engine *e, const char *vert_src,
                                const char *frag_src) override;
    void shader_destroy(Engine *e, ShaderHandle sh) override;
    void shader_use    (Engine *e, ShaderHandle sh) override;
    void shader_none   (Engine *e)                  override;
    void shader_set_int  (Engine *e, ShaderHandle sh,
                          const char *name, int   v) override;
    void shader_set_float(Engine *e, ShaderHandle sh,
                          const char *name, float v) override;
    void shader_set_vec2 (Engine *e, ShaderHandle sh,
                          const char *name, float x, float y) override;
    void shader_set_vec4 (Engine *e, ShaderHandle sh,
                          const char *name,
                          float x, float y, float z, float w) override;

private:
    /* --- SDL3 ---------------------------------------------------------- */
    SDL_Window *m_window     = nullptr;
    int         m_win_w      = 0;
    int         m_win_h      = 0;
    bool        m_fullscreen = false;
    bool        m_vsync      = true;

    /* --- Dispositivo ---------------------------------------------------- */
    VkInstance               m_instance   = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debug_msg  = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface    = VK_NULL_HANDLE;
    VkPhysicalDevice         m_phys_dev   = VK_NULL_HANDLE;
    VkDevice                 m_device     = VK_NULL_HANDLE;
    uint32_t                 m_gfx_family = UINT32_MAX;
    VkQueue                  m_gfx_queue  = VK_NULL_HANDLE;

    /* --- [OTM 1] VMA --------------------------------------------------- */
    VmaAllocator m_vma = VK_NULL_HANDLE;

    /* --- Swapchain ------------------------------------------------------ */
    VkSwapchainKHR             m_swapchain = VK_NULL_HANDLE;
    VkFormat                   m_sc_format = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_sc_extent = {0, 0};
    std::vector<VkImage>       m_sc_images;
    std::vector<VkImageView>   m_sc_views;
    std::vector<VkFramebuffer> m_sc_fbs;

    /* --- Render pass ---------------------------------------------------- */
    VkRenderPass m_render_pass = VK_NULL_HANDLE;

    /* --- Pipeline ------------------------------------------------------- */
    VkPipelineLayout m_pipe_layout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline      = VK_NULL_HANDLE;
    VkPipeline       m_pipeline_inv  = VK_NULL_HANDLE;

    /* --- [OTM 4] Pipeline cache — persiste SPIR-V compilado em disco --- */
    VkPipelineCache  m_pipeline_cache = VK_NULL_HANDLE;

    /* --- [OTM 2] Descriptor bindless ----------------------------------- */
    VkDescriptorSetLayout m_desc_layout  = VK_NULL_HANDLE;
    VkDescriptorPool      m_desc_pool    = VK_NULL_HANDLE;
    VkDescriptorSet       m_bindless_set = VK_NULL_HANDLE; /* único set global */

    /* --- [OTM 3] Ring buffer de vertex buffers ------------------------- */
    VkBuffer      m_vb     [VK_MAX_FRAMES_IN_FLIGHT] = {};
    VmaAllocation m_vb_alloc[VK_MAX_FRAMES_IN_FLIGHT] = {};
    VkQuadVertex *m_vb_ptrs[VK_MAX_FRAMES_IN_FLIGHT] = {}; /* mapeado permanente */

    /* --- Index buffer --------------------------------------------------- */
    VkBuffer      m_ib      = VK_NULL_HANDLE;
    VmaAllocation m_ib_alloc = VK_NULL_HANDLE;

    /* --- Command pool e buffers ---------------------------------------- */
    VkCommandPool   m_cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd_bufs[VK_MAX_FRAMES_IN_FLIGHT] = {};

    /* --- Sincronização -------------------------------------------------- */
    VkSemaphore m_img_available[VK_MAX_FRAMES_IN_FLIGHT] = {};
    VkSemaphore m_render_done  [VK_MAX_FRAMES_IN_FLIGHT] = {};
    VkFence     m_in_flight    [VK_MAX_FRAMES_IN_FLIGHT] = {};
    uint32_t    m_frame_idx = 0;
    uint32_t    m_image_idx = 0;

    /* --- Estado do batch ----------------------------------------------- */
    int      m_batch_count = 0;
    uint32_t m_cur_tex     = 0;     /* índice bindless da textura ativa */
    bool     m_blend_inv   = false;
    bool     m_in_frame    = false;
    bool     m_in_pass     = false;

    /* --- [OTM 8] glslang inicializado apenas uma vez ------------------- */
    bool     m_glslang_init = false;

    /* --- Projeção e câmera --------------------------------------------- */
    VkPushConstants m_push         = {};
    VkPushConstants m_push_saved   = {};
    bool            m_camera_pushed = false;

    /* --- Cor de fundo --------------------------------------------------- */
    float m_clear_r = 0.f, m_clear_g = 0.f, m_clear_b = 0.f;

    /* --- Estado de teclado (gerenciado internamente pelo renderer) ------ */
    /* Segue o mesmo padrão do RendererGL: keys_cur/keys_prev por keycode. */
    uint8_t     m_keys_cur [ENGINE_MAX_KEYS] = {};  /* estado atual         */
    uint8_t     m_keys_prev[ENGINE_MAX_KEYS] = {};  /* estado frame anterior*/
    SDL_Keycode m_key_codes[ENGINE_MAX_KEYS] = {};  /* SDL_Keycode mapeado  */

    /* --- Textura branca 1×1 -------------------------------------------- */
    unsigned int m_white_tex = 0;

    /* --- Pools de slots ------------------------------------------------- */
    VkTextureSlot m_tex_slots   [VK_MAX_TEXTURES]    = {};
    VkFboSlot     m_fbo_slots   [ENGINE_MAX_FBOS]    = {};
    VkShaderSlot  m_shader_slots[ENGINE_MAX_SHADERS] = {};

    /* --- Estado atual --------------------------------------------------- */
    VkFboSlot  *m_active_fbo      = nullptr;
    VkPipeline  m_active_pipeline = VK_NULL_HANDLE;

    /* ===================================================================
     * Helpers privados
     * ================================================================= */

    bool _create_instance(const char *title);
    bool _pick_physical_device();
    bool _create_device();
    bool _create_vma();                      /* [OTM 1] */
    bool _create_swapchain();
    void _destroy_swapchain();
    bool _recreate_swapchain();
    bool _create_render_pass();
    bool _create_framebuffers();
    bool _create_bindless_layout();          /* [OTM 2] */
    bool _create_bindless_pool_and_set();    /* [OTM 2] */
    bool _create_pipeline_layout();
    bool _create_pipeline(bool inverted, VkPipeline *out);
    bool _create_pipeline_cache();          /* [OTM 4] */
    void _save_pipeline_cache();            /* [OTM 4] */
    bool _create_vertex_ring_buffer();       /* [OTM 3] */
    bool _create_index_buffer();
    bool _create_command_pool();
    bool _create_sync_objects();
    bool _create_white_texture();

    /* [OTM 1] — VMA wrappers */
    bool _vma_create_buffer(VkDeviceSize size,
                            VkBufferUsageFlags  usage,
                            VmaMemoryUsage      vma_usage,
                            VmaAllocationCreateFlags vma_flags,
                            VkBuffer     &out_buf,
                            VmaAllocation &out_alloc,
                            void **out_mapped = nullptr);

    bool _vma_create_image(uint32_t w, uint32_t h,
                           VkFormat format,
                           VkImageTiling tiling,
                           VkImageUsageFlags usage,
                           VmaMemoryUsage vma_usage,
                           VkImage       &out_img,
                           VmaAllocation &out_alloc);

    /* [OTM 2] — atualiza o slot no array bindless */
    void _bindless_write(uint32_t slot_idx,
                         VkImageView view,
                         VkSampler   sampler);

    /* Utilitários gerais */
    VkImageView     _create_image_view(VkImage img, VkFormat format);
    VkCommandBuffer _begin_one_time_cmd();
    void            _end_one_time_cmd(VkCommandBuffer cb);
    void _transition_image_layout(VkImage img,
                                  VkImageLayout old_layout,
                                  VkImageLayout new_layout);
    void _copy_buffer_to_image(VkBuffer buf, VkImage img,
                               uint32_t w, uint32_t h);

    unsigned int _alloc_texture_slot();

    void _begin_render_pass();
    void _end_render_pass();
    void _build_ortho(float l, float r, float b, float t);
    void _flush_batch();
    bool _compile_glsl(const char *src, bool is_vert,
                       std::vector<uint32_t> &out_spirv);

    static const uint32_t s_vert_spv[];
    static const uint32_t s_frag_spv[];
    static const size_t   s_vert_spv_size;
    static const size_t   s_frag_spv_size;
};

#endif /* ENGINE_BACKEND_VK */
#endif /* RENDERER_VK_HPP */