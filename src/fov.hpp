/*
 * fov.hpp — Sistema de Field of View 2D via Shadowcasting Recursivo.
 *
 * Implementa o algoritmo "Recursive Shadowcasting" de Björn Permutations
 * (popularizado por RogueBasin), adaptado para a engine 2D em tiles.
 *
 * Conceitos-chave:
 *   • Octante: o plano é dividido em 8 octantes (fatias de 45°).
 *     O algoritmo resolve um octante e mapeia os outros 7 por reflexão/rotação.
 *   • Sombra: faixas angulares bloqueadas por paredes; representadas por
 *     pares de slopes (inclinação inicial / final do raio).
 *   • Visibilidade: um tile é visível se seu range angular não está
 *     completamente coberto por sombras anteriores.
 *
 * API pública (C-linkage para FFI LuaJIT):
 *   engine_fov_compute()      — calcula e preenche o array vis[].
 *   engine_fov_draw_shadow()  — sobrepõe escuridão usando engine_draw_overlay().
 *   engine_fov_draw_debug()   — visualiza raios e octantes (dev-only).
 *
 * Layout do array de visibilidade:
 *   vis[row * cols + col] = valor float [0.0, 1.0]
 *     0.0 → completamente escuro (nunca visto ou bloqueado)
 *     1.0 → totalmente visível (no raio de visão atual)
 *   Valores intermediários são usados pelo modo FOG_OF_WAR para tiles
 *   já explorados mas fora do raio atual (valor ENGINE_FOV_EXPLORED).
 *
 * Modos de operação (FovMode):
 *   FOV_MODE_BASIC     — tiles dentro do raio = 1.0; fora = 0.0.
 *   FOV_MODE_FOG_WAR   — tiles visitados ficam com ENGINE_FOV_EXPLORED (0.35)
 *                        após saírem do raio de visão.
 *   FOV_MODE_SMOOTH    — aplica falloff linear da borda interna até o raio.
 *
 * Integração com o tilemap:
 *   O chamador fornece um callback FovBlockFn que retorna 1 se um tile
 *   bloqueia visão (parede) e 0 se é transparente.  Isso desacopla o
 *   sistema de qualquer formato específico de tilemap.
 *
 * Thread safety:
 *   engine_fov_compute() é stateless (não altera Engine); pode ser chamado
 *   em thread separada desde que vis[] não seja lido ao mesmo tempo.
 */

#pragma once
#ifndef ENGINE_FOV_HPP
#define ENGINE_FOV_HPP

#include "Engine.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constantes de visibilidade no array vis[]
 * ========================================================================= */
#define ENGINE_FOV_VISIBLE   1.0f   /* tile no cone de visão atual          */
#define ENGINE_FOV_EXPLORED  0.35f  /* tile já visitado, fora do raio atual */
#define ENGINE_FOV_DARK      0.0f   /* tile nunca visto                     */

/* Máximo de tiles suportados em vis[] — suficiente para mapas 256×256 */
#define ENGINE_FOV_MAX_TILES (256 * 256)

/* =========================================================================
 * FovMode — controla o comportamento de persistência da visibilidade
 * ========================================================================= */
typedef enum {
    FOV_MODE_BASIC    = 0,  /* apenas raio atual; sem memória               */
    FOV_MODE_FOG_WAR  = 1,  /* tiles visitados ficam em ENGINE_FOV_EXPLORED */
    FOV_MODE_SMOOTH   = 2   /* falloff linear até a borda do raio           */
} FovMode;

/* =========================================================================
 * FovBlockFn — callback de opacidade de tile
 *
 * Retorna 1 se o tile em (col, row) bloqueia a visão (parede, objeto sólido).
 * Retorna 0 se é transparente (chão, vazio).
 *
 * user_data: ponteiro opaco passado de volta pelo caller — use para
 *   referenciar o tilemap, o Engine*, ou qualquer contexto necessário.
 *
 * Importante: chamada intensivamente dentro de engine_fov_compute();
 *   mantenha leve (sem alocações, sem I/O).
 * ========================================================================= */
typedef int (*FovBlockFn)(int col, int row, void *user_data);

/* =========================================================================
 * FovParams — parâmetros completos de uma computação de FOV
 * ========================================================================= */
typedef struct {
    /* Posição do observador em coordenadas de tile */
    int origin_col;
    int origin_row;

    /* Raio máximo de visão em tiles */
    int radius;

    /* Dimensões do mapa (tiles) */
    int map_cols;
    int map_rows;

    /* Modo de acumulação de visibilidade */
    FovMode mode;

    /* Callback de opacidade — deve ser thread-safe se usado em paralelo */
    FovBlockFn is_blocking;
    void      *user_data;   /* passado de volta para is_blocking()          */

    /*
     * vis[] — array de saída, alocado e gerenciado pelo CALLER.
     * Tamanho mínimo: map_rows * map_cols floats.
     * engine_fov_compute() reseta apenas os tiles dentro do bounding-box
     * do raio antes de recomputar; tiles fora são preservados (para fog-of-war).
     */
    float *vis;
} FovParams;

/* =========================================================================
 * engine_fov_compute() — calcula o campo de visão
 *
 * Preenche params->vis[] usando shadowcasting recursivo em 8 octantes.
 * Complexidade: O(raio²) no pior caso (sem paredes); tipicamente O(tiles_visíveis).
 *
 * Pré-condições:
 *   • params->vis aponta para array de pelo menos (map_cols * map_rows) floats.
 *   • origin_col/row estão dentro de [0, map_cols-1] × [0, map_rows-1].
 *   • is_blocking != NULL.
 * ========================================================================= */
void engine_fov_compute(FovParams *params);

/* =========================================================================
 * engine_fov_draw_shadow() — renderiza a escuridão sobre o mundo
 *
 * Para cada tile, desenha um overlay semi-transparente proporcional a
 * (1.0 - vis[tile]).  Deve ser chamado APÓS engine_draw_tilemap() e
 * objetos do mundo, mas ANTES da UI/HUD.
 *
 * Parâmetros:
 *   e          — contexto da engine
 *   vis        — array de visibilidade computado por engine_fov_compute()
 *   map_cols   — colunas do mapa
 *   map_rows   — linhas do mapa
 *   tile_w     — largura de um tile em pixels virtuais
 *   tile_h     — altura de um tile em pixels virtuais
 *   offset_x   — deslocamento X do tilemap na tela (mesmo que engine_draw_tilemap)
 *   offset_y   — deslocamento Y do tilemap na tela
 *   dark_r/g/b — cor da escuridão (normalmente 0,0,0 para preto)
 * ========================================================================= */
void engine_fov_draw_shadow(Engine *e,
                            const float *vis,
                            int map_cols, int map_rows,
                            int tile_w,  int tile_h,
                            int offset_x, int offset_y,
                            int dark_r, int dark_g, int dark_b);

/* =========================================================================
 * engine_fov_draw_debug() — visualiza raios e limites de octante (dev-only)
 *
 * Desenha linhas coloridas para cada octante a partir da posição do observador.
 * Útil para ajustar o raio e verificar oclusões visualmente.
 *
 * Não deve ser chamado em builds de release; tem custo O(raio) em draw_line.
 * ========================================================================= */
void engine_fov_draw_debug(Engine *e,
                           int origin_col, int origin_row,
                           int radius,
                           int tile_w, int tile_h,
                           int offset_x, int offset_y);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENGINE_FOV_HPP */
