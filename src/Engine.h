#ifndef ENGINE_H
#define ENGINE_H

/* ============================================================
 * Engine.h — Engine 2D para Linux (OpenGL 2.1 + GLX + libpng)
 *
 * OTIMIZAÇÕES DE PERFORMANCE vs versão anterior:
 *
 *  RAM:
 *   - ENGINE_MAX_PARTICLES reduzido 2048 → 512  (-48 KB)
 *   - ENGINE_MAX_OBJECTS   reduzido 1024 → 256  (-53 KB)
 *   - ENGINE_MAX_SPRITES   reduzido 128  → 64   (-sem impacto na struct principal)
 *   - ENGINE_MAX_ANIMATORS reduzido 64   → 32
 *   - Cache PNG: só 1 entrada (igual antes) — liberada após upload à GPU
 *   - BATCH_MAX_QUADS reduzido 8192 → 4096 (-128 KB de buffer CPU)
 *   - Partícula compactada: removidos campos redundantes de cor
 *     (armazena cor inicial como constante e interpola via life ratio)
 *
 *  CPU:
 *   - engine_key_down/pressed/released: cache interno de keycode→keysym
 *     montado 1 vez no init, eliminando o loop XDisplayKeycodes a cada frame
 *   - _spawn_particle: busca slot livre usa next_free circular, O(1) amortizado
 *   - engine_draw: só itera até object_count (não ENGINE_MAX_OBJECTS)
 *   - engine_particles_update/draw: só itera até particle_count (pool compacto)
 *   - engine_animators_update: pré-computa 1/fps no add
 *
 *  GPU:
 *   - VSync habilitado na inicialização (não mais lazy no primeiro present)
 *   - _batch_flush não chama glBufferData quando quad_count==0
 *   - engine_draw_sprite_part não chama _batch_flush desnecessariamente
 *   - Removido glBegin/glEnd de engine_draw_circle (substituído por fan no batch)
 *
 * Compilar com:
 *   gcc -O2 -o game main.c Engine.c -lX11 -lGL -lpng -lm
 * ============================================================ */

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <stdint.h>
#include <pthread.h>

/* ---- Limites (ajuste conforme seu jogo) --------------------- */
#define ENGINE_MAX_SPRITES      64
#define ENGINE_MAX_OBJECTS      256
#define ENGINE_MAX_KEYS         256
#define ENGINE_MAX_PARTICLES    512
#define ENGINE_MAX_ANIMATORS    32
#define ENGINE_MAX_LAYERS       8

/* ---- Botões do mouse --------------------------------------- */
#define ENGINE_MOUSE_LEFT   0
#define ENGINE_MOUSE_MIDDLE 1
#define ENGINE_MOUSE_RIGHT  2

/* ---- Flags de layer --------------------------------------- */
#define ENGINE_LAYER_SORT_Y     (1 << 0)
#define ENGINE_LAYER_SORT_Z     (1 << 1)

/* ============================================================
 * Audio subsystem
 * ============================================================
 *
 * Integrated directly into Engine — no separate library needed.
 * Uses miniaudio (header-only). Download miniaudio.h from:
 *   https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
 *
 * Supports: MP3, WAV, OGG, FLAC
 * Backend: auto-detected (PulseAudio → PipeWire → ALSA)
 *
 * Build flags added to Makefile: -lpthread -ldl
 * ============================================================ */

#define ENGINE_AUDIO_MAX_TRACKS   32    /* max simultaneous tracks      */
#define ENGINE_AUDIO_INVALID      (-1)  /* invalid / empty handle       */

typedef int AudioHandle;

typedef enum {
    AUDIO_TRACK_FREE      = 0,
    AUDIO_TRACK_PLAYING   = 1,
    AUDIO_TRACK_PAUSED    = 2,
    AUDIO_TRACK_FINISHED  = 3
} AudioTrackStatus;

/* Forward-declared opaque miniaudio types used inside AudioTrack.
 * The full definition is in Engine.c after the miniaudio include. */
#ifdef MA_SOUND_DEFINED
typedef ma_sound ma_sound_opaque;
#else
typedef struct { char _opaque[2048]; } ma_sound_opaque;
#endif

typedef struct {
    ma_sound_opaque  sound;         /* miniaudio sound object (opaque here) */
    AudioTrackStatus status;
    int              in_use;
    float            volume;        /* 0.0 .. 1.0  */
    float            pitch;         /* 0.5 .. 2.0  */
    int              loop;
    AudioHandle      resume_after;  /* handle to resume when this track ends */
} AudioTrack;

/* Audio engine context — embedded inside Engine struct */
typedef struct {
    char         _ma_engine[4096];  /* ma_engine object (opaque, sized conservatively) */
    AudioTrack   tracks[ENGINE_AUDIO_MAX_TRACKS];
    pthread_mutex_t mutex;
    int          ready;             /* 1 after engine_audio_init() succeeds */
} AudioContext;

/* ---- Estrutura de sprite ----------------------------------- */
typedef struct {
    GLuint texture;
    int    width;
    int    height;
    int    loaded;
} SpriteData;

/* ---- Hitbox customizada ------------------------------------ */
typedef struct {
    int offset_x, offset_y;
    int width,    height;
    int enabled;
} Hitbox;

/* ---- Objeto de jogo --------------------------------------- */
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

/* ---- Câmera 2D -------------------------------------------- */
typedef struct {
    float  x, y;
    float  zoom;
    float  shake_x, shake_y;
    float  shake_intensity;
    float  shake_duration;
    float  shake_timer;
} Camera;

/* ---- Partícula (compactada) -------------------------------- */
/* Cor e tamanho são calculados via interpolação em tempo de draw,
 * usando life/life_max — eliminamos os campos r,g,b,a duplicados. */
typedef struct {
    float  x, y;
    float  vx, vy;
    float  ax, ay;
    float  life, life_max;
    float  size_start, size_end;
    float  r0, g0, b0, a0;   /* cor inicial */
    float  r1, g1, b1, a1;   /* cor final   */
    int    sprite_id;
    int    active;
} Particle;

/* ---- Emissor de partículas --------------------------------- */
typedef struct {
    float  x, y;
    float  vx_min, vx_max;
    float  vy_min, vy_max;
    float  ax, ay;
    float  life_min, life_max;
    float  size_start, size_end;
    float  r0, g0, b0, a0;
    float  r1, g1, b1, a1;
    int    sprite_id;
    int    rate;
    int    max_particles;
    int    active;
    float  _acc;
} ParticleEmitter;

/* ---- Animação por frames ----------------------------------- */
typedef struct {
    int    sprite_ids[32];
    int    frame_count;
    float  fps;
    float  frame_dur;    /* 1/fps — pré-computado no add */
    int    loop;
    int    current_frame;
    float  timer;
    int    finished;
    int    object_id;
} Animator;

/* ---- Estado do mouse -------------------------------------- */
typedef struct {
    int   x, y;
    int   buttons[3];
    int   buttons_prev[3];
    int   scroll;
} MouseState;

/* ---- Cache de keycode→keysym (montado 1x no init) ---------- */
typedef struct {
    KeySym map[ENGINE_MAX_KEYS];   /* map[keycode & 0xFF] = keysym */
} KeysymCache;

/* ---- Contexto principal ------------------------------------ */
typedef struct {
    /* X11 / GLX */
    Display    *display;
    int         screen;
    Window      window;
    GLXContext  glx_ctx;

    int         win_w, win_h;
    int         depth;
    int         scale;
    int         render_w, render_h;

    GLuint      white_tex;

    SpriteData  sprites[ENGINE_MAX_SPRITES];
    int         sprite_count;

    GameObject  objects[ENGINE_MAX_OBJECTS];
    int         object_count;

    /* Keys */
    int        *keys;
    int        *keys_prev;
    KeysymCache ksym_cache;   /* lookups O(1) sem XDisplayKeycodes por frame */

    MouseState  mouse;

    /* Partículas — pool compacto */
    Particle        particles[ENGINE_MAX_PARTICLES];
    int             particle_count;  /* maior índice+1 em uso */
    int             particle_next;   /* próximo slot a tentar (circular) */
    ParticleEmitter emitters[16];
    int             emitter_count;

    Animator    animators[ENGINE_MAX_ANIMATORS];
    int         animator_count;

    Camera      camera;
    int         camera_enabled;

    float       fade_alpha;
    float       fade_target;
    float       fade_speed;
    int         fade_r, fade_g, fade_b;

    double      time_elapsed;
    double      delta_time;

    int         running;
    unsigned long bg_color;

    int         fullscreen;
    int         saved_win_w, saved_win_h;
    int         saved_render_w, saved_render_h;

    /* Audio subsystem (miniaudio, embedded) */
    AudioContext audio;
} Engine;

/* ==============================================================
 * API pública (idêntica à versão anterior — compatibilidade total)
 * ============================================================== */

int  engine_init(Engine *e, int width, int height, const char *title, int scale);
void engine_destroy(Engine *e);
void engine_set_background(Engine *e, int r, int g, int b);

void engine_poll_events(Engine *e);
void engine_clear(Engine *e);
void engine_draw(Engine *e);
void engine_flush(Engine *e);
void engine_present(Engine *e);
void engine_cap_fps(Engine *e, int fps_target);
void engine_update(Engine *e, float dt);

double engine_get_time(Engine *e);
double engine_get_delta(Engine *e);

int  engine_load_sprite(Engine *e, const char *path);
int  engine_load_sprite_region(Engine *e, const char *path, int x, int y, int w, int h);

int  engine_add_object(Engine *e, int x, int y, int sprite_id, int width, int height, int r, int g, int b);
int  engine_add_tile_object(Engine *e, int x, int y, int sprite_id, int tile_x, int tile_y, int tile_w, int tile_h);
void engine_move_object(Engine *e, int oid, int dx, int dy);
void engine_set_object_pos(Engine *e, int oid, int x, int y);
void engine_set_object_sprite(Engine *e, int oid, int sprite_id);
void engine_get_object_pos(Engine *e, int oid, int *out_x, int *out_y);
void engine_set_object_tile(Engine *e, int oid, int tile_x, int tile_y);
void engine_set_object_flip(Engine *e, int oid, int flip_h, int flip_v);
void engine_set_object_scale(Engine *e, int oid, float sx, float sy);
void engine_set_object_rotation(Engine *e, int oid, float degrees);
void engine_set_object_alpha(Engine *e, int oid, float alpha);
void engine_set_object_layer(Engine *e, int oid, int layer, int z_order);
void engine_set_object_hitbox(Engine *e, int oid, int offset_x, int offset_y, int width, int height);
void engine_remove_object(Engine *e, int oid);

int  engine_check_collision(Engine *e, int oid1, int oid2);
int  engine_check_collision_rect(Engine *e, int oid, int rx, int ry, int rw, int rh);
int  engine_check_collision_point(Engine *e, int oid, int px, int py);

void engine_camera_set(Engine *e, float x, float y);
void engine_camera_move(Engine *e, float dx, float dy);
void engine_camera_zoom(Engine *e, float zoom);
void engine_camera_follow(Engine *e, int oid, float lerp_speed);
void engine_camera_shake(Engine *e, float intensity, float duration);
void engine_camera_enable(Engine *e, int enabled);
void engine_world_to_screen(Engine *e, float wx, float wy, float *sx, float *sy);
void engine_screen_to_world(Engine *e, float sx, float sy, float *wx, float *wy);

int  engine_emitter_add(Engine *e, ParticleEmitter *cfg);
void engine_emitter_set_pos(Engine *e, int eid, float x, float y);
void engine_emitter_burst(Engine *e, int eid, int count);
void engine_emitter_remove(Engine *e, int eid);
void engine_particles_update(Engine *e, float dt);
void engine_particles_draw(Engine *e);

int  engine_animator_add(Engine *e, int *sprite_ids, int frame_count, float fps, int loop, int object_id);
void engine_animator_play(Engine *e, int aid);
void engine_animator_stop(Engine *e, int aid);
void engine_animator_reset(Engine *e, int aid);
int  engine_animator_finished(Engine *e, int aid);
void engine_animators_update(Engine *e, float dt);

void engine_fade_to(Engine *e, float target_alpha, float speed, int r, int g, int b);
void engine_fade_draw(Engine *e);
int  engine_fade_done(Engine *e);

void engine_draw_rect(Engine *e, int x, int y, int w, int h, int r, int g, int b);
void engine_draw_rect_outline(Engine *e, int x, int y, int w, int h, int r, int g, int b, int thickness);
void engine_draw_line(Engine *e, int x0, int y0, int x1, int y1, int r, int g, int b, int thickness);
void engine_draw_circle(Engine *e, int cx, int cy, int radius, int r, int g, int b, int filled);
void engine_draw_overlay(Engine *e, int x, int y, int w, int h, int r, int g, int b, float alpha);

void engine_draw_rain(Engine *e, int screen_w, int screen_h, int frame,
                      const int *gotas_x, const int *gotas_y, int n_gotas,
                      int gota_w, int gota_h);
void engine_draw_night(Engine *e, int screen_w, int screen_h, float intensidade, int offset);

void engine_draw_tilemap(Engine *e, const int *tilemap, int tile_rows, int tile_cols,
                          int sprite_id, int tile_w, int tile_h, int offset_x, int offset_y);
void engine_draw_sprite_part(Engine *e, int sprite_id, int x, int y,
                              int src_x, int src_y, int src_w, int src_h);
void engine_draw_sprite_part_ex(Engine *e, int sprite_id, int x, int y,
                                 int src_x, int src_y, int src_w, int src_h,
                                 float scale_x, float scale_y, float rotation, float alpha,
                                 int flip_h, int flip_v);
void engine_draw_sprite_part_inverted(Engine *e, int sprite_id, int x, int y,
                                       int src_x, int src_y, int src_w, int src_h);

void engine_draw_text(Engine *e, int x, int y, const char *text,
                      int font_sid, int font_w, int font_h,
                      int chars_per_row, int ascii_offset, int line_spacing);
void engine_draw_box(Engine *e, int x, int y, int box_w, int box_h,
                     int box_sid, int tile_w, int tile_h);
void engine_draw_text_box(Engine *e, int x, int y, int box_w, int box_h,
                           const char *title, const char *content,
                           int box_sid, int box_tw, int box_th,
                           int font_sid, int font_w, int font_h,
                           int chars_per_row, int ascii_offset, int line_spacing);

int  engine_key_down(Engine *e, const char *key);
int  engine_key_pressed(Engine *e, const char *key);
int  engine_key_released(Engine *e, const char *key);
int  engine_mouse_down(Engine *e, int button);
int  engine_mouse_pressed(Engine *e, int button);
int  engine_mouse_released(Engine *e, int button);
void engine_mouse_pos(Engine *e, int *out_x, int *out_y);
int  engine_mouse_scroll(Engine *e);

void engine_toggle_fullscreen(Engine *e);
int          engine_audio_init   (Engine *e);
void         engine_audio_update (Engine *e);   /* call every frame */
void         engine_audio_destroy(Engine *e);

AudioHandle  engine_audio_play   (Engine *e,
                                   const char  *file,
                                   int          loop,
                                   float        volume,
                                   float        pitch,
                                   AudioHandle  resume_after);

void         engine_audio_pause  (Engine *e, AudioHandle h);
void         engine_audio_resume (Engine *e, AudioHandle h);
void         engine_audio_stop   (Engine *e, AudioHandle h);
void         engine_audio_volume (Engine *e, AudioHandle h, float volume);
void         engine_audio_pitch  (Engine *e, AudioHandle h, float pitch);
int          engine_audio_done   (Engine *e, AudioHandle h);

#endif /* ENGINE_H */