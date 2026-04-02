//mapa.cpp

#include "mapa.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

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

/* Distância em tiles entre um ponto pixel e o centro de um objeto. */
static float _distancia_tile(MapaDados *m, float px, float py,
                              const MapaObjeto *obj)
{
    float ox = (float)(obj->col * m->tile_w + m->tile_w / 2);
    float oy = (float)(obj->lin * m->tile_h + m->tile_h / 2);
    float dx = px - ox, dy = py - oy;
    float tile_size = (float)((m->tile_w + m->tile_h) / 2);
    return sqrtf(dx*dx + dy*dy) / tile_size;
}

/* =============================================================================
 * Ciclo de vida
 * ============================================================================= */

void mapa_init(MapaDados *m)    { _limpar_mapa(m); }
void mapa_destruir(MapaDados *m){ _limpar_mapa(m); }  /* sem heap; zera tudo */

/* =============================================================================
 * Helpers de leitura da stack Lua
 * ============================================================================= */

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

/* Converte string de tipo de objeto para TriggerTipo. */
static int _parse_tipo_objeto(const char *s)
{
    if (!s)                         return TRIGGER_TIPO_GENERICO;
    if (strcmp(s, "bau")       == 0) return TRIGGER_TIPO_BAU;
    if (strcmp(s, "porta")     == 0) return TRIGGER_TIPO_PORTA;
    if (strcmp(s, "npc")       == 0) return TRIGGER_TIPO_NPC;
    if (strcmp(s, "teleporte") == 0) return TRIGGER_TIPO_TELEPORTE;
    if (strcmp(s, "script")    == 0) return TRIGGER_TIPO_SCRIPT;
    return TRIGGER_TIPO_GENERICO;
}

/*
 * _parse_flags_lua — lê o campo "flags" em dois formatos:
 *
 *   1. Bitmask numérico (rápido):
 *        flags = 0x01          -- COLISOR
 *        flags = 0x01 | 0x10   -- COLISOR | SOMBRA
 *
 *   2. Tabela de strings (legível):
 *        flags = {"colisor", "sombra"}
 *
 * Espera o valor de "flags" já no topo da stack.
 */
static uint8_t _parse_flags_lua(lua_State *L)
{
    if (lua_isnumber(L, -1))
        return (uint8_t)(int)lua_tonumber(L, -1);

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

/* =============================================================================
 * mapa_carregar_lua — carrega e parseia um arquivo .lua de mapa.
 *
 * O arquivo deve retornar m:build() (ver mapa.lua).
 * Retorna 1 em sucesso, 0 em erro.
 * ============================================================================= */
int mapa_carregar_lua(MapaDados *m, Engine *e, const char *caminho)
{
    _limpar_mapa(m);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    if (luaL_loadfile(L, caminho) || lua_pcall(L, 0, 1, 0)) {
        fprintf(stderr, "[mapa] Erro ao carregar '%s': %s\n",
                caminho, lua_tostring(L, -1));
        lua_close(L);
        return 0;
    }

    if (!lua_istable(L, -1)) {
        fprintf(stderr, "[mapa] '%s' deve retornar m:build()\n", caminho);
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

    /* ------------------------------------------------------------------
     * Camadas
     * ------------------------------------------------------------------ */
    lua_getfield(L, -1, "camadas");
    if (lua_istable(L, -1)) {
        int n_cam = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n_cam && m->n_camadas < MAPA_MAX_CAMADAS; i++) {
            lua_rawgeti(L, -1, i);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

            MapaCamada *c = &m->camadas[m->n_camadas++];
            strncpy(c->nome, _lua_str(L, "nome", ""), 31);
            c->visivel = _lua_bool(L, "visivel", 1);
            c->z_order = _lua_int (L, "z_order", m->n_camadas - 1);

            lua_getfield(L, -1, "tiles");
            if (lua_istable(L, -1)) {
                int n_tiles = (int)lua_rawlen(L, -1);
                for (int ti = 1; ti <= n_tiles; ti++) {
                    lua_rawgeti(L, -1, ti);
                    if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

                    int col = _lua_int(L, "col", 0);
                    int lin = _lua_int(L, "lin", 0);
                    if (col < 0 || col >= m->largura ||
                        lin < 0 || lin >= m->altura) {
                        lua_pop(L, 1);
                        continue;
                    }

                    MapaTile *tile = &c->tiles[lin * m->largura + col];

                    /* sprite: usa campo "sprite" ou cai no atlas principal */
                    lua_getfield(L, -1, "sprite");
                    if (lua_isstring(L, -1))
                        tile->sprite_id = engine_load_sprite(e, lua_tostring(L, -1));
                    else
                        tile->sprite_id = m->sprite_atlas;
                    lua_pop(L, 1);

                    tile->tile_col = _lua_int  (L, "sprite_col", 0);
                    tile->tile_lin = _lua_int  (L, "sprite_lin", 0);
                    tile->anim_fps = _lua_float(L, "anim_fps",   0.f);

                    lua_getfield(L, -1, "flags");
                    tile->flags = _parse_flags_lua(L);
                    lua_pop(L, 1);

                    if (tile->anim_fps > 0.f)
                        tile->flags |= MAPA_FLAG_ANIMADO;

                    /*
                     * anim_cols — colunas do atlas por frame.
                     * Linha permanece fixa (padrão de tilesets clássicos).
                     */
                    lua_getfield(L, -1, "anim_cols");
                    if (lua_istable(L, -1)) {
                        int nf = (int)lua_rawlen(L, -1);
                        for (int fi = 1; fi <= nf && fi <= 8; fi++) {
                            lua_rawgeti(L, -1, fi);
                            tile->anim_cols[fi-1] = (int)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                        }
                        tile->n_frames = (nf > 8) ? 8 : nf;
                    }
                    lua_pop(L, 1);

                    /*
                     * anim_lins — linha do atlas por frame (opcional).
                     * Codificado em anim_cols como: col | (lin << 8).
                     * A renderização desempacota se o valor > 255.
                     */
                    lua_getfield(L, -1, "anim_lins");
                    if (lua_istable(L, -1) && tile->n_frames > 0) {
                        int nl = (int)lua_rawlen(L, -1);
                        for (int fi = 1; fi <= nl && fi <= tile->n_frames; fi++) {
                            lua_rawgeti(L, -1, fi);
                            int lin_frame = (int)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                            tile->anim_cols[fi-1] =
                                (tile->anim_cols[fi-1] & 0xFF) |
                                ((lin_frame & 0xFF) << 8);
                        }
                        tile->flags |= MAPA_FLAG_ANIMADO;
                    }
                    lua_pop(L, 1);  /* anim_lins */

                    lua_pop(L, 1);  /* tile[ti] */
                }
            }
            lua_pop(L, 1);  /* tiles */
            lua_pop(L, 1);  /* camada[i] */
        }
    }
    lua_pop(L, 1);  /* camadas */

    /* ------------------------------------------------------------------
     * Objetos
     * ------------------------------------------------------------------ */
    lua_getfield(L, -1, "objetos");
    if (lua_istable(L, -1)) {
        int n_obj = (int)lua_rawlen(L, -1);
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

            /* props extras (pares chave/valor string) */
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
    printf("[mapa] Carregado: %dx%d tiles, %d camadas, %d objetos\n",
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

            /*
             * Tile animado: anim_cols[] armazena o frame a usar.
             * Formato do valor:
             *   valor <= 0xFF  → apenas coluna (linha fixa = tile_lin)
             *   valor >  0xFF  → col em bits 0-7, lin em bits 8-15
             */
            if ((t->flags & MAPA_FLAG_ANIMADO) && t->n_frames > 0 &&
                t->anim_fps > 0.f) {
                int frame    = (int)(engine_get_time(e) * t->anim_fps) % t->n_frames;
                int packed   = t->anim_cols[frame];
                int anim_col = packed & 0xFF;
                int anim_lin = (packed > 0xFF) ? ((packed >> 8) & 0xFF) : t->tile_lin;
                sx = anim_col * m->tile_w;
                sy = anim_lin * m->tile_h;
            }

            engine_draw_sprite_part(e, t->sprite_id, px, py,
                                    sx, sy, m->tile_w, m->tile_h);
        }
    }
}

/* Desenha objetos ativos que tenham sprite. */
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

/* Desenha todas as camadas (na ordem declarada) e depois os objetos. */
void mapa_desenhar(MapaDados *m, Engine *e)
{
    if (!m->carregado) return;
    for (int i = 0; i < m->n_camadas; i++)
        mapa_desenhar_camada(m, e, i);
    mapa_desenhar_objetos(m, e);
}

/* =============================================================================
 * Consultas de tile
 * ============================================================================= */

/* Retorna ponteiro para o tile em (camada, col, lin), ou NULL se inválido. */
MapaTile *mapa_get_tile(MapaDados *m, int camada, int col, int lin)
{
    if (camada < 0 || camada >= m->n_camadas) return NULL;
    if (col < 0 || col >= m->largura)        return NULL;
    if (lin < 0 || lin >= m->altura)         return NULL;
    return &m->camadas[camada].tiles[lin * m->largura + col];
}

/* true se qualquer camada tem COLISOR na posição (col, lin). */
int mapa_colisor(MapaDados *m, int col, int lin)
{
    for (int i = 0; i < m->n_camadas; i++) {
        MapaTile *t = mapa_get_tile(m, i, col, lin);
        if (t && (t->flags & MAPA_FLAG_COLISOR)) return 1;
    }
    return 0;
}

/* Retorna o OR de todas as flags de todas as camadas na posição (col, lin). */
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

/* Verifica se o ponto pixel (px, py) está dentro do raio de algum objeto ativo.
 * Retorna o primeiro encontrado. */
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
            return v;
        }
    }
    return v;
}

/* Busca objeto por id. Retorna NULL se não encontrar. */
MapaObjeto *mapa_get_objeto(MapaDados *m, int id)
{
    for (int i = 0; i < m->n_objetos; i++)
        if (m->objetos[i].id == id) return &m->objetos[i];
    return NULL;
}

/* Desativa um objeto (não é mais desenhado nem dispara trigger). */
void mapa_desativar_objeto(MapaDados *m, int id)
{
    MapaObjeto *o = mapa_get_objeto(m, id);
    if (o) o->ativo = 0;
}

/* =============================================================================
 * Câmera do mapa
 * ============================================================================= */

/* Define o deslocamento do mapa em pixels (scroll). */
void mapa_set_offset(MapaDados *m, int ox, int oy)
{
    m->offset_x = ox;
    m->offset_y = oy;
}

/* Centraliza o mapa no tile (col, lin) em relação à tela. */
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

/* Substitui um tile em (camada, col, lin) com sprite e flags novos. */
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
 * mapa_set_tile_atlas — igual a mapa_set_tile, mas usa o atlas principal
 * automaticamente. Preferível quando todos os tiles vêm do mesmo tileset.
 *
 * tile_col, tile_lin → posição na grade do atlas (não em pixels).
 *
 * Exemplo:
 *   mapa_set_tile_atlas(m, 0, 3, 2, 0, 1, MAPA_FLAG_COLISOR);
 *   // coloca tile (col=0, lin=1) do atlas na posição (3,2) da camada 0
 */
void mapa_set_tile_atlas(MapaDados *m, int camada, int col, int lin,
                         int tile_col, int tile_lin, uint8_t flags)
{
    mapa_set_tile(m, camada, col, lin,
                  m->sprite_atlas,
                  tile_col, tile_lin, flags);
}