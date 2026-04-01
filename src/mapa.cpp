/*
 * mapa.cpp — Implementação do sistema de mapas 2D.
 *
 * Responsabilidades:
 *   - Carregar mapas de JSON (via parser minimalista) ou de Lua (via lua_State)
 *   - Renderizar camadas de tiles usando engine_draw_sprite_part()
 *   - Detectar colisão AABB com tiles sólidos
 *   - Processar triggers de proximidade (baú, porta, NPC, etc.)
 *
 * O Lua NÃO renderiza nada — apenas descreve a estrutura do mapa.
 * Todo o rendering é feito aqui em C++.
 *
 * Dependências externas:
 *   - Engine.hpp / libengine.so   → renderização e sprites
 *   - lua.hpp (LuaJIT)            → parser do formato .lua
 *   - cJSON (single-header)       → parser do formato .json
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
 * cJSON — parser JSON single-header (copie cJSON.h/.c ao lado deste arquivo)
 * https://github.com/DaveGamble/cJSON
 * ============================================================================= */
#include "cJSON.h"

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
 * Loader JSON
 *
 * Formato esperado (exemplo em mapa.json):
 * {
 *   "largura": 20, "altura": 15,
 *   "tile_w": 16,  "tile_h": 16,
 *   "sprite_atlas": "assets/tileset.png",
 *   "camadas": [
 *     {
 *       "nome": "chao", "z_order": 0, "visivel": true,
 *       "tiles": [
 *         { "col": 0, "lin": 0, "sprite_col": 1, "sprite_lin": 0,
 *           "flags": ["colisor"] },
 *         ...
 *       ]
 *     }
 *   ],
 *   "objetos": [
 *     { "id": 1, "tipo": "bau", "col": 5, "lin": 3, "raio": 1.5,
 *       "props": { "item": "espada", "quantidade": "1" } }
 *   ]
 * }
 * ============================================================================= */

static uint8_t _parse_flags_json(cJSON *arr)
{
    uint8_t f = 0;
    if (!arr || !cJSON_IsArray(arr)) return f;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsString(item)) continue;
        const char *s = item->valuestring;
        if (strcmp(s, "colisor") == 0)  f |= MAPA_FLAG_COLISOR;
        if (strcmp(s, "trigger") == 0)  f |= MAPA_FLAG_TRIGGER;
        if (strcmp(s, "agua")    == 0)  f |= MAPA_FLAG_AGUA;
        if (strcmp(s, "escada")  == 0)  f |= MAPA_FLAG_ESCADA;
        if (strcmp(s, "sombra")  == 0)  f |= MAPA_FLAG_SOMBRA;
        if (strcmp(s, "animado") == 0)  f |= MAPA_FLAG_ANIMADO;
    }
    return f;
}

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

int mapa_carregar_json(MapaDados *m, Engine *e, const char *caminho)
{
    _limpar_mapa(m);

    /* Lê arquivo inteiro */
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        fprintf(stderr, "[mapa] Erro: não foi possível abrir '%s'\n", caminho);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        fprintf(stderr, "[mapa] Erro ao parsear JSON: %s\n", cJSON_GetErrorPtr());
        return 0;
    }

    /* Dimensões */
    m->largura = cJSON_GetObjectItem(root, "largura") ?
                 cJSON_GetObjectItem(root, "largura")->valueint : 10;
    m->altura  = cJSON_GetObjectItem(root, "altura")  ?
                 cJSON_GetObjectItem(root, "altura")->valueint  : 10;
    m->tile_w  = cJSON_GetObjectItem(root, "tile_w")  ?
                 cJSON_GetObjectItem(root, "tile_w")->valueint  : 16;
    m->tile_h  = cJSON_GetObjectItem(root, "tile_h")  ?
                 cJSON_GetObjectItem(root, "tile_h")->valueint  : 16;

    /* Atlas principal */
    cJSON *atlas = cJSON_GetObjectItem(root, "sprite_atlas");
    if (atlas && cJSON_IsString(atlas)) {
        m->sprite_atlas = engine_load_sprite(e, atlas->valuestring);
    }

    /* Camadas */
    cJSON *camadas = cJSON_GetObjectItem(root, "camadas");
    if (camadas && cJSON_IsArray(camadas)) {
        cJSON *cam = NULL;
        cJSON_ArrayForEach(cam, camadas) {
            if (m->n_camadas >= MAPA_MAX_CAMADAS) break;
            MapaCamada *c = &m->camadas[m->n_camadas++];

            cJSON *nome = cJSON_GetObjectItem(cam, "nome");
            if (nome && cJSON_IsString(nome))
                strncpy(c->nome, nome->valuestring, 31);

            c->visivel = 1;
            cJSON *vis = cJSON_GetObjectItem(cam, "visivel");
            if (vis) c->visivel = cJSON_IsTrue(vis) ? 1 : 0;

            cJSON *z = cJSON_GetObjectItem(cam, "z_order");
            if (z) c->z_order = z->valueint;

            cJSON *tiles = cJSON_GetObjectItem(cam, "tiles");
            if (!tiles || !cJSON_IsArray(tiles)) continue;

            cJSON *t = NULL;
            cJSON_ArrayForEach(t, tiles) {
                int col = cJSON_GetObjectItem(t, "col") ?
                          cJSON_GetObjectItem(t, "col")->valueint : 0;
                int lin = cJSON_GetObjectItem(t, "lin") ?
                          cJSON_GetObjectItem(t, "lin")->valueint : 0;
                if (col < 0 || col >= m->largura ||
                    lin < 0 || lin >= m->altura) continue;

                MapaTile *tile = &c->tiles[lin * m->largura + col];

                /* sprite customizado ou usa atlas */
                cJSON *spr = cJSON_GetObjectItem(t, "sprite");
                if (spr && cJSON_IsString(spr))
                    tile->sprite_id = engine_load_sprite(e, spr->valuestring);
                else
                    tile->sprite_id = m->sprite_atlas;

                cJSON *sc = cJSON_GetObjectItem(t, "sprite_col");
                cJSON *sl = cJSON_GetObjectItem(t, "sprite_lin");
                tile->tile_col = sc ? sc->valueint : 0;
                tile->tile_lin = sl ? sl->valueint : 0;
                tile->flags    = _parse_flags_json(cJSON_GetObjectItem(t, "flags"));

                /* Animação */
                cJSON *afps = cJSON_GetObjectItem(t, "anim_fps");
                if (afps) {
                    tile->anim_fps = (float)afps->valuedouble;
                    tile->flags |= MAPA_FLAG_ANIMADO;
                }
                cJSON *af = cJSON_GetObjectItem(t, "anim_frames");
                if (af && cJSON_IsArray(af)) {
                    cJSON *fr = NULL;
                    int fi = 0;
                    cJSON_ArrayForEach(fr, af) {
                        if (fi >= 8) break;
                        tile->anim_frames[fi++] = fr->valueint;
                    }
                    tile->n_frames = fi;
                }
            }
        }
    }

    /* Objetos */
    cJSON *objs = cJSON_GetObjectItem(root, "objetos");
    if (objs && cJSON_IsArray(objs)) {
        cJSON *obj = NULL;
        cJSON_ArrayForEach(obj, objs) {
            if (m->n_objetos >= MAPA_MAX_OBJETOS) break;
            MapaObjeto *o = &m->objetos[m->n_objetos++];

            cJSON *id = cJSON_GetObjectItem(obj, "id");
            o->id = id ? id->valueint : m->n_objetos;

            cJSON *tipo = cJSON_GetObjectItem(obj, "tipo");
            o->tipo = _parse_tipo_objeto(tipo ? tipo->valuestring : NULL);

            cJSON *col  = cJSON_GetObjectItem(obj, "col");
            cJSON *lin  = cJSON_GetObjectItem(obj, "lin");
            o->col = col ? col->valueint : 0;
            o->lin = lin ? lin->valueint : 0;

            cJSON *raio = cJSON_GetObjectItem(obj, "raio");
            o->raio = raio ? (float)raio->valuedouble : 1.5f;
            o->ativo = 1;

            cJSON *spr = cJSON_GetObjectItem(obj, "sprite");
            o->sprite_id = (spr && cJSON_IsString(spr)) ?
                           engine_load_sprite(e, spr->valuestring) : -1;

            /* Props extras */
            cJSON *props = cJSON_GetObjectItem(obj, "props");
            if (props && cJSON_IsObject(props)) {
                cJSON *p = NULL;
                cJSON_ArrayForEach(p, props) {
                    if (o->n_props >= MAPA_MAX_PROPRIEDADES) break;
                    strncpy(o->props[o->n_props][0], p->string,        63);
                    strncpy(o->props[o->n_props][1], p->valuestring,   63);
                    o->n_props++;
                }
            }
        }
    }

    cJSON_Delete(root);
    m->carregado = 1;
    printf("[mapa] JSON carregado: %dx%d tiles, %d camadas, %d objetos\n",
           m->largura, m->altura, m->n_camadas, m->n_objetos);
    return 1;
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

static uint8_t _parse_flags_lua(lua_State *L)
{
    /* Espera tabela de strings no topo da stack */
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

                    /* flags */
                    lua_getfield(L, -1, "flags");
                    tile->flags = _parse_flags_lua(L);
                    lua_pop(L, 1);

                    if (tile->anim_fps > 0.f)
                        tile->flags |= MAPA_FLAG_ANIMADO;

                    /* frames de animação */
                    lua_getfield(L, -1, "anim_frames");
                    if (lua_istable(L, -1)) {
                        int nf = (int)lua_objlen(L, -1);
                        for (int fi = 1; fi <= nf && fi <= 8; fi++) {
                            lua_rawgeti(L, -1, fi);
                            tile->anim_frames[fi-1] = (int)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                        }
                        tile->n_frames = (nf > 8) ? 8 : nf;
                    }
                    lua_pop(L, 1);  /* anim_frames */

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

            /* Tile animado: calcula frame atual */
            if ((t->flags & MAPA_FLAG_ANIMADO) && t->n_frames > 0 &&
                t->anim_fps > 0.f) {
                double tempo = engine_get_time(e);
                int frame = (int)(tempo * t->anim_fps) % t->n_frames;
                int sid = t->anim_frames[frame];
                engine_draw_sprite_part(e, sid, px, py, 0, 0, m->tile_w, m->tile_h);
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