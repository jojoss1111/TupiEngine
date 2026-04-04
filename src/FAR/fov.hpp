//fov.hpp
/*
 * Calcula quais tiles são visíveis a partir de uma posição de origem,
 * usando o algoritmo Recursive Shadowcasting (RogueBasin).
 *
 * Como funciona (resumo):
 *   O plano é dividido em 8 octantes de 45°. Em cada octante, o algoritmo
 *   varre anéis de tiles e rastreia "sombras" (faixas angulares bloqueadas
 *   por paredes). Um tile é visível se seu ângulo não está coberto por sombra.
 *
 * Fluxo de uso:
 *   1. Aloque um array float vis[map_cols * map_rows].
 *   2. Preencha FovParams e chame engine_fov_compute().
 *   3. Consulte vis[] para saber o estado de cada tile.
 *   4. Chame engine_fov_draw_shadow() para sobrepor a escuridão.
 *
 * Valores de vis[]:
 *   ENGINE_FOV_VISIBLE  (1.0) — tile no cone de visão atual
 *   ENGINE_FOV_EXPLORED (0.35)— tile já visitado, fora do raio (modo FOG_WAR)
 *   ENGINE_FOV_DARK     (0.0) — tile nunca visto
 *
 * Modos (FovMode):
 *   FOV_MODE_BASIC    — sem memória; tudo escuro fora do raio atual
 *   FOV_MODE_FOG_WAR  — tiles visitados ficam em EXPLORED após sair do raio
 *   FOV_MODE_SMOOTH   — falloff linear até a borda do raio
 */

#pragma once
#ifndef ENGINE_FOV_HPP
#define ENGINE_FOV_HPP

#include "Engine.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constantes de visibilidade
 * ------------------------------------------------------------------------- */
#define ENGINE_FOV_VISIBLE   1.0f   /* tile visível agora                    */
#define ENGINE_FOV_EXPLORED  0.35f  /* tile já visto, fora do raio atual     */
#define ENGINE_FOV_DARK      0.0f   /* tile nunca visto                      */

/* Tamanho máximo suportado de vis[] (256×256 tiles). */
#define ENGINE_FOV_MAX_TILES (256 * 256)

/* -------------------------------------------------------------------------
 * FovMode — controla a persistência de visibilidade entre frames
 * ------------------------------------------------------------------------- */
typedef enum {
    FOV_MODE_BASIC    = 0,  /* sem memória                                   */
    FOV_MODE_FOG_WAR  = 1,  /* tiles visitados ficam em EXPLORED             */
    FOV_MODE_SMOOTH   = 2   /* falloff linear até a borda                    */
} FovMode;

/* -------------------------------------------------------------------------
 * FovBlockFn — callback que diz se um tile bloqueia a visão.
 *
 * Retorna 1 se o tile (col, row) é opaco (parede), 0 se transparente.
 * user_data: ponteiro opaco que você passa em FovParams.
 *
 * É chamado intensivamente — mantenha rápido (sem alocações, sem I/O).
 * ------------------------------------------------------------------------- */
typedef int (*FovBlockFn)(int col, int row, void *user_data);

/* -------------------------------------------------------------------------
 * FovParams — configuração de uma computação de FOV
 * ------------------------------------------------------------------------- */
typedef struct {
    int origin_col;     /* coluna do observador                              */
    int origin_row;     /* linha do observador                               */
    int radius;         /* raio máximo em tiles                              */
    int map_cols;       /* largura do mapa em tiles                          */
    int map_rows;       /* altura do mapa em tiles                           */
    FovMode    mode;
    FovBlockFn is_blocking;
    void      *user_data;

    /*
     * vis[] — array de saída, alocado pelo CALLER.
     * Tamanho mínimo: map_rows * map_cols floats.
     * Apenas o bounding-box do raio é resetado antes de cada compute.
     */
    float *vis;
} FovParams;

/* -------------------------------------------------------------------------
 * engine_fov_compute — calcula o campo de visão.
 *
 * Preenche params->vis[] via shadowcasting em 8 octantes.
 * Complexidade: O(raio²) no pior caso; tipicamente O(tiles_visíveis).
 * Thread-safe desde que vis[] não seja lido simultaneamente.
 * ------------------------------------------------------------------------- */
void engine_fov_compute(FovParams *params);

/* -------------------------------------------------------------------------
 * engine_fov_draw_shadow — renderiza a escuridão sobre o tilemap.
 *
 * Desenha um overlay proporcional a (1 - vis[tile]) em cada tile.
 * Chame APÓS engine_draw_tilemap() e ANTES da UI/HUD.
 *
 * offset_x/y: mesmo deslocamento usado ao desenhar o tilemap.
 * dark_r/g/b: cor da sombra (normalmente 0,0,0 = preto).
 * ------------------------------------------------------------------------- */
void engine_fov_draw_shadow(Engine *e,
                            const float *vis,
                            int map_cols, int map_rows,
                            int tile_w,  int tile_h,
                            int offset_x, int offset_y,
                            int dark_r, int dark_g, int dark_b);

/* -------------------------------------------------------------------------
 * engine_fov_draw_debug — visualiza octantes e raio (apenas em dev).
 *
 * Desenha linhas coloridas para cada octante a partir do observador.
 * Não use em builds de release.
 * ------------------------------------------------------------------------- */
void engine_fov_draw_debug(Engine *e,
                           int origin_col, int origin_row,
                           int radius,
                           int tile_w, int tile_h,
                           int offset_x, int offset_y);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENGINE_FOV_HPP */