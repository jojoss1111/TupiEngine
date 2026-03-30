-- engineffi.lua
--
-- Declarações FFI da engine para uso com LuaJIT.
--
-- Este arquivo espelha Engine.hpp: qualquer mudança na struct Engine ou nos
-- protótipos das funções deve ser replicada aqui manualmente.
--
-- Uso:
--   local ffi = require("engineffi")          -- retorna o objeto ffi já configurado
--   local e   = ffi.new("Engine")
--   ffi.C.engine_init(e, 320, 180, "Meu Jogo", 3)
--
-- Observações importantes:
--   • Os tipos de ponteiro (void*, int*) são opacos do ponto de vista do Lua.
--     Nunca acesse renderer_impl diretamente; use as funções da API pública.
--   • Os tamanhos dos buffers opacos (_ma_engine, sound) devem coincidir
--     exatamente com os equivalentes em C; verifique ao atualizar miniaudio.
--   • AudioHandle é apenas int; ENGINE_AUDIO_INVALID == -1.

local ffi = require("ffi")

ffi.cdef[[
/* Capacidades máximas — devem coincidir com os #defines em Engine.hpp */
static const int ENGINE_MAX_SPRITES    = 64;
static const int ENGINE_MAX_OBJECTS    = 256;
static const int ENGINE_MAX_KEYS       = 256;
static const int ENGINE_MAX_PARTICLES  = 512;
static const int ENGINE_MAX_ANIMATORS  = 32;
static const int ENGINE_MAX_LAYERS     = 8;

/* IDs de backend — usados em Engine.backend_id */
static const int ENGINE_BACKEND_ID_GL   = 0;
static const int ENGINE_BACKEND_ID_DX11 = 1;
static const int ENGINE_BACKEND_ID_VK   = 2;

/* Índices de botão do mouse para MouseState.buttons[] */
static const int ENGINE_MOUSE_LEFT   = 0;
static const int ENGINE_MOUSE_MIDDLE = 1;
static const int ENGINE_MOUSE_RIGHT  = 2;

/* Metadados de uma textura carregada; texture é handle opaco (nunca acesse diretamente) */
typedef struct {
    unsigned int texture;
    int          width;
    int          height;
    int          loaded;
} SpriteData;

/* Região de colisão relativa ao objeto; enabled=0 usa bounding-box do sprite */
typedef struct {
    int offset_x, offset_y;
    int width,    height;
    int enabled;
} Hitbox;

/* Entidade básica do mundo 2D.
 * use_tile=1 → renderiza região (tile_x*tile_w, tile_y*tile_h) do sprite.
 * alpha: 0.0 invisível, 1.0 opaco.  layer/z_order controlam ordem de draw. */
typedef struct {
    int           x, y;
    int           sprite_id;
    unsigned long color;
    int           width, height;
    int           active;
    int           tile_x, tile_y;
    int           tile_w, tile_h;
    int           use_tile;
    int           flip_h, flip_v;
    float         scale_x, scale_y;
    float         rotation;
    float         alpha;
    int           layer;
    int           z_order;
    Hitbox        hitbox;
} GameObject;

/* Câmera 2D; shake_x/y são atualizados por engine_update() — não escreva manualmente */
typedef struct {
    float x, y;
    float zoom;
    float shake_x, shake_y;
    float shake_intensity;
    float shake_duration;
    float shake_timer;
} Camera;

/* Partícula individual; cor e tamanho interpolam de (r0..a0) para (r1..a1) ao longo de life */
typedef struct {
    float x, y;
    float vx, vy;
    float ax, ay;
    float life, life_max;
    float size_start, size_end;
    float r0, g0, b0, a0;
    float r1, g1, b1, a1;
    int   sprite_id;
    int   active;
} Particle;

/* Emissor; rate>0 emite automaticamente em engine_particles_update(); _acc é interno */
typedef struct {
    float x, y;
    float vx_min, vx_max;
    float vy_min, vy_max;
    float ax, ay;
    float life_min, life_max;
    float size_start, size_end;
    float r0, g0, b0, a0;
    float r1, g1, b1, a1;
    int   sprite_id;
    int   rate;
    int   max_particles;
    int   active;
    float _acc;
} ParticleEmitter;

/* Sequência de frames; atualiza objects[object_id].sprite_id automaticamente */
typedef struct {
    int   sprite_ids[32];
    int   frame_count;
    float fps;
    float frame_dur;
    int   loop;
    int   current_frame;
    float timer;
    int   finished;
    int   object_id;
} Animator;

/* Estado do mouse no frame atual; buttons_prev permite detectar pressed/released */
typedef struct {
    int x, y;
    int buttons[3];
    int buttons_prev[3];
    int scroll;
} MouseState;

/* Mapa keycode → keysym pré-calculado; preenchido em init(), não modifique */
typedef struct {
    unsigned long map[256];
} KeysymCache;

/* =================================================================
 * Tipos do subsistema de áudio
 *
 * AudioHandle é um índice inteiro no pool de trilhas; ENGINE_AUDIO_INVALID
 * (-1) indica falha ou handle inválido.
 * ================================================================= */

static const int ENGINE_AUDIO_MAX_TRACKS = 32;
static const int ENGINE_AUDIO_INVALID    = -1;

typedef int AudioHandle;

typedef enum {
    AUDIO_TRACK_FREE     = 0,
    AUDIO_TRACK_PLAYING  = 1,
    AUDIO_TRACK_PAUSED   = 2,
    AUDIO_TRACK_FINISHED = 3
} AudioTrackStatus;

typedef struct {
    char             sound[2048];   /* ma_sound opaco — tamanho deve bater com sizeof(ma_sound) */
    AudioTrackStatus status;
    int              in_use;
    float            volume;
    float            pitch;
    int              loop;
    int              resume_after;
} AudioTrack;

typedef struct {
    char        _ma_engine[4096];  /* ma_engine opaco — tamanho deve bater com sizeof(ma_engine) */
    AudioTrack  tracks[32];
    char        _mutex[64];         /* pthread_mutex_t opaco; tamanho seguro para Linux x64 */
    int         ready;
} AudioContext;

/* =================================================================
 * Engine — contexto principal do jogo
 *
 * Esta definição é o espelho de Engine em Engine.hpp.
 * A ordem e os tipos de todos os campos devem ser idênticos.
 *
 * renderer_impl é void* — o Lua enxerga o ponteiro mas nunca o
 * dereference; toda operação passa pelas funções engine_*().
 *
 * Se Engine.hpp for modificado, atualize este bloco imediatamente.
 * ================================================================= */
typedef struct {
    /* Backend gráfico opaco */
    void *renderer_impl;   /* IRenderer* — não acesse diretamente do Lua */
    int   backend_id;

    /* Dimensões da janela e da área de render virtual */
    int   win_w, win_h;
    int   depth;
    int   scale;
    int   render_w, render_h;

    /* Textura 1×1 branca usada como tint neutro em primitivas sólidas */
    unsigned int white_tex;

    /* Recursos do mundo */
    SpriteData  sprites[64];
    int         sprite_count;

    GameObject  objects[256];
    int         object_count;

    /* Input de teclado e mouse */
    int        *keys;
    int        *keys_prev;
    KeysymCache ksym_cache;

    MouseState  mouse;

    /* Sistema de partículas */
    Particle        particles[512];
    int             particle_count;
    int             particle_next;
    ParticleEmitter emitters[16];
    int             emitter_count;

    /* Animadores de sprite */
    Animator    animators[32];
    int         animator_count;

    /* Câmera 2D */
    Camera      camera;
    int         camera_enabled;

    /* Efeito de fade de tela */
    float       fade_alpha;
    float       fade_target;
    float       fade_speed;
    int         fade_r, fade_g, fade_b;

    /* Temporização */
    double      time_elapsed;
    double      delta_time;

    /* Estado geral do loop */
    int         running;
    unsigned long bg_color;

    int         fullscreen;
    int         saved_win_w, saved_win_h;
    int         saved_render_w, saved_render_h;

    /* Subsistema de áudio */
    AudioContext audio;
} Engine;

/* =================================================================
 * API pública — nomes idênticos aos declarados em Engine.hpp
 * ================================================================= */
int    engine_init(Engine *e, int width, int height, const char *title, int scale);
void   engine_destroy(Engine *e);
void   engine_set_background(Engine *e, int r, int g, int b);

void   engine_poll_events(Engine *e);
void   engine_clear(Engine *e);
void   engine_draw(Engine *e);
void   engine_flush(Engine *e);
void   engine_present(Engine *e);
void   engine_cap_fps(Engine *e, int fps_target);
void   engine_update(Engine *e, float dt);

double engine_get_time(Engine *e);
double engine_get_delta(Engine *e);

int    engine_load_sprite(Engine *e, const char *path);
int    engine_load_sprite_region(Engine *e, const char *path, int x, int y, int w, int h);

int    engine_add_object(Engine *e, int x, int y, int sprite_id, int width, int height, int r, int g, int b);
int    engine_add_tile_object(Engine *e, int x, int y, int sprite_id, int tile_x, int tile_y, int tile_w, int tile_h);
void   engine_move_object(Engine *e, int oid, int dx, int dy);
void   engine_set_object_pos(Engine *e, int oid, int x, int y);
void   engine_set_object_sprite(Engine *e, int oid, int sprite_id);
void   engine_get_object_pos(Engine *e, int oid, int *out_x, int *out_y);
void   engine_set_object_tile(Engine *e, int oid, int tile_x, int tile_y);
void   engine_set_object_flip(Engine *e, int oid, int flip_h, int flip_v);
void   engine_set_object_scale(Engine *e, int oid, float sx, float sy);
void   engine_set_object_rotation(Engine *e, int oid, float degrees);
void   engine_set_object_alpha(Engine *e, int oid, float alpha);
void   engine_set_object_layer(Engine *e, int oid, int layer, int z_order);
void   engine_set_object_hitbox(Engine *e, int oid, int offset_x, int offset_y, int width, int height);
void   engine_remove_object(Engine *e, int oid);

int    engine_check_collision(Engine *e, int oid1, int oid2);
int    engine_check_collision_rect(Engine *e, int oid, int rx, int ry, int rw, int rh);
int    engine_check_collision_point(Engine *e, int oid, int px, int py);

void   engine_camera_set(Engine *e, float x, float y);
void   engine_camera_move(Engine *e, float dx, float dy);
void   engine_camera_zoom(Engine *e, float zoom);
void   engine_camera_follow(Engine *e, int oid, float lerp_speed);
void   engine_camera_shake(Engine *e, float intensity, float duration);
void   engine_camera_enable(Engine *e, int enabled);
void   engine_world_to_screen(Engine *e, float wx, float wy, float *sx, float *sy);
void   engine_screen_to_world(Engine *e, float sx, float sy, float *wx, float *wy);

int    engine_emitter_add(Engine *e, ParticleEmitter *cfg);
void   engine_emitter_set_pos(Engine *e, int eid, float x, float y);
void   engine_emitter_burst(Engine *e, int eid, int count);
void   engine_emitter_remove(Engine *e, int eid);
void   engine_particles_update(Engine *e, float dt);
void   engine_particles_draw(Engine *e);

int    engine_animator_add(Engine *e, int *sprite_ids, int frame_count, float fps, int loop, int object_id);
void   engine_animator_play(Engine *e, int aid);
void   engine_animator_stop(Engine *e, int aid);
void   engine_animator_reset(Engine *e, int aid);
int    engine_animator_finished(Engine *e, int aid);
void   engine_animators_update(Engine *e, float dt);

void   engine_fade_to(Engine *e, float target_alpha, float speed, int r, int g, int b);
void   engine_fade_draw(Engine *e);
int    engine_fade_done(Engine *e);

void   engine_draw_rect(Engine *e, int x, int y, int w, int h, int r, int g, int b);
void   engine_draw_rect_outline(Engine *e, int x, int y, int w, int h, int r, int g, int b, int thickness);
void   engine_draw_line(Engine *e, int x0, int y0, int x1, int y1, int r, int g, int b, int thickness);
void   engine_draw_circle(Engine *e, int cx, int cy, int radius, int r, int g, int b, int filled);
void   engine_draw_overlay(Engine *e, int x, int y, int w, int h, int r, int g, int b, float alpha);

void   engine_draw_rain(Engine *e, int screen_w, int screen_h, int frame, const int *gotas_x, const int *gotas_y, int n_gotas, int gota_w, int gota_h);
void   engine_draw_night(Engine *e, int screen_w, int screen_h, float intensidade, int offset);

void   engine_draw_tilemap(Engine *e, const int *tilemap, int tile_rows, int tile_cols, int sprite_id, int tile_w, int tile_h, int offset_x, int offset_y);
void   engine_draw_sprite_part(Engine *e, int sprite_id, int x, int y, int src_x, int src_y, int src_w, int src_h);
void   engine_draw_sprite_part_ex(Engine *e, int sprite_id, int x, int y, int src_x, int src_y, int src_w, int src_h, float scale_x, float scale_y, float rotation, float alpha, int flip_h, int flip_v);
void   engine_draw_sprite_part_inverted(Engine *e, int sprite_id, int x, int y, int src_x, int src_y, int src_w, int src_h);

void   engine_draw_text(Engine *e, int x, int y, const char *text, int font_sid, int font_w, int font_h, int chars_per_row, int ascii_offset, int line_spacing);
void   engine_draw_box(Engine *e, int x, int y, int box_w, int box_h, int box_sid, int tile_w, int tile_h);
void   engine_draw_text_box(Engine *e, int x, int y, int box_w, int box_h, const char *title, const char *content, int box_sid, int box_tw, int box_th, int font_sid, int font_w, int font_h, int chars_per_row, int ascii_offset, int line_spacing);

int    engine_key_down(Engine *e, const char *key);
int    engine_key_pressed(Engine *e, const char *key);
int    engine_key_released(Engine *e, const char *key);
int    engine_mouse_down(Engine *e, int button);
int    engine_mouse_pressed(Engine *e, int button);
int    engine_mouse_released(Engine *e, int button);
void   engine_mouse_pos(Engine *e, int *out_x, int *out_y);
int    engine_mouse_scroll(Engine *e);

void   engine_toggle_fullscreen(Engine *e);

int          engine_audio_init   (Engine *e);
void         engine_audio_update (Engine *e);
void         engine_audio_destroy(Engine *e);
AudioHandle  engine_audio_play   (Engine *e, const char *file, int loop, float volume, float pitch, int resume_after);
void         engine_audio_pause  (Engine *e, AudioHandle h);
void         engine_audio_resume (Engine *e, AudioHandle h);
void         engine_audio_stop   (Engine *e, AudioHandle h);
void         engine_audio_volume (Engine *e, AudioHandle h, float volume);
void         engine_audio_pitch  (Engine *e, AudioHandle h, float pitch);
int          engine_audio_done   (Engine *e, AudioHandle h);
]]

return ffi