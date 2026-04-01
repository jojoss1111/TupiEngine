/*
 * mapa.hpp — Cabeçalho do sistema de mapas 2D.
 *
 * Define as estruturas de dados do mapa, flags de comportamento de blocos,
 * e a API pública (extern "C") usada tanto pelo C++ quanto pelo FFI Lua.
 *
 * O sistema suporta dois formatos de carregamento:
 *   - JSON  : via arquivo .json  (campo "layers" com tiles e objetos)
 *   - Lua   : via arquivo .lua   (tabela MapData retornada por loadfile)
 *
 * A renderização é feita inteiramente em C++. O Lua apenas descreve
 * quais tiles vão onde e quais propriedades cada bloco possui.
 */

#ifndef MAPA_HPP
#define MAPA_HPP

#include "Engine.hpp"
#include <stdint.h>

/* =============================================================================
 * Limites do subsistema de mapa
 * ============================================================================= */
#define MAPA_MAX_LARGURA      256   /* colunas máximas de tiles                  */
#define MAPA_MAX_ALTURA       256   /* linhas máximas de tiles                   */
#define MAPA_MAX_CAMADAS      8     /* camadas de tiles sobrepostas              */
#define MAPA_MAX_OBJETOS      512   /* objetos especiais no mapa (baús, npcs...) */
#define MAPA_MAX_PROPRIEDADES 16    /* propriedades string por objeto            */
#define MAPA_TILE_INVALIDO    -1    /* tile vazio / sem sprite                   */

/* =============================================================================
 * Flags de bloco — combinam-se com OR bit a bit
 *
 *  MAPA_FLAG_COLISOR   → o tile bloqueia movimento do jogador (AABB sólido)
 *  MAPA_FLAG_TRIGGER   → o tile dispara evento quando o jogador se aproxima
 *  MAPA_FLAG_AGUA      → tile tratado como água (afeta física / animação)
 *  MAPA_FLAG_ESCADA    → tile permite escalar/descer
 *  MAPA_FLAG_SOMBRA    → tile projeta sombra no FOV
 *  MAPA_FLAG_ANIMADO   → tile possui animação definida na camada de tiles
 * ============================================================================= */
#define MAPA_FLAG_COLISOR   (1 << 0)
#define MAPA_FLAG_TRIGGER   (1 << 1)
#define MAPA_FLAG_AGUA      (1 << 2)
#define MAPA_FLAG_ESCADA    (1 << 3)
#define MAPA_FLAG_SOMBRA    (1 << 4)
#define MAPA_FLAG_ANIMADO   (1 << 5)

/* =============================================================================
 * Resultado de evento de trigger — retornado por mapa_verificar_trigger()
 * ============================================================================= */
typedef enum {
    MAPA_TRIGGER_NENHUM   = 0,  /* nenhum trigger na posição                    */
    MAPA_TRIGGER_ATIVO    = 1,  /* trigger encontrado; veja MapaTrigger.tipo    */
    MAPA_TRIGGER_ERRO     = -1  /* posição inválida                             */
} MapaTriggerResultado;

/* =============================================================================
 * Tipos de trigger pré-definidos
 * ============================================================================= */
typedef enum {
    TRIGGER_TIPO_GENERICO   = 0,
    TRIGGER_TIPO_BAU        = 1,
    TRIGGER_TIPO_PORTA      = 2,
    TRIGGER_TIPO_NPC        = 3,
    TRIGGER_TIPO_TELEPORTE  = 4,
    TRIGGER_TIPO_SCRIPT     = 5   /* executa script Lua customizado             */
} TriggerTipo;

/* =============================================================================
 * Estrutura de um tile individual em uma camada
 *
 * sprite_id  → ID do sprite carregado na engine (-1 = vazio)
 * tile_col   → coluna na spritesheet (para tilemaps com atlas)
 * tile_lin   → linha na spritesheet
 * flags      → combinação de MAPA_FLAG_*
 * anim_fps   → frames por segundo da animação (0 = estático)
 * anim_frames→ lista de sprite_ids para animação (0 = não animado)
 * n_frames   → número de frames na animação
 * ============================================================================= */
typedef struct {
    int     sprite_id;
    int     tile_col;
    int     tile_lin;
    uint8_t flags;
    float   anim_fps;
    int     anim_frames[8];   /* até 8 frames por tile animado                  */
    int     n_frames;
} MapaTile;

/* =============================================================================
 * Camada de tiles
 *
 * tiles[] é uma matriz linearizada [linha * largura + coluna].
 * nome é usado para identificação no editor/script Lua.
 * visivel → se 0, a camada não é desenhada (útil para debug)
 * z_order → ordem de renderização entre camadas (menor = desenhado primeiro)
 * ============================================================================= */
typedef struct {
    char     nome[32];
    MapaTile tiles[MAPA_MAX_ALTURA * MAPA_MAX_LARGURA];
    int      visivel;
    int      z_order;
} MapaCamada;

/* =============================================================================
 * Objeto especial no mapa (baú, NPC, teleporte, etc.)
 *
 * id         → identificador único (setado pelo loader)
 * tipo       → TriggerTipo
 * col, lin   → posição em tiles no mapa
 * sprite_id  → sprite do objeto (-1 = invisível, apenas lógico)
 * raio       → distância (em tiles) para ativar o trigger
 * ativo      → 0 após ser coletado / usado
 * props      → propriedades extras definidas no JSON/Lua (chave=valor string)
 * ============================================================================= */
typedef struct {
    int    id;
    int    tipo;       /* TriggerTipo                                            */
    int    col;
    int    lin;
    int    sprite_id;
    float  raio;
    int    ativo;
    char   props[MAPA_MAX_PROPRIEDADES][2][64]; /* [i][0]=chave  [i][1]=valor   */
    int    n_props;
} MapaObjeto;

/* =============================================================================
 * Dados completos de um mapa
 *
 * O loader preenche esta estrutura a partir de JSON ou Lua.
 * A engine renderiza, colide e processa triggers com base nela.
 * ============================================================================= */
typedef struct {
    int        largura;                        /* colunas de tiles               */
    int        altura;                         /* linhas de tiles                */
    int        tile_w;                         /* largura de cada tile em pixels */
    int        tile_h;                         /* altura de cada tile em pixels  */
    int        n_camadas;
    MapaCamada camadas[MAPA_MAX_CAMADAS];

    int        n_objetos;
    MapaObjeto objetos[MAPA_MAX_OBJETOS];

    /* deslocamento de câmera aplicado ao renderizar */
    int        offset_x;
    int        offset_y;

    /* sprite sheet principal (pode ser overrideado por tile) */
    int        sprite_atlas;

    int        carregado;                      /* 1 após mapa_carregar_*()       */
} MapaDados;

/* =============================================================================
 * Resultado de verificação de trigger
 * ============================================================================= */
typedef struct {
    MapaTriggerResultado resultado;
    MapaObjeto          *objeto;   /* ponteiro para o objeto ativo (ou NULL)     */
    float                distancia; /* distância em tiles ao objeto               */
} MapaVerificacao;

/* =============================================================================
 * API pública — visível pelo FFI LuaJIT
 * ============================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/* Ciclo de vida */
void mapa_init    (MapaDados *m);
void mapa_destruir(MapaDados *m);

/* Carregamento */
int  mapa_carregar_json(MapaDados *m, Engine *e, const char *caminho);
int  mapa_carregar_lua (MapaDados *m, Engine *e, const char *caminho);

/* Renderização (chamada pelo loop principal) */
void mapa_desenhar       (MapaDados *m, Engine *e);
void mapa_desenhar_camada(MapaDados *m, Engine *e, int camada_idx);
void mapa_desenhar_objetos(MapaDados *m, Engine *e);

/* Consultas de tile */
MapaTile *mapa_get_tile  (MapaDados *m, int camada, int col, int lin);
int       mapa_colisor   (MapaDados *m, int col, int lin);  /* 1 se qualquer camada tem COLISOR */
int       mapa_flags     (MapaDados *m, int col, int lin);  /* OR de todos os flags no tile     */

/* Conversão de coordenadas */
void mapa_pixel_para_tile(MapaDados *m, int px, int py, int *out_col, int *out_lin);
void mapa_tile_para_pixel(MapaDados *m, int col, int lin, int *out_x, int *out_y);

/* Triggers e objetos */
MapaVerificacao mapa_verificar_trigger(MapaDados *m, float px_jogador, float py_jogador);
MapaObjeto     *mapa_get_objeto       (MapaDados *m, int id);
void            mapa_desativar_objeto (MapaDados *m, int id);

/* Câmera do mapa */
void mapa_set_offset(MapaDados *m, int ox, int oy);
void mapa_centralizar_em(MapaDados *m, Engine *e, int col, int lin);

/* Modificação em tempo de execução */
void mapa_set_tile(MapaDados *m, int camada, int col, int lin,
                   int sprite_id, int tile_col, int tile_lin, uint8_t flags);

#ifdef __cplusplus
}
#endif

#endif /* MAPA_HPP */