//Engine.hpp — Cabeçalho principal da engine 2D.
/*
 * Define todas as estruturas de dados (POD/C-compatible), constantes,
 * a struct Engine que concentra o estado global do jogo, e a API pública
 * exposta via extern "C".
 *
 * A interface IRenderer e QuadParams são definidas em Renderizador/IRenderer.hpp,
 * incluído abaixo via #ifdef __cplusplus.  Isso elimina a duplicação que
 * causava conflitos de redefinição quando RendererDX11.cpp incluía ambos
 * os headers.
 *
 * O arquivo permanece compilável como C puro (sem IRenderer/QuadParams)
 * graças aos guards #ifdef __cplusplus.  Isso mantém compatibilidade total
 * com o FFI LuaJIT, que enxerga apenas o lado C.
 *
 * Seleção de backend em tempo de compilação:
 *   ENGINE_BACKEND_GL   → OpenGL 2.1 + X11    (padrão; define os headers X11/GLX)
 *   ENGINE_BACKEND_DX11 → Direct3D 11          (Windows)
 *   ENGINE_BACKEND_VK   → Vulkan               (futuro)
 *
 * Se nenhuma flag for definida, ENGINE_BACKEND_GL é assumido automaticamente.
 *
 * Flags de include necessárias ao compilar:
 *   -I<src_dir>          (onde Engine.hpp reside)
 *   -I<src_dir>/render   (onde IRenderer.hpp reside)
 */

#ifndef ENGINE_HPP
#define ENGINE_HPP

#if !defined(ENGINE_BACKEND_GL) && \
    !defined(ENGINE_BACKEND_DX11) && \
    !defined(ENGINE_BACKEND_VK)
#  define ENGINE_BACKEND_GL
#endif

#ifdef ENGINE_BACKEND_GL
#  include <X11/Xlib.h>
#  include <X11/keysym.h>
#  include <GL/gl.h>
#  include <GL/glx.h>
#endif

#include <stdint.h>
#include <pthread.h>

/* =============================================================================
 * Constantes de capacidade
 *
 * Valores escolhidos para jogos 2D de pequeno a médio porte.
 * Aumentar ENGINE_MAX_OBJECTS ou ENGINE_MAX_PARTICLES afeta diretamente
 * o tamanho da struct Engine (alocada pelo chamador, geralmente na stack
 * do Lua FFI ou como variável global no jogo).
 * ============================================================================= */
#define ENGINE_MAX_SPRITES      64   /* sprites carregados simultaneamente       */
#define ENGINE_MAX_OBJECTS      256  /* game objects ativos no mundo             */
#define ENGINE_MAX_KEYS         256  /* keycodes rastreados por frame            */
#define ENGINE_MAX_PARTICLES    512  /* partículas no pool (ring buffer)         */
#define ENGINE_MAX_ANIMATORS    32   /* animadores de sprite simultâneos         */
#define ENGINE_MAX_LAYERS       8    /* camadas de ordenação de renderização     */

/* =============================================================================
 * Botões do mouse — índices em MouseState.buttons[]
 * ============================================================================= */
#define ENGINE_MOUSE_LEFT   0
#define ENGINE_MOUSE_MIDDLE 1
#define ENGINE_MOUSE_RIGHT  2

/* =============================================================================
 * Flags de comportamento por layer
 * ============================================================================= */
#define ENGINE_LAYER_SORT_Y     (1 << 0)  /* ordena objetos por posição Y (isométrico) */
#define ENGINE_LAYER_SORT_Z     (1 << 1)  /* ordena objetos por z_order explícito      */

/* =============================================================================
 * Limites do subsistema de FBO e Shaders
 * ============================================================================= */
#define ENGINE_MAX_FBOS     8    /* framebuffer objects simultâneos              */
#define ENGINE_MAX_SHADERS  16   /* programas de shader compilados               */

/* =============================================================================
 * Handles opacos para FBO e Shader — índices no pool interno.
 *
 * ATENÇÃO: estas são as definições canônicas de FboHandle, ShaderHandle,
 * ENGINE_FBO_INVALID e ENGINE_SHADER_INVALID para todo o projeto.
 * IRenderer.hpp NÃO deve redefinir esses tipos — apenas incluir Engine.hpp
 * ou depender de que Engine.hpp seja incluído antes.
 * ============================================================================= */
typedef int FboHandle;
typedef int ShaderHandle;

#define ENGINE_FBO_INVALID     (-1)
#define ENGINE_SHADER_INVALID  (-1)

/*
 * FboData — um Framebuffer Object com textura de cor anexada.
 *
 * width/height correspondem às dimensões da textura de cor (color_tex).
 * fbo_id e color_tex são handles GL (ou equivalentes) opacos para o FFI.
 */
typedef struct {
    unsigned int fbo_id;    /* GL: GLuint do FBO                               */
    unsigned int color_tex; /* GL: GLuint da textura de cor (RGBA)             */
    int          width;
    int          height;
    int          in_use;
} FboData;

/*
 * ShaderData — programa GLSL compilado e linkado.
 *
 * program é o handle GL do programa de shader.
 * Em backends não-GL, pode ser um índice de slot opaco.
 */
typedef struct {
    unsigned int program; /* GL: GLuint do programa                             */
    int          in_use;
} ShaderData;

/* =============================================================================
 * IDs de backend — espelham ENGINE_BACKEND_ID_* no engineffi.lua
 * ============================================================================= */
#define ENGINE_BACKEND_ID_GL    0
#define ENGINE_BACKEND_ID_DX11  1
#define ENGINE_BACKEND_ID_VK    2

/* =============================================================================
 * Subsistema de áudio (miniaudio)
 *
 * A engine gerencia um pool fixo de ENGINE_AUDIO_MAX_TRACKS trilhas.
 * Cada trilha encapsula um ma_sound opaco (evitando expor miniaudio.h
 * no header público) e um estado de máquina de estados simples.
 *
 * AudioHandle é apenas um índice inteiro no array tracks[].
 * ENGINE_AUDIO_INVALID (-1) sinaliza falha ou handle inválido.
 * ============================================================================= */
#define ENGINE_AUDIO_MAX_TRACKS   32
#define ENGINE_AUDIO_INVALID      (-1)

typedef int AudioHandle;

/* Estado do ciclo de vida de uma trilha de áudio */
typedef enum {
    AUDIO_TRACK_FREE      = 0,  /* slot disponível para nova trilha         */
    AUDIO_TRACK_PLAYING   = 1,  /* tocando normalmente                      */
    AUDIO_TRACK_PAUSED    = 2,  /* pausada; pode ser retomada               */
    AUDIO_TRACK_FINISHED  = 3   /* chegou ao fim sem loop; slot reciclável  */
} AudioTrackStatus;

/*
 * ma_sound_opaque — wrapper opaco em torno de ma_sound (miniaudio).
 *
 * Quando miniaudio.h já foi incluído antes deste header (Engine.cpp faz isso),
 * usamos o tipo real.  Caso contrário, reservamos o mesmo espaço via char[].
 * Isso evita que qualquer unidade de compilação precise incluir miniaudio.h
 * apenas por referenciar AudioTrack.
 */
#ifdef MA_SOUND_DEFINED
typedef ma_sound ma_sound_opaque;
#else
typedef struct { char _opaque[2048]; } ma_sound_opaque;
#endif

/* Uma trilha de áudio individual no pool */
typedef struct {
    ma_sound_opaque  sound;         /* estado interno do miniaudio              */
    AudioTrackStatus status;
    int              in_use;        /* 1 = slot ocupado                         */
    float            volume;        /* 0.0 – 1.0                                */
    float            pitch;         /* 0.1 – 4.0                                */
    int              loop;          /* 1 = loop infinito                        */
    AudioHandle      resume_after;  /* ao terminar, retoma esta trilha pausada  */
} AudioTrack;

/* Contexto global do subsistema de áudio; embutido dentro de Engine */
typedef struct {
    char            _ma_engine[4096]; /* ma_engine opaco — não acesse diretamente  */
    AudioTrack      tracks[ENGINE_AUDIO_MAX_TRACKS];
    pthread_mutex_t mutex;            /* protege acesso concorrente ao pool         */
    int             ready;            /* 1 após engine_audio_init() bem-sucedido    */
} AudioContext;

/* =============================================================================
 * Estruturas de dados do jogo — POD, C-compatible, seguras para FFI
 * ============================================================================= */

/*
 * SpriteData — metadados de uma textura carregada.
 *
 * O campo texture é um handle opaco: GLuint no backend GL, índice de slot
 * nos backends DX11/VK.  O FFI Lua trata como unsigned int e nunca o
 * manipula diretamente; toda operação passa pela API pública.
 */
typedef struct {
#ifdef ENGINE_BACKEND_GL
    GLuint texture;
#else
    unsigned int texture;
#endif
    int    width;
    int    height;
    int    loaded;  /* 1 após upload_texture() bem-sucedido */
} SpriteData;

/*
 * Hitbox — região de colisão relativa ao objeto.
 *
 * Quando enabled == 0, a engine usa o bounding-box do sprite (ou tile)
 * como hitbox implícita.  Quando enabled == 1, offset_x/y deslocam o
 * retângulo a partir da posição (x, y) do objeto.
 */
typedef struct {
    int offset_x, offset_y; /* deslocamento em relação a obj.x, obj.y */
    int width,    height;
    int enabled;
} Hitbox;

/*
 * GameObject — entidade básica do mundo 2D.
 *
 * Campos relevantes:
 *   use_tile  → se 1, renderiza apenas a região (tile_x*tile_w, tile_y*tile_h)
 *               da textura em vez do sprite inteiro.
 *   flip_h/v  → espelhamento horizontal/vertical sem alterar os UVs no batch.
 *   layer     → índice de camada de renderização (0..ENGINE_MAX_LAYERS-1).
 *   z_order   → ordem dentro da camada; padrão = oid na criação.
 *   alpha     → transparência: 0.0 invisível, 1.0 opaco.
 */
typedef struct {
    int           x, y;
    int           sprite_id;   /* -1 = sem sprite, desenha retângulo colorido */
    unsigned long color;       /* packed RGB via _pack_color()                 */
    int           width, height;
    int           active;      /* 0 = ignorado em draw e colisão              */
    int           tile_x, tile_y;
    int           tile_w, tile_h;
    int           use_tile;
    int           flip_h, flip_v;
    float         scale_x, scale_y;
    float         rotation;    /* graus, pivô no centro do quad               */
    float         alpha;
    int           layer;
    int           z_order;
    Hitbox        hitbox;
} GameObject;

/*
 * Camera — câmera 2D com suporte a zoom e screen-shake.
 *
 * shake_x/y são deslocamentos calculados por engine_update(); não defina
 * esses campos manualmente — use engine_camera_shake() para acionar o efeito.
 */
typedef struct {
    float  x, y;               /* posição do canto superior-esquerdo no mundo */
    float  zoom;               /* fator de escala: 1.0 = normal               */
    float  shake_x, shake_y;   /* offset de shake, atualizado por engine_update */
    float  shake_intensity;
    float  shake_duration;
    float  shake_timer;        /* tempo restante do shake atual               */
} Camera;

/*
 * Particle — partícula individual no pool.
 *
 * A cor interpola linearmente de (r0,g0,b0,a0) para (r1,g1,b1,a1) ao longo
 * da vida útil.  O tamanho interpola de size_start para size_end.
 * Quando life <= 0, active é zerado e o slot é liberado para reutilização.
 */
typedef struct {
    float  x, y;
    float  vx, vy;             /* velocidade                                  */
    float  ax, ay;             /* aceleração (ex.: gravidade)                 */
    float  life, life_max;     /* vida restante e vida total em segundos      */
    float  size_start, size_end;
    float  r0, g0, b0, a0;    /* cor inicial                                 */
    float  r1, g1, b1, a1;    /* cor final                                   */
    int    sprite_id;          /* -1 = usa white_tex (quad colorido)          */
    int    active;
} Particle;

/*
 * ParticleEmitter — configuração de um emissor contínuo ou de burst.
 *
 * rate > 0: emite 'rate' partículas por segundo de forma automática em
 * engine_particles_update().  Use engine_emitter_burst() para emissão
 * instantânea independente de rate.
 *
 * _acc é o acumulador interno de tempo; não modifique manualmente.
 */
typedef struct {
    float  x, y;
    float  vx_min, vx_max;    /* intervalo de velocidade X aleatório         */
    float  vy_min, vy_max;    /* intervalo de velocidade Y aleatório         */
    float  ax, ay;            /* aceleração constante aplicada a cada partícula */
    float  life_min, life_max;
    float  size_start, size_end;
    float  r0, g0, b0, a0;
    float  r1, g1, b1, a1;
    int    sprite_id;
    int    rate;              /* partículas/segundo; 0 = apenas burst manual  */
    int    max_particles;     /* limite local (não implementado ainda)        */
    int    active;
    float  _acc;              /* acumulador interno — não toque              */
} ParticleEmitter;

/*
 * Animator — sequência de frames de sprite associada a um GameObject.
 *
 * Atualizado automaticamente por engine_animators_update(); ao avançar
 * o frame, escreve diretamente em objects[object_id].sprite_id.
 * frame_dur é calculado como 1/fps na criação; não defina manualmente.
 */
typedef struct {
    int    sprite_ids[32];   /* sequência de IDs de sprite para cada frame   */
    int    frame_count;
    float  fps;
    float  frame_dur;        /* duração de cada frame em segundos (= 1/fps)  */
    int    loop;             /* 1 = reinicia ao chegar no último frame        */
    int    current_frame;
    float  timer;            /* tempo acumulado no frame atual               */
    int    finished;         /* 1 quando animação não-loop chegou ao fim     */
    int    object_id;        /* objeto-alvo que terá sprite_id atualizado    */
} Animator;

/*
 * MouseState — snapshot do estado do mouse no frame atual.
 *
 * buttons[] contém o estado atual (1 = pressionado).
 * buttons_prev[] é copiado de buttons[] no início de cada poll_events,
 * permitindo detectar pressed (cur && !prev) e released (!cur && prev).
 */
typedef struct {
    int   x, y;
    int   buttons[3];       /* índices: ENGINE_MOUSE_LEFT/MIDDLE/RIGHT        */
    int   buttons_prev[3];
    int   scroll;           /* +1 = scroll up, -1 = scroll down, 0 = parado  */
} MouseState;

/*
 * KeysymCache — mapa de keycode → KeySym para consulta rápida em engine_key_down.
 *
 * Preenchido uma vez em init() via _build_keysym_cache(); evita chamar
 * XKeycodeToKeysym() a cada frame.  No backend GL, o tipo é KeySym (X11);
 * nos demais, unsigned long (compatível com FFI sem incluir X11 headers).
 */
#ifdef ENGINE_BACKEND_GL
typedef struct {
    KeySym map[ENGINE_MAX_KEYS];
} KeysymCache;
#else
typedef struct {
    unsigned long map[ENGINE_MAX_KEYS];
} KeysymCache;
#endif

/* =============================================================================
 * Spatial Grid — estruturas e constantes
 *
 * Grid uniforme de COLS×ROWS células para aceleração de detecção de colisão.
 * Os valores abaixo são dimensionados para mapas de até ~4096×4096 px com
 * objetos de ~16 px.  Ajuste ENGINE_SGRID_CELL_SIZE conforme a escala do jogo.
 * ============================================================================= */
#define ENGINE_SGRID_CELL_SIZE   64   /* largura/altura de cada célula em px          */
#define ENGINE_SGRID_COLS        64   /* número de colunas do grid                    */
#define ENGINE_SGRID_ROWS        64   /* número de linhas  do grid                    */
#define ENGINE_SGRID_TOTAL_CELLS (ENGINE_SGRID_COLS * ENGINE_SGRID_ROWS)
#define ENGINE_SGRID_BUCKET_CAP  16   /* máx. de objetos por célula                   */
#define ENGINE_SGRID_OBJ_MAX_CELLS 4  /* máx. de células que um objeto pode ocupar    */

/*
 * SpatialCell — uma célula do grid.
 * oids[]  — IDs dos objetos que intersectam esta célula.
 * count   — quantos slots estão em uso (0..ENGINE_SGRID_BUCKET_CAP).
 */
typedef struct {
    int oids[ENGINE_SGRID_BUCKET_CAP];
    int count;
} SpatialCell;

/*
 * SpatialObjEntry — índices das células onde um objeto está inscrito.
 * Permite remoção O(k) sem varrer o grid todo.
 */
typedef struct {
    int cell_idx[ENGINE_SGRID_OBJ_MAX_CELLS];
    int count;
} SpatialObjEntry;

/*
 * SpatialGrid — estado completo do grid espacial.
 * Embutido diretamente na struct Engine (sem alocação heap).
 */
typedef struct {
    SpatialCell     cells[ENGINE_SGRID_TOTAL_CELLS];
    SpatialObjEntry obj_cells[ENGINE_MAX_OBJECTS];
    int             cell_size;
    int             cols;
    int             rows;
    int             enabled;  /* 0 = desabilitado; funções sgrid são no-op */
    int             dirty;    /* sinaliza que um rebuild é necessário       */
} SpatialGrid;

/* =============================================================================
 * Engine — contexto principal do jogo
 *
 * Layout da struct:
 *   1. renderer_impl / backend_id — identificação e acesso ao backend gráfico.
 *   2. Dimensões da janela e da área de render virtual.
 *   3. Recursos: sprites, objetos, partículas, animadores.
 *   4. Input: teclado (via ponteiros com double-buffer) e mouse.
 *   5. Câmera e efeitos de tela (fade, shake).
 *   6. Temporização: time_elapsed e delta_time atualizados por engine_update().
 *   7. Áudio: AudioContext embutido diretamente (sem alocação extra).
 *
 * Compatibilidade FFI:
 *   A struct é POD puro.  renderer_impl é void* — o Lua vê o ponteiro mas
 *   nunca o dereference; toda operação passa pelas funções extern "C".
 *   O espelho desta struct no engineffi.lua deve permanecer sincronizado
 *   manualmente sempre que campos forem adicionados ou reordenados.
 * ============================================================================= */
typedef struct {
    /* --- Ponteiro opaco para o backend gráfico concreto (IRenderer*) ------- */
    void       *renderer_impl; /* alocado em engine_init(), liberado em engine_destroy() */
    int         backend_id;    /* ENGINE_BACKEND_ID_GL / DX11 / VK                       */

    /* --- Dimensões ---------------------------------------------------------- */
    int         win_w, win_h;       /* tamanho real da janela OS em pixels              */
    int         depth;              /* profundidade de cor (fixo em 24)                 */
    int         scale;              /* fator de pixel-art: render_w * scale == win_w    */
    int         render_w, render_h; /* resolução virtual do jogo em pixels              */

    /* --- Textura branca 1×1 ------------------------------------------------- */
    unsigned int white_tex; /* usada para primitivas sólidas sem sprite           */

    /* --- Recursos de renderização ------------------------------------------- */
    SpriteData  sprites[ENGINE_MAX_SPRITES];
    int         sprite_count;

    GameObject  objects[ENGINE_MAX_OBJECTS];
    int         object_count;

    /* --- Input de teclado --------------------------------------------------- */
    int        *keys;       /* frame atual: keys[keycode] == 1 se pressionado     */
    int        *keys_prev;  /* frame anterior; permite detectar pressed/released   */
    KeysymCache ksym_cache;
    MouseState  mouse;

    /* --- Sistema de partículas ---------------------------------------------- */
    Particle        particles[ENGINE_MAX_PARTICLES];
    int             particle_count; /* high-water mark do pool                   */
    int             particle_next;  /* cursor do ring buffer                     */
    ParticleEmitter emitters[16];
    int             emitter_count;

    /* --- Animadores de sprite ----------------------------------------------- */
    Animator    animators[ENGINE_MAX_ANIMATORS];
    int         animator_count;

    /* --- Câmera ------------------------------------------------------------- */
    Camera      camera;
    int         camera_enabled; /* 0 = câmera fixa na origem; 1 = aplica transform */

    /* --- Efeito de fade ----------------------------------------------------- */
    float       fade_alpha;        /* opacidade atual da sobreposição              */
    float       fade_target;       /* opacidade-alvo; engine_update() aproxima     */
    float       fade_speed;        /* unidades de alpha por segundo                */
    int         fade_r, fade_g, fade_b;

    /* --- Temporização ------------------------------------------------------- */
    double      time_elapsed; /* segundos desde engine_init()                    */
    double      delta_time;   /* duração do frame anterior em segundos           */

    /* --- Estado geral ------------------------------------------------------- */
    int         running;      /* loop principal: 0 encerra o jogo               */
    unsigned long bg_color;   /* cor de fundo packed RGB                         */

    int         fullscreen;
    int         saved_win_w, saved_win_h;
    int         saved_render_w, saved_render_h;

    /* --- Subsistema de áudio ------------------------------------------------ */
    AudioContext audio;

    /* --- FBOs e Shaders ----------------------------------------------------- */
    FboData      fbos[ENGINE_MAX_FBOS];
    ShaderData   shaders[ENGINE_MAX_SHADERS];
    ShaderHandle active_shader; /* -1 = pipeline fixed-function (padrão)        */

    /* --- Spatial Grid para aceleração de colisão --------------------------- */
    SpatialGrid  sgrid;
} Engine;

/* =============================================================================
 * IRenderer — interface abstrata de backend gráfico  (somente C++)
 *
 * Definida em RenderizadorIRenderer.hpp para evitar duplicação: RendererDX11.hpp
 * e Engine.hpp ambos precisavam de IRenderer, o que gerava conflitos de
 * redefinição de classe e de QuadParams quando incluídos na mesma unidade
 * de tradução.
 *
 * A solução canônica:
 *   • IRenderer e QuadParams vivem APENAS em RenderizadorIRenderer.hpp.
 *   • Engine.hpp inclui RenderizadorIRenderer.hpp aqui (somente C++).
 *   • RendererDX11.hpp inclui IRenderer.hpp diretamente.
 *   • Nenhum deles redefine FboHandle/ShaderHandle/ENGINE_FBO_INVALID —
 *     essas definições pertencem a Engine.hpp e devem ser incluídas antes.
 *
 * Convenções de IRenderer:
 *   • flush()           → envia o batch pendente de quads para a GPU.
 *   • present()         → swap de buffers / apresentação da swapchain.
 *   • push_quad()       → acumula no batch; set_texture() pode disparar um
 *                         flush interno se a textura mudou.
 *   • camera_push/pop() → aplicam/removem a transformação de câmera; o código
 *                         entre eles é renderizado no espaço do mundo.
 * ============================================================================= */
#ifdef __cplusplus
#  include "Renderizador/IRenderer.hpp"
#endif /* __cplusplus */

/* =============================================================================
 * API pública — extern "C"
 *
 * Todos os nomes são estáveis e espelhados em engineffi.lua.
 * Nenhuma função retorna ponteiro para estrutura interna (exceto os
 * ponteiros de saída intencional como out_x/out_y).
 * ============================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/* Ciclo de vida */
int  engine_init(Engine *e, int width, int height, const char *title, int scale);
void engine_destroy(Engine *e);
void engine_set_background(Engine *e, int r, int g, int b);

/* Loop principal */
void engine_poll_events(Engine *e);
void engine_clear(Engine *e);
void engine_draw(Engine *e);
void engine_flush(Engine *e);
void engine_present(Engine *e);
void engine_cap_fps(Engine *e, int fps_target);
void engine_update(Engine *e, float dt);  /* dt <= 0 → usa delta_time interno */

/* Temporização */
double engine_get_time(Engine *e);   /* segundos desde engine_init() */
double engine_get_delta(Engine *e);  /* duração do último frame       */

/* Sprites */
int  engine_load_sprite(Engine *e, const char *path);
int  engine_load_sprite_region(Engine *e, const char *path, int x, int y, int w, int h);

/* Objetos */
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
void engine_remove_object(Engine *e, int oid);  /* marca active=0; slot não é reutilizado */

/* Colisão AABB */
int  engine_check_collision(Engine *e, int oid1, int oid2);
int  engine_check_collision_rect(Engine *e, int oid, int rx, int ry, int rw, int rh);
int  engine_check_collision_point(Engine *e, int oid, int px, int py);

/* Spatial Grid */
void engine_sgrid_init         (Engine *e, int cell_size);
void engine_sgrid_destroy      (Engine *e);
void engine_sgrid_rebuild      (Engine *e);
void engine_sgrid_insert_object(Engine *e, int oid);
void engine_sgrid_remove_object(Engine *e, int oid);
void engine_sgrid_update_object(Engine *e, int oid);
int  engine_sgrid_query_rect   (Engine *e, int x, int y, int w, int h, int *out_oids, int cap);
int  engine_sgrid_query_object (Engine *e, int oid, int *out_oids, int cap);
int  engine_sgrid_query_point  (Engine *e, int px, int py, int *out_oids, int cap);
int  engine_sgrid_first_collision(Engine *e, int oid);
int  engine_sgrid_all_collisions (Engine *e, int oid, int *out_oids, int cap);

/* Câmera */
void engine_camera_set(Engine *e, float x, float y);
void engine_camera_move(Engine *e, float dx, float dy);
void engine_camera_zoom(Engine *e, float zoom);
void engine_camera_follow(Engine *e, int oid, float lerp_speed);  /* 0=estático, 1=instantâneo */
void engine_camera_shake(Engine *e, float intensity, float duration);
void engine_camera_enable(Engine *e, int enabled);
void engine_world_to_screen(Engine *e, float wx, float wy, float *sx, float *sy);
void engine_screen_to_world(Engine *e, float sx, float sy, float *wx, float *wy);

/* Partículas */
int  engine_emitter_add(Engine *e, ParticleEmitter *cfg);
void engine_emitter_set_pos(Engine *e, int eid, float x, float y);
void engine_emitter_burst(Engine *e, int eid, int count);
void engine_emitter_remove(Engine *e, int eid);
void engine_particles_update(Engine *e, float dt);
void engine_particles_draw(Engine *e);

/* Animadores */
int  engine_animator_add(Engine *e, int *sprite_ids, int frame_count, float fps, int loop, int object_id);
void engine_animator_play(Engine *e, int aid);
void engine_animator_stop(Engine *e, int aid);
void engine_animator_reset(Engine *e, int aid);
int  engine_animator_finished(Engine *e, int aid);
void engine_animators_update(Engine *e, float dt);

/* Fade de tela */
void engine_fade_to(Engine *e, float target_alpha, float speed, int r, int g, int b);
void engine_fade_draw(Engine *e);
int  engine_fade_done(Engine *e);

/* Primitivas de desenho 2D */
void engine_draw_rect(Engine *e, int x, int y, int w, int h, int r, int g, int b);
void engine_draw_rect_outline(Engine *e, int x, int y, int w, int h, int r, int g, int b, int thickness);
void engine_draw_line(Engine *e, int x0, int y0, int x1, int y1, int r, int g, int b, int thickness);
void engine_draw_circle(Engine *e, int cx, int cy, int radius, int r, int g, int b, int filled);
void engine_draw_overlay(Engine *e, int x, int y, int w, int h, int r, int g, int b, float alpha);

/* Efeitos de ambiente */
void engine_draw_rain(Engine *e, int screen_w, int screen_h, int frame,
                      const int *gotas_x, const int *gotas_y, int n_gotas,
                      int gota_w, int gota_h);
void engine_draw_night(Engine *e, int screen_w, int screen_h, float intensidade, int offset);

/* Tilemap e sprites com recorte */
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

/* Texto e UI baseados em bitmap font (sprite sheet de caracteres) */
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

/* Input — teclado */
int  engine_key_down(Engine *e, const char *key);     /* mantido pressionado      */
int  engine_key_pressed(Engine *e, const char *key);  /* pressionado neste frame  */
int  engine_key_released(Engine *e, const char *key); /* solto neste frame        */

/* Input — mouse */
int  engine_mouse_down(Engine *e, int button);
int  engine_mouse_pressed(Engine *e, int button);
int  engine_mouse_released(Engine *e, int button);
void engine_mouse_pos(Engine *e, int *out_x, int *out_y);
int  engine_mouse_scroll(Engine *e); /* +1 = cima, -1 = baixo, 0 = sem movimento */

/* Janela */
void engine_toggle_fullscreen(Engine *e);

/* Áudio */
int         engine_audio_init   (Engine *e);
void        engine_audio_update (Engine *e); /* chame uma vez por frame; detecta trilhas finalizadas */
void        engine_audio_destroy(Engine *e);
AudioHandle engine_audio_play   (Engine *e, const char *file, int loop,
                                  float volume, float pitch, AudioHandle resume_after);
void        engine_audio_pause  (Engine *e, AudioHandle h);
void        engine_audio_resume (Engine *e, AudioHandle h);
void        engine_audio_stop   (Engine *e, AudioHandle h);  /* para e libera o slot */
void        engine_audio_volume (Engine *e, AudioHandle h, float volume);
void        engine_audio_pitch  (Engine *e, AudioHandle h, float pitch);
int         engine_audio_done   (Engine *e, AudioHandle h);  /* 1 se terminou ou inválido */

/* FBOs — Framebuffer Objects */
FboHandle    engine_fbo_create (Engine *e, int w, int h);
void         engine_fbo_destroy(Engine *e, FboHandle fh);
void         engine_fbo_bind   (Engine *e, FboHandle fh); /* rendering → FBO     */
void         engine_fbo_unbind (Engine *e);               /* rendering → tela    */
unsigned int engine_fbo_texture(Engine *e, FboHandle fh); /* textura de cor      */

/* Shaders */
ShaderHandle engine_shader_create (Engine *e, const char *vert_src, const char *frag_src);
void         engine_shader_destroy(Engine *e, ShaderHandle sh);
void         engine_shader_use    (Engine *e, ShaderHandle sh); /* ativa o programa  */
void         engine_shader_none   (Engine *e);                  /* fixed-function    */
void         engine_shader_set_int  (Engine *e, ShaderHandle sh, const char *name, int   v);
void         engine_shader_set_float(Engine *e, ShaderHandle sh, const char *name, float v);
void         engine_shader_set_vec2 (Engine *e, ShaderHandle sh, const char *name, float x, float y);
void         engine_shader_set_vec4 (Engine *e, ShaderHandle sh, const char *name, float x, float y, float z, float w);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENGINE_HPP */