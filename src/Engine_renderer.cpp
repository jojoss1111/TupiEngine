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
/* =============================================================================
 * Backend OpenGL 2.1 + X11
 *
 * Implementação movida para RendererGL.cpp / RendererGL.hpp.
 * ============================================================================= */
#ifdef ENGINE_BACKEND_GL
#  include "Renderizador/RendererGL.hpp"
#endif


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

/* Forward declarations — definidas em Engine_events.cpp */
void _sg_insert_object(Engine *e, int oid);
void _sg_remove_object(Engine *e, int oid);

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


/* =============================================================================
 * Nota: Colisão AABB, Spatial Grid e Input (teclado/mouse) foram
 * movidos para Engine_Events.cpp
 * ============================================================================= */

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

} /* extern "C" */