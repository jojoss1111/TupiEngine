/*
 * fov.cpp — Implementação do sistema de Field of View 2D via Shadowcasting.
 *
 * Algoritmo: "Recursive Shadowcasting" (Björn Permutations / RogueBasin).
 *
 * Cada um dos 8 octantes é varrido em anéis concêntricos (distância 1..raio).
 * Para cada anel, o algoritmo rastreia as "sombras" acumuladas como pares
 * (slope_start, slope_end) normalizados.  Um tile é visível se nenhuma sombra
 * cobre completamente seu intervalo angular.  Paredes adicionam novas sombras
 * à lista; a lista é mantida ordenada para permitir early-exit.
 *
 * Transformação de octante:
 *   O algoritmo resolve o octante 0 (NE, dx > 0, dy > 0, |dx| >= |dy|) e
 *   mapeia os outros 7 via a tabela transform[8][4] = {mul_col, mul_row, ...}.
 *   Cada par (dc, dr) no espaço do octante 0 é transformado em (col, row) do
 *   mapa aplicando as multiplicações corretas.
 *
 * Shadowcasting recursivo vs. iterativo:
 *   A versão clássica é recursiva por faixa de slope.  Esta implementação usa
 *   um stack explícito (ShadowFrame) para evitar recursão profunda em mapas
 *   abertos com raio grande, eliminando o risco de stack overflow.
 *
 * Referências:
 *   • https://www.roguebasin.com/index.php/FOV_using_recursive_shadowcasting
 *   • Lode Vandevenne — "Raycasting in 2D"
 */

#include "fov.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>

/* =========================================================================
 * Helpers internos
 * ========================================================================= */

/* Clamp de float */
static inline float _fclamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Distância euclidiana ao quadrado entre dois pontos de tile */
static inline float _dist2(int dc, int dr) {
    return (float)(dc * dc + dr * dr);
}

/* =========================================================================
 * Tabela de transformação de octante
 *
 * Cada linha: { mul_dc_to_col, mul_dr_to_col, mul_dc_to_row, mul_dr_to_row }
 *
 * A transformação é:
 *   col = origin_col + dc * m[0] + dr * m[1]
 *   row = origin_row + dc * m[2] + dr * m[3]
 *
 * Os 8 octantes cobrem o plano completo sem sobreposição:
 *   0: E-NE  1: N-NE  2: N-NO  3: O-NO
 *   4: O-SO  5: S-SO  6: S-SE  7: E-SE
 * ========================================================================= */
static const int OCT_TRANSFORM[8][4] = {
    { 1,  0,  0,  1},  /* octante 0: E-NE  */
    { 0,  1,  1,  0},  /* octante 1: N-NE  */
    { 0, -1,  1,  0},  /* octante 2: N-NO  */
    {-1,  0,  0,  1},  /* octante 3: O-NO  */
    {-1,  0,  0, -1},  /* octante 4: O-SO  */
    { 0, -1, -1,  0},  /* octante 5: S-SO  */
    { 0,  1, -1,  0},  /* octante 6: S-SE  */
    { 1,  0,  0, -1},  /* octante 7: E-SE  */
};

/* =========================================================================
 * Shadow — faixa angular ocluída [start, end]
 *
 * start e end são slopes normalizados: slope = dr / (dc + 0.5)
 * Mantidos em lista estática por octante; capacidade conservadora:
 * no pior caso (linha de paredes em zigzag) temos ~raio sombras ativas.
 * ========================================================================= */
#define MAX_SHADOWS 128

struct Shadow {
    float start;
    float end;
};

/* =========================================================================
 * _slope_low / _slope_high
 *
 * Calculam os slopes inferior e superior de um tile (dc, dr) no espaço
 * do octante.  O tile ocupa a faixa [slope_low, slope_high] no ângulo.
 *
 * Convenção: dc é sempre >= 1 nesta implementação (anel de distância).
 * dr varia de 0 até dc (borda inferior do octante).
 * ========================================================================= */
static inline float _slope_low (int dc, int dr) {
    return (float)dr / (float)(dc + 1);  /* borda inferior do tile */
}
static inline float _slope_high(int dc, int dr) {
    return (float)(dr + 1) / (float)dc;  /* borda superior do tile */
}

/* =========================================================================
 * _shadows_insert — insere uma nova sombra e mescla sobreposições
 *
 * A lista é mantida ordenada por start.  Após inserção, pares adjacentes
 * sobrepostos são fundidos para manter a lista compacta.
 * ========================================================================= */
static int _shadows_insert(Shadow *shadows, int *count,
                            float start, float end)
{
    if (*count >= MAX_SHADOWS) return 0;  /* lista cheia — conservador */

    /* Encontra posição de inserção ordenada */
    int pos = *count;
    for (int i = 0; i < *count; i++) {
        if (start < shadows[i].start) { pos = i; break; }
    }

    /* Desloca elementos para abrir espaço */
    for (int i = *count; i > pos; i--) {
        shadows[i] = shadows[i - 1];
    }
    shadows[pos] = {start, end};
    (*count)++;

    /* Mescla sobreposições: percorre de pos para frente e para trás */
    /* Para trás: funde com o anterior se sobrepostos */
    if (pos > 0 && shadows[pos - 1].end >= shadows[pos].start) {
        shadows[pos - 1].end = shadows[pos - 1].end > shadows[pos].end
                                ? shadows[pos - 1].end
                                : shadows[pos].end;
        for (int i = pos; i < *count - 1; i++) shadows[i] = shadows[i + 1];
        (*count)--;
        pos--;
    }

    /* Para frente: funde com próximos sobrepostos */
    while (pos + 1 < *count && shadows[pos].end >= shadows[pos + 1].start) {
        shadows[pos].end = shadows[pos].end > shadows[pos + 1].end
                            ? shadows[pos].end
                            : shadows[pos + 1].end;
        for (int i = pos + 1; i < *count - 1; i++) shadows[i] = shadows[i + 1];
        (*count)--;
    }

    return 1;
}

/* =========================================================================
 * _shadows_covers — verifica se a faixa [start, end] está completamente
 * coberta por alguma sombra na lista.
 * ========================================================================= */
static inline int _shadows_covers(const Shadow *shadows, int count,
                                   float start, float end)
{
    for (int i = 0; i < count; i++) {
        if (shadows[i].start <= start && shadows[i].end >= end) return 1;
        if (shadows[i].start > start) break;  /* lista ordenada: early-exit */
    }
    return 0;
}

/* =========================================================================
 * _fov_octant — processa um único octante
 *
 * Varre os anéis dc = 1..radius, dr = 0..dc.
 * Para cada tile:
 *   1. Transforma (dc, dr) → (col, row) do mapa.
 *   2. Verifica se está dentro do mapa e do raio circular.
 *   3. Se não coberto por sombra, marca como visível.
 *   4. Se for parede, adiciona sombra à lista.
 * ========================================================================= */
static void _fov_octant(FovParams *p, int oct, Shadow *shadows, int *shadow_count)
{
    const int *m = OCT_TRANSFORM[oct];
    const float r2 = (float)(p->radius * p->radius);

    for (int dc = 1; dc <= p->radius; dc++) {
        /* Se a faixa completa do anel já está sombreada, encerra o octante */
        if (_shadows_covers(shadows, *shadow_count, 0.0f, 1.0f)) break;

        for (int dr = 0; dr <= dc; dr++) {
            int col = p->origin_col + dc * m[0] + dr * m[1];
            int row = p->origin_row + dc * m[2] + dr * m[3];

            /* Descarta tiles fora do mapa */
            if (col < 0 || col >= p->map_cols) continue;
            if (row < 0 || row >= p->map_rows) continue;

            /* Descarta tiles além do raio circular */
            if (_dist2(dc, dr) > r2) continue;

            float sl = _slope_low (dc, dr);
            float sh = _slope_high(dc, dr);

            int covered = _shadows_covers(shadows, *shadow_count, sl, sh);

            if (!covered) {
                int idx = row * p->map_cols + col;
                float vis_val;

                if (p->mode == FOV_MODE_SMOOTH) {
                    /* Falloff linear: 1.0 na origem, 0.0 na borda do raio */
                    float dist = sqrtf(_dist2(dc, dr));
                    vis_val = _fclamp(1.0f - dist / (float)p->radius, 0.0f, 1.0f);
                } else {
                    vis_val = ENGINE_FOV_VISIBLE;
                }

                /* Fog-of-war: só sobrescreve se o novo valor é maior */
                if (p->mode == FOV_MODE_FOG_WAR) {
                    if (vis_val > p->vis[idx]) p->vis[idx] = vis_val;
                } else {
                    p->vis[idx] = vis_val;
                }
            }

            /* Se o tile bloqueia visão, acrescenta sombra */
            if (p->is_blocking(col, row, p->user_data)) {
                _shadows_insert(shadows, shadow_count, sl, sh);
            }
        }
    }
}

/* =========================================================================
 * engine_fov_compute — ponto de entrada público
 * ========================================================================= */
void engine_fov_compute(FovParams *params)
{
    if (!params || !params->vis || !params->is_blocking) return;
    if (params->radius <= 0) return;

    const int cols = params->map_cols;
    const int rows = params->map_rows;
    const int oc   = params->origin_col;
    const int or_  = params->origin_row;
    const int r    = params->radius;

    /* --- Reset da área de influência ------------------------------------ */
    if (params->mode == FOV_MODE_FOG_WAR) {
        /*
         * Fog-of-war: reduz tiles visíveis para ENGINE_FOV_EXPLORED
         * dentro do bounding-box antes de recomputar.
         * Tiles já em FOV_EXPLORED ou FOV_DARK permanecem intactos fora do box.
         */
        int c0 = oc - r; if (c0 < 0) c0 = 0;
        int c1 = oc + r; if (c1 >= cols) c1 = cols - 1;
        int r0 = or_ - r; if (r0 < 0) r0 = 0;
        int r1 = or_ + r; if (r1 >= rows) r1 = rows - 1;

        for (int row = r0; row <= r1; row++) {
            for (int col = c0; col <= c1; col++) {
                int idx = row * cols + col;
                if (params->vis[idx] >= ENGINE_FOV_VISIBLE) {
                    params->vis[idx] = ENGINE_FOV_EXPLORED;
                }
            }
        }
    } else {
        /*
         * Basic / Smooth: zera o bounding-box completo para garantir
         * que tiles fora do raio fiquem escuros.
         */
        int c0 = oc - r; if (c0 < 0) c0 = 0;
        int c1 = oc + r; if (c1 >= cols) c1 = cols - 1;
        int r0 = or_ - r; if (r0 < 0) r0 = 0;
        int r1 = or_ + r; if (r1 >= rows) r1 = rows - 1;

        for (int row = r0; row <= r1; row++) {
            for (int col = c0; col <= c1; col++) {
                params->vis[row * cols + col] = ENGINE_FOV_DARK;
            }
        }
    }

    /* --- Tile de origem: sempre visível --------------------------------- */
    if (oc >= 0 && oc < cols && or_ >= 0 && or_ < rows) {
        params->vis[or_ * cols + oc] = ENGINE_FOV_VISIBLE;
    }

    /* --- Shadowcasting em 8 octantes ------------------------------------ */
    for (int oct = 0; oct < 8; oct++) {
        Shadow shadows[MAX_SHADOWS];
        int    shadow_count = 0;
        _fov_octant(params, oct, shadows, &shadow_count);
    }
}

/* =========================================================================
 * engine_fov_draw_shadow — sobrepõe escuridão sobre o tilemap
 * ========================================================================= */
void engine_fov_draw_shadow(Engine *e,
                             const float *vis,
                             int map_cols, int map_rows,
                             int tile_w,  int tile_h,
                             int offset_x, int offset_y,
                             int dark_r, int dark_g, int dark_b)
{
    if (!e || !vis) return;

    for (int row = 0; row < map_rows; row++) {
        for (int col = 0; col < map_cols; col++) {
            float v = vis[row * map_cols + col];
            float darkness = 1.0f - v;  /* 0 = iluminado, 1 = escuro total  */

            if (darkness <= 0.0f) continue;  /* tile totalmente visível: skip */

            int px = offset_x + col * tile_w;
            int py = offset_y + row * tile_h;

            engine_draw_overlay(e, px, py, tile_w, tile_h,
                                dark_r, dark_g, dark_b, darkness);
        }
    }
}

/* =========================================================================
 * engine_fov_draw_debug — visualiza octantes e raio (dev-only)
 * ========================================================================= */
void engine_fov_draw_debug(Engine *e,
                            int origin_col, int origin_row,
                            int radius,
                            int tile_w, int tile_h,
                            int offset_x, int offset_y)
{
    if (!e) return;

    /* Centro do tile de origem em pixels */
    int cx = offset_x + origin_col * tile_w + tile_w / 2;
    int cy = offset_y + origin_row * tile_h + tile_h / 2;
    int pr = radius * ((tile_w + tile_h) / 2);  /* raio em pixels (aprox.)  */

    /* Cores distintas para cada par de octantes opostos */
    static const int oct_colors[8][3] = {
        {255, 80, 80},   /* 0: E-NE  vermelho      */
        {255,180, 50},   /* 1: N-NE  laranja       */
        { 80,220, 80},   /* 2: N-NO  verde         */
        { 50,180,255},   /* 3: O-NO  azul-claro    */
        {180, 80,255},   /* 4: O-SO  roxo          */
        {255, 80,200},   /* 5: S-SO  rosa          */
        { 80,255,210},   /* 6: S-SE  ciano         */
        {230,230, 50},   /* 7: E-SE  amarelo       */
    };

    /* Ângulos de divisão de octante (0°, 45°, 90°, ..., 315°) */
    for (int i = 0; i < 8; i++) {
        double angle_rad = i * 3.14159265 / 4.0;
        int ex = cx + (int)(pr * cos(angle_rad));
        int ey = cy + (int)(pr * sin(angle_rad));
        engine_draw_line(e, cx, cy, ex, ey,
                         oct_colors[i][0], oct_colors[i][1], oct_colors[i][2], 1);
    }

    /* Círculo do raio (aproximado por 32 segmentos) */
    int px_prev = cx + pr, py_prev = cy;
    for (int i = 1; i <= 32; i++) {
        double a = i * 2.0 * 3.14159265 / 32.0;
        int px_cur = cx + (int)(pr * cos(a));
        int py_cur = cy + (int)(pr * sin(a));
        engine_draw_line(e, px_prev, py_prev, px_cur, py_cur,
                         200, 200, 200, 1);
        px_prev = px_cur;
        py_prev = py_cur;
    }

    /* Marca o tile de origem */
    engine_draw_circle(e, cx, cy, tile_w / 3,
                       255, 255, 0, /*filled=*/1);
}
