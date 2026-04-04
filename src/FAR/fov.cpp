//fov.cpp — Field of View 2D via Shadowcasting.
/*
 * Algoritmo: Recursive Shadowcasting (RogueBasin / Björn Permutations).
 *
 * Ideia central:
 *   O plano é dividido em 8 octantes de 45°. Em cada octante, o algoritmo
 *   varre anéis de tiles (distância 1..raio) e mantém uma lista de "sombras"
 *   — faixas angulares já bloqueadas por paredes. Um tile é visível se seu
 *   intervalo angular não está completamente coberto pela lista de sombras.
 *
 * Implementação iterativa (sem recursão):
 *   A versão clássica usa recursão por faixa de slope. Esta versão usa stack
 *   explícito (ShadowFrame) para evitar stack overflow em mapas abertos.
 *
 * Transformação de octante:
 *   O algoritmo resolve o octante 0 e mapeia os outros 7 via OCT_TRANSFORM[][4].
 *   Cada (dc, dr) no espaço do octante é convertido para (col, row) do mapa.
 */

#include "fov.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>

/* =========================================================================
 * Helpers
 * ========================================================================= */

static inline float _fclamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float _dist2(int dc, int dr) {
    return (float)(dc * dc + dr * dr);
}

/* =========================================================================
 * Tabela de transformação de octante
 *
 * Cada linha: { mul_dc→col, mul_dr→col, mul_dc→row, mul_dr→row }
 *
 *   col = origin_col + dc*m[0] + dr*m[1]
 *   row = origin_row + dc*m[2] + dr*m[3]
 *
 * Os 8 octantes cobrem o plano completo sem sobreposição.
 * ========================================================================= */
static const int OCT_TRANSFORM[8][4] = {
    { 1,  0,  0,  1},  /* 0: E-NE  */
    { 0,  1,  1,  0},  /* 1: N-NE  */
    { 0, -1,  1,  0},  /* 2: N-NO  */
    {-1,  0,  0,  1},  /* 3: O-NO  */
    {-1,  0,  0, -1},  /* 4: O-SO  */
    { 0, -1, -1,  0},  /* 5: S-SO  */
    { 0,  1, -1,  0},  /* 6: S-SE  */
    { 1,  0,  0, -1},  /* 7: E-SE  */
};

/* =========================================================================
 * Shadow — faixa angular ocluída [start, end] (slopes normalizados).
 *
 * slope = dr / (dc + 0.5) — normalizado para [0, 1] dentro do octante.
 * Lista mantida ordenada; capacidade conservadora (~raio sombras ativas).
 * ========================================================================= */
#define MAX_SHADOWS 128

struct Shadow { float start, end; };

/* Slopes inferior e superior do tile (dc, dr) no espaço do octante. */
static inline float _slope_low (int dc, int dr) { return (float)dr / (float)(dc + 1); }
static inline float _slope_high(int dc, int dr) { return (float)(dr + 1) / (float)dc; }

/* =========================================================================
 * _shadows_insert — insere uma sombra e mescla sobreposições.
 *
 * A lista é mantida ordenada por start. Pares adjacentes sobrepostos são
 * fundidos para manter a lista compacta e o early-exit eficiente.
 * ========================================================================= */
static int _shadows_insert(Shadow *shadows, int *count, float start, float end)
{
    if (*count >= MAX_SHADOWS) return 0;

    /* Posição de inserção ordenada */
    int pos = *count;
    for (int i = 0; i < *count; i++) {
        if (start < shadows[i].start) { pos = i; break; }
    }

    /* Abre espaço */
    for (int i = *count; i > pos; i--) shadows[i] = shadows[i-1];
    shadows[pos] = {start, end};
    (*count)++;

    /* Mescla com anterior se sobreposto */
    if (pos > 0 && shadows[pos-1].end >= shadows[pos].start) {
        shadows[pos-1].end = shadows[pos-1].end > shadows[pos].end
                           ? shadows[pos-1].end : shadows[pos].end;
        for (int i = pos; i < *count-1; i++) shadows[i] = shadows[i+1];
        (*count)--;
        pos--;
    }

    /* Mescla com próximos sobrepostos */
    while (pos+1 < *count && shadows[pos].end >= shadows[pos+1].start) {
        shadows[pos].end = shadows[pos].end > shadows[pos+1].end
                         ? shadows[pos].end : shadows[pos+1].end;
        for (int i = pos+1; i < *count-1; i++) shadows[i] = shadows[i+1];
        (*count)--;
    }

    return 1;
}

/* Retorna 1 se [start, end] está completamente coberto por alguma sombra. */
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
 * _fov_octant — processa um único octante.
 *
 * Para cada tile em dc=1..radius, dr=0..dc:
 *   1. Transforma (dc,dr) → (col,row) do mapa.
 *   2. Descarta se fora do mapa ou do raio circular.
 *   3. Se não coberto por sombra, marca visível.
 *   4. Se for parede, adiciona sombra.
 * ========================================================================= */
static void _fov_octant(FovParams *p, int oct, Shadow *shadows, int *shadow_count)
{
    const int *m = OCT_TRANSFORM[oct];
    const float r2 = (float)(p->radius * p->radius);

    for (int dc = 1; dc <= p->radius; dc++) {
        /* Anel completamente na sombra: encerra o octante */
        if (_shadows_covers(shadows, *shadow_count, 0.0f, 1.0f)) break;

        for (int dr = 0; dr <= dc; dr++) {
            int col = p->origin_col + dc*m[0] + dr*m[1];
            int row = p->origin_row + dc*m[2] + dr*m[3];

            if (col < 0 || col >= p->map_cols) continue;
            if (row < 0 || row >= p->map_rows) continue;
            if (_dist2(dc, dr) > r2)           continue;

            float sl = _slope_low (dc, dr);
            float sh = _slope_high(dc, dr);

            if (!_shadows_covers(shadows, *shadow_count, sl, sh)) {
                int idx = row * p->map_cols + col;
                float vis_val;

                if (p->mode == FOV_MODE_SMOOTH) {
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

            if (p->is_blocking(col, row, p->user_data))
                _shadows_insert(shadows, shadow_count, sl, sh);
        }
    }
}

/* =========================================================================
 * engine_fov_compute — ponto de entrada público.
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

    /* --- Reset da área de influência (bounding-box do raio) -------------- */
    int c0 = oc - r; if (c0 < 0)    c0 = 0;
    int c1 = oc + r; if (c1 >= cols) c1 = cols - 1;
    int r0 = or_ - r; if (r0 < 0)   r0 = 0;
    int r1 = or_ + r; if (r1 >= rows) r1 = rows - 1;

    if (params->mode == FOV_MODE_FOG_WAR) {
        /* Tiles visíveis passam para EXPLORED; tiles fora do box preservados */
        for (int row = r0; row <= r1; row++)
            for (int col = c0; col <= c1; col++) {
                int idx = row * cols + col;
                if (params->vis[idx] >= ENGINE_FOV_VISIBLE)
                    params->vis[idx] = ENGINE_FOV_EXPLORED;
            }
    } else {
        /* Basic / Smooth: zera o box inteiro */
        for (int row = r0; row <= r1; row++)
            for (int col = c0; col <= c1; col++)
                params->vis[row * cols + col] = ENGINE_FOV_DARK;
    }

    /* --- Origem: sempre visível ------------------------------------------ */
    if (oc >= 0 && oc < cols && or_ >= 0 && or_ < rows)
        params->vis[or_ * cols + oc] = ENGINE_FOV_VISIBLE;

    /* --- Shadowcasting nos 8 octantes ------------------------------------ */
    for (int oct = 0; oct < 8; oct++) {
        Shadow shadows[MAX_SHADOWS];
        int    shadow_count = 0;
        _fov_octant(params, oct, shadows, &shadow_count);
    }
}

/* =========================================================================
 * engine_fov_draw_shadow — sobrepõe escuridão sobre o tilemap.
 *
 * Para cada tile, desenha um overlay com alpha = (1 - vis[tile]).
 * Tiles totalmente visíveis são pulados (sem draw call desnecessário).
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
            float darkness = 1.0f - vis[row * map_cols + col];
            if (darkness <= 0.0f) continue;  /* tile iluminado: sem draw */

            int px = offset_x + col * tile_w;
            int py = offset_y + row * tile_h;
            engine_draw_overlay(e, px, py, tile_w, tile_h,
                                dark_r, dark_g, dark_b, darkness);
        }
    }
}

/* =========================================================================
 * engine_fov_draw_debug — visualiza octantes e raio (apenas em dev).
 *
 * Desenha 8 linhas coloridas (uma por octante) e o círculo do raio.
 * ========================================================================= */
void engine_fov_draw_debug(Engine *e,
                            int origin_col, int origin_row,
                            int radius,
                            int tile_w, int tile_h,
                            int offset_x, int offset_y)
{
    if (!e) return;

    int cx = offset_x + origin_col * tile_w + tile_w / 2;
    int cy = offset_y + origin_row * tile_h + tile_h / 2;
    int pr = radius * ((tile_w + tile_h) / 2);  /* raio em pixels */

    static const int oct_colors[8][3] = {
        {255, 80, 80},   /* 0: E-NE  vermelho   */
        {255,180, 50},   /* 1: N-NE  laranja    */
        { 80,220, 80},   /* 2: N-NO  verde      */
        { 50,180,255},   /* 3: O-NO  azul-claro */
        {180, 80,255},   /* 4: O-SO  roxo       */
        {255, 80,200},   /* 5: S-SO  rosa       */
        { 80,255,210},   /* 6: S-SE  ciano      */
        {230,230, 50},   /* 7: E-SE  amarelo    */
    };

    /* Linhas de divisão dos octantes (a cada 45°) */
    for (int i = 0; i < 8; i++) {
        double a = i * 3.14159265 / 4.0;
        int ex = cx + (int)(pr * cos(a));
        int ey = cy + (int)(pr * sin(a));
        engine_draw_line(e, cx, cy, ex, ey,
                         oct_colors[i][0], oct_colors[i][1], oct_colors[i][2], 1);
    }

    /* Círculo do raio (32 segmentos) */
    int px_prev = cx + pr, py_prev = cy;
    for (int i = 1; i <= 32; i++) {
        double a = i * 2.0 * 3.14159265 / 32.0;
        int px_cur = cx + (int)(pr * cos(a));
        int py_cur = cy + (int)(pr * sin(a));
        engine_draw_line(e, px_prev, py_prev, px_cur, py_cur, 200, 200, 200, 1);
        px_prev = px_cur;
        py_prev = py_cur;
    }

    /* Marca a origem */
    engine_draw_circle(e, cx, cy, tile_w / 3, 255, 255, 0, /*filled=*/1);
}