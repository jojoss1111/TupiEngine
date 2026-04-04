/* =============================================================================
 * Engine_Events.cpp — Colisão, Spatial Grid e Input (teclado / mouse)
 *
 * Este arquivo contém:
 *   • Colisão AABB (_get_hitbox, _aabb, engine_check_collision*)
 *   • Layers de colisão (collision_layer / collision_mask) e sensores
 *   • Spatial Grid dinâmico com std::unordered_map — suporta coords negativas
 *     e mapa virtualmente ilimitado (sem COLS/ROWS fixos)
 *   • Input de teclado (engine_key_down/pressed/released)
 *   • Input de mouse (engine_mouse_down/pressed/released/pos/scroll)
 *
 * Dependências do Engine.cpp principal:
 *   • _oid_valid, _get_hitbox, RENDERER, _name_to_keysym, RendererGL
 * ============================================================================= */

#include "Engine.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unordered_map>

/* --- Helpers compartilhados ------------------------------------------------ */

/* Validador de índice de objeto — replicado aqui para evitar dependência de TU */
static inline bool _oid_valid(const Engine *e, int oid)
{
    return oid >= 0 && oid < e->object_count;
}

/* Macro de acesso ao renderer concreto */
#ifndef RENDERER
#  include "Renderizador/IRenderer.hpp"
#  define RENDERER(e)  (static_cast<IRenderer *>((e)->renderer_impl))
#endif

/* --- Colisão AABB ---------------------------------------------------------- */

/*
 * _get_hitbox() — retorna a hitbox efetiva do objeto em coordenadas de mundo.
 * Prioridade: hitbox customizada > tile_w/h > sprite width/height > width/height.
 */
static void _get_hitbox(const Engine *e, int oid,
                         int *hx, int *hy, int *hw, int *hh)
{
    const GameObject &o = e->objects[oid];
    if (o.hitbox.enabled) {
        *hx = o.x + o.hitbox.offset_x;
        *hy = o.y + o.hitbox.offset_y;
        *hw = o.hitbox.width;
        *hh = o.hitbox.height;
    } else {
        *hx = o.x;
        *hy = o.y;
        if (o.use_tile) {
            *hw = o.tile_w;
            *hh = o.tile_h;
        } else if (o.sprite_id >= 0 && o.sprite_id < e->sprite_count) {
            *hw = e->sprites[o.sprite_id].width;
            *hh = e->sprites[o.sprite_id].height;
        } else {
            *hw = o.width;
            *hh = o.height;
        }
    }
}

/* Teste AABB puro — usado internamente por todas as funções de colisão */
static inline int _aabb(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh)
{
    return (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by) ? 1 : 0;
}

/*
 * _layers_interact() — verifica se dois objetos interagem por máscara de bits.
 *
 * A colisão é bidirecional: A deve reconhecer o layer de B E B deve reconhecer
 * o layer de A.  Objetos com collision_mask == 0 (nunca configurados) assumem
 * ENGINE_LAYER_ALL para manter compatibilidade com código que não usa layers.
 */
static inline int _layers_interact(const Engine *e, int oid1, int oid2)
{
    const GameObject &a = e->objects[oid1];
    const GameObject &b = e->objects[oid2];

    const unsigned int maskA = (a.collision_mask  != 0) ? a.collision_mask  : ENGINE_LAYER_ALL;
    const unsigned int maskB = (b.collision_mask  != 0) ? b.collision_mask  : ENGINE_LAYER_ALL;
    const unsigned int layA  = (a.collision_layer != 0) ? a.collision_layer : ENGINE_LAYER_DEFAULT;
    const unsigned int layB  = (b.collision_layer != 0) ? b.collision_layer : ENGINE_LAYER_DEFAULT;

    return ((layA & maskB) != 0) && ((layB & maskA) != 0);
}

int engine_check_collision(Engine *e, int oid1, int oid2)
{
    if (!_oid_valid(e, oid1) || !_oid_valid(e, oid2))           return 0;
    if (!e->objects[oid1].active || !e->objects[oid2].active)   return 0;
    if (!_layers_interact(e, oid1, oid2))                        return 0;
    int ax, ay, aw, ah, bx, by, bw, bh;
    _get_hitbox(e, oid1, &ax, &ay, &aw, &ah);
    _get_hitbox(e, oid2, &bx, &by, &bw, &bh);
    return _aabb(ax, ay, aw, ah, bx, by, bw, bh);
}

int engine_check_collision_rect(Engine *e, int oid, int rx, int ry, int rw, int rh)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return 0;
    int ax, ay, aw, ah;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return _aabb(ax, ay, aw, ah, rx, ry, rw, rh);
}

int engine_check_collision_point(Engine *e, int oid, int px, int py)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return 0;
    int ax, ay, aw, ah;
    _get_hitbox(e, oid, &ax, &ay, &aw, &ah);
    return (px >= ax && px < ax + aw && py >= ay && py < ay + ah) ? 1 : 0;
}

/* --- API de layers de colisão e sensores ---------------------------------- */

void engine_set_collision_layer(Engine *e, int oid, unsigned int layer, unsigned int mask)
{
    if (!_oid_valid(e, oid)) return;
    e->objects[oid].collision_layer = layer;
    e->objects[oid].collision_mask  = mask;
}

void engine_set_sensor(Engine *e, int oid, int is_sensor)
{
    if (!_oid_valid(e, oid)) return;
    e->objects[oid].is_sensor = is_sensor ? 1 : 0;
}

int engine_is_sensor(Engine *e, int oid)
{
    if (!_oid_valid(e, oid)) return 0;
    return e->objects[oid].is_sensor;
}

/* =============================================================================
 * Spatial Grid dinâmico — implementação
 *
 * Arquitetura:
 *   • std::unordered_map<int64_t, SpatialCell> alocado no heap via new/delete.
 *     O ponteiro vive em SpatialGrid::cells_map (void*).
 *   • Chave int64_t = ((int64_t)(int32_t)col << 32) | (uint32_t)(int32_t)row
 *     → suporta qualquer col/row no intervalo de int32_t, inclusive negativos.
 *   • Células são criadas sob demanda e destruídas em engine_sgrid_destroy().
 *   • SpatialObjEntry rastreia as chaves das células onde cada objeto está
 *     inscrito (máximo ENGINE_SGRID_OBJ_MAX_CELLS = 4).
 *   • Toda a lógica de filtro por layer e sensor acontece na camada de query,
 *     não na inserção — o grid armazena todos os objetos sem distinção.
 *
 * Complexidade:
 *   insert/remove: O(k)   k = células cobertas (≤ 4)
 *   query:         O(k·b) b = ocupação média da célula
 *   vs. bruto:     O(N)   N = ENGINE_MAX_OBJECTS
 * ============================================================================= */

/* Alias para facilitar a leitura */
using CellMap = std::unordered_map<int64_t, SpatialCell>;

/* Ponteiro tipado a partir de void* */
static inline CellMap *_cm(SpatialGrid *g)
{
    return static_cast<CellMap *>(g->cells_map);
}

/* Codifica (col, row) em uma chave int64_t; funciona com negativos */
static inline int64_t _sg_key(int col, int row)
{
    return ((int64_t)(int32_t)col << 32) | (uint32_t)(int32_t)row;
}

/* Converte coordenada de mundo para índice de coluna/linha sem clamping */
static inline int _sg_col(const SpatialGrid *g, int wx)
{
    /* floor division correta para negativos */
    return (wx >= 0) ? (wx / g->cell_size)
                     : (wx / g->cell_size - (wx % g->cell_size != 0 ? 1 : 0));
}
static inline int _sg_row(const SpatialGrid *g, int wy)
{
    return (wy >= 0) ? (wy / g->cell_size)
                     : (wy / g->cell_size - (wy % g->cell_size != 0 ? 1 : 0));
}

/* Remove oid de uma célula específica (busca linear; células pequenas) */
static void _sg_cell_remove(SpatialCell *cell, int oid)
{
    for (int i = 0; i < cell->count; ++i) {
        if (cell->oids[i] == oid) {
            cell->oids[i] = cell->oids[--cell->count];
            return;
        }
    }
}

/* Insere oid numa célula; retorna 1 se ok, 0 se cheia */
static int _sg_cell_insert(SpatialCell *cell, int oid)
{
    if (cell->count >= ENGINE_SGRID_BUCKET_CAP) return 0;
    cell->oids[cell->count++] = oid;
    return 1;
}

/*
 * _sg_insert_object() — insere o objeto em todas as células que sua hitbox cobre.
 */
void _sg_insert_object(Engine *e, int oid)
{
    SpatialGrid *g = &e->sgrid;
    CellMap     *cm = _cm(g);
    SpatialObjEntry *entry = &g->obj_cells[oid];
    entry->count = 0;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);

    const int c0 = _sg_col(g, hx);
    const int c1 = _sg_col(g, hx + hw - 1);
    const int r0 = _sg_row(g, hy);
    const int r1 = _sg_row(g, hy + hh - 1);

    for (int r = r0; r <= r1 && entry->count < ENGINE_SGRID_OBJ_MAX_CELLS; ++r) {
        for (int c = c0; c <= c1 && entry->count < ENGINE_SGRID_OBJ_MAX_CELLS; ++c) {
            const int64_t key = _sg_key(c, r);
            SpatialCell   &cell = (*cm)[key];   /* cria se não existir */
            if (_sg_cell_insert(&cell, oid)) {
                entry->cell_keys[entry->count++] = key;
            } else {
                fprintf(stderr,
                    "SpatialGrid: célula [%d,%d] cheia (cap=%d). "
                    "Aumente ENGINE_SGRID_BUCKET_CAP.\n",
                    c, r, ENGINE_SGRID_BUCKET_CAP);
            }
        }
    }
}

/* Remove o objeto de todas as células onde está registrado */
void _sg_remove_object(Engine *e, int oid)
{
    SpatialGrid *g = &e->sgrid;
    CellMap     *cm = _cm(g);
    SpatialObjEntry *entry = &g->obj_cells[oid];
    for (int i = 0; i < entry->count; ++i) {
        auto it = cm->find(entry->cell_keys[i]);
        if (it != cm->end()) {
            _sg_cell_remove(&it->second, oid);
            /* remove célula vazia para não acumular lixo no mapa */
            if (it->second.count == 0)
                cm->erase(it);
        }
    }
    entry->count = 0;
}

/* --- API pública do Spatial Grid ------------------------------------------ */

void engine_sgrid_init(Engine *e, int cell_size)
{
    SpatialGrid *g = &e->sgrid;
    /* libera mapa anterior se existir */
    if (g->cells_map) {
        delete static_cast<CellMap *>(g->cells_map);
        g->cells_map = nullptr;
    }
    memset(g, 0, sizeof(SpatialGrid));
    g->cells_map = new CellMap();
    g->cell_size = (cell_size > 0) ? cell_size : ENGINE_SGRID_CELL_SIZE;
    g->enabled   = 1;
    g->dirty     = 0;
}

void engine_sgrid_destroy(Engine *e)
{
    SpatialGrid *g = &e->sgrid;
    if (g->cells_map) {
        delete static_cast<CellMap *>(g->cells_map);
        g->cells_map = nullptr;
    }
    memset(g, 0, sizeof(SpatialGrid));
    /* enabled permanece 0 após memset */
}

void engine_sgrid_rebuild(Engine *e)
{
    SpatialGrid *g = &e->sgrid;
    if (!g->enabled) return;

    /* Limpa o mapa e as entradas de objetos */
    _cm(g)->clear();
    for (int i = 0; i < ENGINE_MAX_OBJECTS; ++i)
        g->obj_cells[i].count = 0;

    /* Reinsere todos os objetos ativos */
    for (int i = 0; i < e->object_count; ++i) {
        if (e->objects[i].active)
            _sg_insert_object(e, i);
    }
    g->dirty = 0;
}

void engine_sgrid_insert_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid) || !e->objects[oid].active) return;
    _sg_insert_object(e, oid);
}

void engine_sgrid_remove_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid)) return;
    _sg_remove_object(e, oid);
}

void engine_sgrid_update_object(Engine *e, int oid)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid)) return;
    _sg_remove_object(e, oid);
    if (e->objects[oid].active)
        _sg_insert_object(e, oid);
}

/*
 * _sgrid_query_rect_internal() — acumula candidatos numa área retangular.
 *
 * Usa bitmask stack-allocated para deduplicação em O(1):
 *   seen[oid >> 5] & (1 << (oid & 31))
 * ENGINE_MAX_OBJECTS = 256 → seen[8] (32 bytes na stack).
 */
static int _sgrid_query_rect_internal(Engine *e,
                                       int x, int y, int w, int h,
                                       int *out_oids, int cap)
{
    SpatialGrid *g = &e->sgrid;
    CellMap     *cm = _cm(g);

    const int c0 = _sg_col(g, x);
    const int c1 = _sg_col(g, x + w - 1);
    const int r0 = _sg_row(g, y);
    const int r1 = _sg_row(g, y + h - 1);

    unsigned int seen[(ENGINE_MAX_OBJECTS + 31) / 32] = {};
    int found = 0;

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            auto it = cm->find(_sg_key(c, r));
            if (it == cm->end()) continue;
            const SpatialCell &cell = it->second;
            for (int k = 0; k < cell.count; ++k) {
                const int oid = cell.oids[k];
                if (oid < 0 || oid >= ENGINE_MAX_OBJECTS) continue;
                const unsigned int mask = 1u << (oid & 31);
                if (seen[oid >> 5] & mask) continue;
                seen[oid >> 5] |= mask;
                if (found < cap) out_oids[found++] = oid;
            }
        }
    }
    return found;
}

int engine_sgrid_query_rect(Engine *e, int x, int y, int w, int h,
                             int *out_oids, int cap)
{
    if (!e->sgrid.enabled || cap <= 0) return 0;
    return _sgrid_query_rect_internal(e, x, y, w, h, out_oids, cap);
}

int engine_sgrid_query_object(Engine *e, int oid, int *out_oids, int cap)
{
    if (!e->sgrid.enabled || !_oid_valid(e, oid) || cap <= 0) return 0;
    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);
    return _sgrid_query_rect_internal(e, hx, hy, hw, hh, out_oids, cap);
}

int engine_sgrid_query_point(Engine *e, int px, int py,
                              int *out_oids, int cap)
{
    if (!e->sgrid.enabled || cap <= 0) return 0;
    return _sgrid_query_rect_internal(e, px, py, 1, 1, out_oids, cap);
}

int engine_sgrid_first_collision(Engine *e, int oid)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active) return -1;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);

    if (e->sgrid.enabled) {
        int candidates[ENGINE_SGRID_BUCKET_CAP * ENGINE_SGRID_OBJ_MAX_CELLS];
        const int n = _sgrid_query_rect_internal(e, hx, hy, hw, hh,
                                                   candidates,
                                                   (int)(sizeof(candidates)/sizeof(candidates[0])));
        for (int i = 0; i < n; ++i) {
            const int other = candidates[i];
            if (other == oid || !e->objects[other].active) continue;
            if (!_layers_interact(e, oid, other))          continue;
            int bx, by, bw, bh;
            _get_hitbox(e, other, &bx, &by, &bw, &bh);
            if (_aabb(hx, hy, hw, hh, bx, by, bw, bh)) return other;
        }
        return -1;
    }

    /* Fallback bruto quando o grid está desativado */
    for (int i = 0; i < e->object_count; ++i) {
        if (i == oid || !e->objects[i].active)  continue;
        if (!_layers_interact(e, oid, i))        continue;
        int bx, by, bw, bh;
        _get_hitbox(e, i, &bx, &by, &bw, &bh);
        if (_aabb(hx, hy, hw, hh, bx, by, bw, bh)) return i;
    }
    return -1;
}

int engine_sgrid_all_collisions(Engine *e, int oid, int *out_oids, int cap)
{
    if (!_oid_valid(e, oid) || !e->objects[oid].active || cap <= 0) return 0;

    int hx, hy, hw, hh;
    _get_hitbox(e, oid, &hx, &hy, &hw, &hh);
    int found = 0;

    if (e->sgrid.enabled) {
        int candidates[ENGINE_SGRID_BUCKET_CAP * ENGINE_SGRID_OBJ_MAX_CELLS];
        const int n = _sgrid_query_rect_internal(e, hx, hy, hw, hh,
                                                   candidates,
                                                   (int)(sizeof(candidates)/sizeof(candidates[0])));
        for (int i = 0; i < n && found < cap; ++i) {
            const int other = candidates[i];
            if (other == oid || !e->objects[other].active) continue;
            if (!_layers_interact(e, oid, other))          continue;
            int bx, by, bw, bh;
            _get_hitbox(e, other, &bx, &by, &bw, &bh);
            if (_aabb(hx, hy, hw, hh, bx, by, bw, bh))
                out_oids[found++] = other;
        }
        return found;
    }

    /* Fallback bruto */
    for (int i = 0; i < e->object_count && found < cap; ++i) {
        if (i == oid || !e->objects[i].active)  continue;
        if (!_layers_interact(e, oid, i))        continue;
        int bx, by, bw, bh;
        _get_hitbox(e, i, &bx, &by, &bw, &bh);
        if (_aabb(hx, hy, hw, hh, bx, by, bw, bh))
            out_oids[found++] = i;
    }
    return found;
}


/* --- Input ----------------------------------------------------------------- */

/* Versão interna de poll_events; mantida para chamadas entre subsistemas */
void engine_poll_events_impl(Engine *e) { RENDERER(e)->poll_events(e); }

#ifdef ENGINE_BACKEND_GL
#include "Renderizador/RendererGL.hpp"

/* _name_to_keysym é definida em RendererGL.cpp (sem static) */
extern KeySym _name_to_keysym(const char *key);

static inline RendererGL *_rgl(Engine *e)
{
    return static_cast<RendererGL *>(e->renderer_impl);
}

int engine_key_down(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && r->keys_cur[kc]) return 1;
    return 0;
}

int engine_key_pressed(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && r->keys_cur[kc] && !r->keys_prev[kc]) return 1;
    return 0;
}

int engine_key_released(Engine *e, const char *key)
{
    const KeySym ksym = _name_to_keysym(key);
    if (!ksym) return 0;
    const RendererGL *r = _rgl(e);
    for (int kc = 0; kc < ENGINE_MAX_KEYS; ++kc)
        if (e->ksym_cache.map[kc] == ksym && !r->keys_cur[kc] && r->keys_prev[kc]) return 1;
    return 0;
}

#else
/* Stubs vazios: input de teclado para backends não-X11 é tratado dentro do renderer */
int engine_key_down    (Engine *, const char *) { return 0; }
int engine_key_pressed (Engine *, const char *) { return 0; }
int engine_key_released(Engine *, const char *) { return 0; }
#endif

int  engine_mouse_down    (Engine *e, int b) { return (b < 0 || b > 2) ? 0 :  e->mouse.buttons[b]; }
int  engine_mouse_pressed (Engine *e, int b) { return (b < 0 || b > 2) ? 0 :  e->mouse.buttons[b] && !e->mouse.buttons_prev[b]; }
int  engine_mouse_released(Engine *e, int b) { return (b < 0 || b > 2) ? 0 : !e->mouse.buttons[b] &&  e->mouse.buttons_prev[b]; }
void engine_mouse_pos     (Engine *e, int *x, int *y) { *x = e->mouse.x; *y = e->mouse.y; }
int  engine_mouse_scroll  (Engine *e) { return e->mouse.scroll; }