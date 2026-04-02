/*
 * mapa.cpp — Implementação do sistema de mapas 2D.
 *
 * Responsabilidades:
 *   - Carregar mapas via Lua (lua_State) a partir de tabela MapData
 *   - Renderizar camadas de tiles usando engine_draw_sprite_part()
 *   - Detectar colisão AABB com tiles sólidos
 *   - Processar triggers de proximidade (baú, porta, NPC, etc.)
 *
 * Flags de tile aceitam bitmask numérico direto do Lua (uint8_t) OU
 * tabela de strings legíveis — ambos são suportados por _parse_flags_lua().
 *
 * Animação de tiles funciona por deslocamento de tile_col no atlas (a linha
 * fica fixa). Isso é compatível com qualquer tileset clássico 2D sem precisar
 * de múltiplos sprite_ids pré-carregados.
 *
 * O Lua NÃO renderiza nada — apenas descreve a estrutura do mapa.
 * Todo o rendering é feito aqui em C++.
 *
 * Dependências externas:
 *   - Engine.hpp / libengine.so   → renderização e sprites
 *   - lua.hpp (LuaJIT)            → parser do formato .lua
 */

#include "mapa.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

/* LuaJIT — ajuste o include path conforme seu projeto */
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

/* =============================================================================
 * Utilitários internos
 * ============================================================================= */

static void _limpar_mapa(MapaDados *m)
{
    memset(m, 0, sizeof(MapaDados));
    m->tile_w       = 16;
    m->tile_h       = 16;
    m->sprite_atlas = -1;
    m->carregado    = 0;
}

/* Distância em tiles entre um ponto pixel e um objeto */
static float _distancia_tile(MapaDados *m, float px, float py,
                              const MapaObjeto *obj)
{
    float ox = (float)(obj->col * m->tile_w + m->tile_w / 2);
    float oy = (float)(obj->lin * m->tile_h + m->tile_h / 2);
    float dx = px - ox;
    float dy = py - oy;
    float dist_px = sqrtf(dx * dx + dy * dy);
    /* converte para unidades de tile */
    float tile_size = (float)((m->tile_w + m->tile_h) / 2);
    return dist_px / tile_size;
}

/* =============================================================================
 * API pública — Ciclo de vida
 * ============================================================================= */

void mapa_init(MapaDados *m)
{
    _limpar_mapa(m);
}

void mapa_destruir(MapaDados *m)
{
    /* Por ora todos os dados são estáticos (sem heap). */
    _limpar_mapa(m);
}

/* =============================================================================
 * Loader Lua
 *
 * O arquivo .lua deve retornar uma tabela MapData com o mesmo esquema.
 * Ver mapa.lua para o formato completo e funções auxiliares.
 * ============================================================================= */

/* Lê string de campo da tabela no topo da stack */
static const char *_lua_str(lua_State *L, const char *campo, const char *def)
{
    lua_getfield(L, -1, campo);
    const char *v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static int _lua_int(lua_State *L, const char *campo, int def)
{
    lua_getfield(L, -1, campo);
    int v = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static float _lua_float(lua_State *L, const char *campo, float def)
{
    lua_getfield(L, -1, campo);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static int _lua_bool(lua_State *L, const char *campo, int def)
{
    lua_getfield(L, -1, campo);
    int v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

/* Mapeia string de tipo para TriggerTipo */
static int _parse_tipo_objeto(const char *s)
{
    if (!s) return TRIGGER_TIPO_GENERICO;
    if (strcmp(s, "bau")       == 0) return TRIGGER_TIPO_BAU;
    if (strcmp(s, "porta")     == 0) return TRIGGER_TIPO_PORTA;
    if (strcmp(s, "npc")       == 0) return TRIGGER_TIPO_NPC;
    if (strcmp(s, "teleporte") == 0) return TRIGGER_TIPO_TELEPORTE;
    if (strcmp(s, "script")    == 0) return TRIGGER_TIPO_SCRIPT;
    return TRIGGER_TIPO_GENERICO;
}

/*
 * _parse_flags_lua — aceita dois formatos no campo "flags" da tabela Lua:
 *
 *   1. Bitmask numérico (rápido, sem strcmp):
 *        flags = 0x01          -- MAPA_FLAG_COLISOR
 *        flags = 0x01 | 0x10   -- MAPA_FLAG_COLISOR | MAPA_FLAG_SOMBRA
 *
 *   2. Tabela de strings (legível, compatível com código legado):
 *        flags = {"colisor", "sombra"}
 *
 * Ambas as formas podem ser usadas no mesmo mapa sem problema.
 * Espera o valor de "flags" já no topo da stack (lua_getfield já chamado).
 */
static uint8_t _parse_flags_lua(lua_State *L)
{
    /* Formato 1: número direto — path rápido, sem iteração */
    if (lua_isnumber(L, -1))
        return (uint8_t)(int)lua_tonumber(L, -1);

    /* Formato 2: tabela de strings — compatibilidade legada */
    uint8_t f = 0;
    if (!lua_istable(L, -1)) return f;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        const char *s = lua_tostring(L, -1);
        if (s) {
            if (strcmp(s, "colisor") == 0) f |= MAPA_FLAG_COLISOR;
            if (strcmp(s, "trigger") == 0) f |= MAPA_FLAG_TRIGGER;
            if (strcmp(s, "agua")    == 0) f |= MAPA_FLAG_AGUA;
            if (strcmp(s, "escada")  == 0) f |= MAPA_FLAG_ESCADA;
            if (strcmp(s, "sombra")  == 0) f |= MAPA_FLAG_SOMBRA;
            if (strcmp(s, "animado") == 0) f |= MAPA_FLAG_ANIMADO;
        }
        lua_pop(L, 1);
    }
    return f;
}

int mapa_carregar_lua(MapaDados *m, Engine *e, const char *caminho)
{
    _limpar_mapa(m);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    if (luaL_loadfile(L, caminho) || lua_pcall(L, 0, 1, 0)) {
        fprintf(stderr, "[mapa] Erro ao carregar Lua '%s': %s\n",
                caminho, lua_tostring(L, -1));
        lua_close(L);
        return 0;
    }

    if (!lua_istable(L, -1)) {
        fprintf(stderr, "[mapa] '%s' deve retornar uma tabela MapData\n", caminho);
        lua_close(L);
        return 0;
    }

    /* Dimensões */
    m->largura = _lua_int(L, "largura", 10);
    m->altura  = _lua_int(L, "altura",  10);
    m->tile_w  = _lua_int(L, "tile_w",  16);
    m->tile_h  = _lua_int(L, "tile_h",  16);

    /* Atlas principal */
    lua_getfield(L, -1, "sprite_atlas");
    if (lua_isstring(L, -1))
        m->sprite_atlas = engine_load_sprite(e, lua_tostring(L, -1));
    lua_pop(L, 1);

    /* -----------------------------------------------------------------------
     * Camadas
     * ----------------------------------------------------------------------- */
    lua_getfield(L, -1, "camadas");
    if (lua_istable(L, -1)) {
        int n_cam = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n_cam && m->n_camadas < MAPA_MAX_CAMADAS; i++) {
            lua_rawgeti(L, -1, i);               /* push camada[i]             */
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

            MapaCamada *c = &m->camadas[m->n_camadas++];
            strncpy(c->nome, _lua_str(L, "nome", ""), 31);
            c->visivel = _lua_bool(L, "visivel", 1);
            c->z_order = _lua_int (L, "z_order", m->n_camadas - 1);

            /* tiles da camada */
            lua_getfield(L, -1, "tiles");
            if (lua_istable(L, -1)) {
                int n_tiles = (int)lua_objlen(L, -1);
                for (int ti = 1; ti <= n_tiles; ti++) {
                    lua_rawgeti(L, -1, ti);       /* push tile[ti]              */
                    if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

                    int col = _lua_int(L, "col", 0);
                    int lin = _lua_int(L, "lin", 0);
                    if (col < 0 || col >= m->largura ||
                        lin < 0 || lin >= m->altura) {
                        lua_pop(L, 1);
                        continue;
                    }

                    MapaTile *tile = &c->tiles[lin * m->largura + col];

                    /* sprite customizado ou usa atlas */
                    lua_getfield(L, -1, "sprite");
                    if (lua_isstring(L, -1))
                        tile->sprite_id = engine_load_sprite(e, lua_tostring(L, -1));
                    else
                        tile->sprite_id = m->sprite_atlas;
                    lua_pop(L, 1);

                    tile->tile_col = _lua_int  (L, "sprite_col", 0);
                    tile->tile_lin = _lua_int  (L, "sprite_lin", 0);
                    tile->anim_fps = _lua_float(L, "anim_fps",   0.f);

                    /* flags — aceita número (bitmask) ou tabela de strings */
                    lua_getfield(L, -1, "flags");
                    tile->flags = _parse_flags_lua(L);
                    lua_pop(L, 1);

                    if (tile->anim_fps > 0.f)
                        tile->flags |= MAPA_FLAG_ANIMADO;

                    /*
                     * anim_cols — sequência de tile_col para cada frame.
                     * A linha (tile_lin) permanece fixa durante a animação,
                     * o que é o padrão de tilesets 2D clássicos.
                     *
                     * Exemplo Lua:
                     *   chao:tile_animado(2, 5, {0,1,2,3}, 8)
                     *   -- frames nas colunas 0,1,2,3 da mesma linha
                     */
                    lua_getfield(L, -1, "anim_cols");
                    if (lua_istable(L, -1)) {
                        int nf = (int)lua_objlen(L, -1);
                        for (int fi = 1; fi <= nf && fi <= 8; fi++) {
                            lua_rawgeti(L, -1, fi);
                            tile->anim_cols[fi-1] = (int)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                        }
                        tile->n_frames = (nf > 8) ? 8 : nf;
                    }
                    lua_pop(L, 1);  /* anim_cols */

                    lua_pop(L, 1);  /* tile[ti] */
                }
            }
            lua_pop(L, 1);  /* tiles */
            lua_pop(L, 1);  /* camada[i] */
        }
    }
    lua_pop(L, 1);  /* camadas */

    /* -----------------------------------------------------------------------
     * Objetos
     * ----------------------------------------------------------------------- */
    lua_getfield(L, -1, "objetos");
    if (lua_istable(L, -1)) {
        int n_obj = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n_obj && m->n_objetos < MAPA_MAX_OBJETOS; i++) {
            lua_rawgeti(L, -1, i);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

            MapaObjeto *o = &m->objetos[m->n_objetos++];
            o->id   = _lua_int  (L, "id",   m->n_objetos);
            o->col  = _lua_int  (L, "col",  0);
            o->lin  = _lua_int  (L, "lin",  0);
            o->raio = _lua_float(L, "raio", 1.5f);
            o->ativo = 1;
            o->tipo = _parse_tipo_objeto(_lua_str(L, "tipo", "generico"));

            lua_getfield(L, -1, "sprite");
            o->sprite_id = lua_isstring(L, -1) ?
                           engine_load_sprite(e, lua_tostring(L, -1)) : -1;
            lua_pop(L, 1);

            /* props extras */
            lua_getfield(L, -1, "props");
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                while (lua_next(L, -2) && o->n_props < MAPA_MAX_PROPRIEDADES) {
                    if (lua_isstring(L, -2)) {
                        strncpy(o->props[o->n_props][0], lua_tostring(L, -2), 63);
                        strncpy(o->props[o->n_props][1], lua_tostring(L, -1), 63);
                        o->n_props++;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);  /* props */

            lua_pop(L, 1);  /* objeto[i] */
        }
    }
    lua_pop(L, 1);  /* objetos */

    lua_close(L);
    m->carregado = 1;
    printf("[mapa] Lua carregado: %dx%d tiles, %d camadas, %d objetos\n",
           m->largura, m->altura, m->n_camadas, m->n_objetos);
    return 1;
}

/* =============================================================================
 * Renderização
 * ============================================================================= */

void mapa_desenhar_camada(MapaDados *m, Engine *e, int ci)
{
    if (ci < 0 || ci >= m->n_camadas) return;
    MapaCamada *c = &m->camadas[ci];
    if (!c->visivel) return;

    for (int lin = 0; lin < m->altura; lin++) {
        for (int col = 0; col < m->largura; col++) {
            MapaTile *t = &c->tiles[lin * m->largura + col];
            if (t->sprite_id < 0) continue;

            int px = col * m->tile_w + m->offset_x;
            int py = lin * m->tile_h + m->offset_y;
            int sx = t->tile_col * m->tile_w;
            int sy = t->tile_lin * m->tile_h;

            /* Tile animado: desloca tile_col no atlas mantendo tile_lin fixo */
            if ((t->flags & MAPA_FLAG_ANIMADO) && t->n_frames > 0 &&
                t->anim_fps > 0.f) {
                double tempo = engine_get_time(e);
                int frame    = (int)(tempo * t->anim_fps) % t->n_frames;
                int anim_col = t->anim_cols[frame];
                sx = anim_col * m->tile_w;
                /* sy (tile_lin) permanece o mesmo calculado acima */
                engine_draw_sprite_part(e, t->sprite_id, px, py,
                                        sx, sy, m->tile_w, m->tile_h);
            } else {
                engine_draw_sprite_part(e, t->sprite_id, px, py,
                                        sx, sy, m->tile_w, m->tile_h);
            }
        }
    }
}

void mapa_desenhar_objetos(MapaDados *m, Engine *e)
{
    for (int i = 0; i < m->n_objetos; i++) {
        MapaObjeto *o = &m->objetos[i];
        if (!o->ativo || o->sprite_id < 0) continue;
        int px = o->col * m->tile_w + m->offset_x;
        int py = o->lin * m->tile_h + m->offset_y;
        engine_draw_sprite_part(e, o->sprite_id, px, py,
                                0, 0, m->tile_w, m->tile_h);
    }
}

void mapa_desenhar(MapaDados *m, Engine *e)
{
    if (!m->carregado) return;
    /* Desenha em ordem de z_order (simples: itera na ordem declarada) */
    for (int i = 0; i < m->n_camadas; i++)
        mapa_desenhar_camada(m, e, i);
    mapa_desenhar_objetos(m, e);
}

/* =============================================================================
 * Consultas de tile
 * ============================================================================= */

MapaTile *mapa_get_tile(MapaDados *m, int camada, int col, int lin)
{
    if (camada < 0 || camada >= m->n_camadas) return NULL;
    if (col < 0 || col >= m->largura)        return NULL;
    if (lin < 0 || lin >= m->altura)         return NULL;
    return &m->camadas[camada].tiles[lin * m->largura + col];
}

int mapa_colisor(MapaDados *m, int col, int lin)
{
    for (int i = 0; i < m->n_camadas; i++) {
        MapaTile *t = mapa_get_tile(m, i, col, lin);
        if (t && (t->flags & MAPA_FLAG_COLISOR)) return 1;
    }
    return 0;
}

int mapa_flags(MapaDados *m, int col, int lin)
{
    int f = 0;
    for (int i = 0; i < m->n_camadas; i++) {
        MapaTile *t = mapa_get_tile(m, i, col, lin);
        if (t) f |= t->flags;
    }
    return f;
}

/* =============================================================================
 * Conversão de coordenadas
 * ============================================================================= */

void mapa_pixel_para_tile(MapaDados *m, int px, int py, int *out_col, int *out_lin)
{
    *out_col = (px - m->offset_x) / m->tile_w;
    *out_lin = (py - m->offset_y) / m->tile_h;
}

void mapa_tile_para_pixel(MapaDados *m, int col, int lin, int *out_x, int *out_y)
{
    *out_x = col * m->tile_w + m->offset_x;
    *out_y = lin * m->tile_h + m->offset_y;
}

/* =============================================================================
 * Triggers e objetos
 * ============================================================================= */

MapaVerificacao mapa_verificar_trigger(MapaDados *m, float px, float py)
{
    MapaVerificacao v = { MAPA_TRIGGER_NENHUM, NULL, 0.f };
    for (int i = 0; i < m->n_objetos; i++) {
        MapaObjeto *o = &m->objetos[i];
        if (!o->ativo) continue;
        float dist = _distancia_tile(m, px, py, o);
        if (dist <= o->raio) {
            v.resultado = MAPA_TRIGGER_ATIVO;
            v.objeto    = o;
            v.distancia = dist;
            return v;   /* retorna o primeiro trigger encontrado */
        }
    }
    return v;
}

MapaObjeto *mapa_get_objeto(MapaDados *m, int id)
{
    for (int i = 0; i < m->n_objetos; i++)
        if (m->objetos[i].id == id) return &m->objetos[i];
    return NULL;
}

void mapa_desativar_objeto(MapaDados *m, int id)
{
    MapaObjeto *o = mapa_get_objeto(m, id);
    if (o) o->ativo = 0;
}

/* =============================================================================
 * Câmera do mapa
 * ============================================================================= */

void mapa_set_offset(MapaDados *m, int ox, int oy)
{
    m->offset_x = ox;
    m->offset_y = oy;
}

void mapa_centralizar_em(MapaDados *m, Engine *e, int col, int lin)
{
    int px = col * m->tile_w;
    int py = lin * m->tile_h;
    int sw = (int)e->render_w;
    int sh = (int)e->render_h;
    m->offset_x = sw / 2 - px - m->tile_w / 2;
    m->offset_y = sh / 2 - py - m->tile_h / 2;
}

/* =============================================================================
 * Modificação em tempo de execução
 * ============================================================================= */

void mapa_set_tile(MapaDados *m, int camada, int col, int lin,
                   int sprite_id, int tile_col, int tile_lin, uint8_t flags)
{
    MapaTile *t = mapa_get_tile(m, camada, col, lin);
    if (!t) return;
    t->sprite_id = sprite_id;
    t->tile_col  = tile_col;
    t->tile_lin  = tile_lin;
    t->flags     = flags;
}

/*
 * mapa_set_tile_atlas — variante de mapa_set_tile que usa o atlas principal
 * do mapa como sprite_id automaticamente. Preferível quando todos os tiles
 * vêm do mesmo tileset (o caso mais comum ao usar carregar_matriz() no Lua).
 *
 * tile_col, tile_lin → coordenadas do tile na grade do atlas (não em pixels).
 * flags              → bitmask MAPA_FLAG_* (ex: MAPA_FLAG_COLISOR | MAPA_FLAG_SOMBRA).
 *
 * Exemplo:
 *   // coloca tile da coluna 0, linha 1 do atlas na posição (3,2) da camada 0
 *   mapa_set_tile_atlas(m, 0, 3, 2, 0, 1, MAPA_FLAG_COLISOR);
 */
void mapa_set_tile_atlas(MapaDados *m, int camada, int col, int lin,
                         int tile_col, int tile_lin, uint8_t flags)
{
    mapa_set_tile(m, camada, col, lin,
                  m->sprite_atlas,   /* usa atlas principal automaticamente */
                  tile_col, tile_lin, flags);
}