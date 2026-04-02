-- mapa.lua
local bit = require("bit")
local bor, lshift = bit.bor, bit.lshift

local Mapa = {}
Mapa.__index = Mapa

-- Flags de tile (bitmask). Espelham MAPA_FLAG_* do C++.
Mapa.F = {
    NENHUM  = 0,
    COLISOR = lshift(1, 0),   -- 0x01  bloqueia movimento
    TRIGGER = lshift(1, 1),   -- 0x02  porta, baú, NPC...
    AGUA    = lshift(1, 2),   -- 0x04
    ESCADA  = lshift(1, 3),   -- 0x08
    SOMBRA  = lshift(1, 4),   -- 0x10
    ANIMADO = lshift(1, 5),   -- 0x20  setado automaticamente em tiles animados
}

-- =============================================================================
-- Camada (interna)
-- =============================================================================

local Camada = {}
Camada.__index = Camada

-- Adiciona um tile estático.
function Camada:_add_tile(col, lin, sprite_col, sprite_lin, flags, sprite)
    table.insert(self._tiles, {
        col        = col,
        lin        = lin,
        sprite_col = sprite_col,
        sprite_lin = sprite_lin,
        flags      = flags,
        sprite     = sprite,
    })
end

--[[
    Adiciona um tile animado.
    anim_cols → {c0,c1,...}  coluna do atlas por frame
    anim_lins → {l0,l1,...}  linha do atlas por frame (nil = todos na sprite_lin)
]]
function Camada:_add_tile_animado(col, lin, sprite_lin, anim_cols, anim_lins,
                                   fps, loop, flags, sprite)
    table.insert(self._tiles, {
        col        = col,
        lin        = lin,
        sprite_col = anim_cols[1],
        sprite_lin = sprite_lin,
        flags      = bor(flags, Mapa.F.ANIMADO),
        sprite     = sprite,
        anim_fps   = fps  or 4,
        anim_loop  = (loop ~= false),
        anim_cols  = anim_cols,
        anim_lins  = anim_lins,
    })
end

function Camada:_to_table()
    return {
        nome    = self.nome,
        layer   = self.layer,
        z       = self.z,
        visivel = self.visivel,
        tiles   = self._tiles,
    }
end

-- =============================================================================
-- Mapa
-- =============================================================================

-- Cria um novo mapa. tile_w/h padrão: 16px.
function Mapa.novo(largura, altura, tile_w, tile_h)
    local self  = setmetatable({}, Mapa)
    self.largura    = largura or 20
    self.altura     = altura  or 15
    self.tile_w     = tile_w  or 16
    self.tile_h     = tile_h  or 16
    self._atlas     = nil
    self._camadas   = {}     -- lista de Camada (em ordem de inserção)
    self._cam_index = {}     -- "layer:z" → Camada
    self._objetos   = {}
    self._templates = {}
    return self
end

-- Define o caminho do spritesheet principal.
function Mapa:atlas(caminho)
    self._atlas = caminho
    return self
end

-- =============================================================================
-- _get_ou_criar_camada (interna)
-- Retorna a camada para (layer, z), criando-a se necessário.
-- =============================================================================
function Mapa:_get_ou_criar_camada(layer, z, visivel)
    layer = layer or 0
    z     = z     or 0
    local key = layer .. ":" .. z
    if self._cam_index[key] then
        return self._cam_index[key]
    end
    local c = setmetatable({
        nome    = "layer" .. layer .. "_z" .. z,
        layer   = layer,
        z       = z,
        visivel = visivel ~= false,
        _tiles  = {},
    }, Camada)
    table.insert(self._camadas, c)
    self._cam_index[key] = c
    return c
end

-- =============================================================================
-- :camada(id_bloco, layer, z)
--
-- Registra em qual camada de renderização um bloco deve aparecer.
-- Pode ser chamado várias vezes para o mesmo bloco — ele aparece em todas.
-- Se não for chamado, o bloco vai para layer=0, z=0.
--
-- Exemplo:
--   m:camada(G, 0)      -- layer 0, z 0 (fundo)
--   m:camada(P, 1)      -- layer 1, z 0
--   m:camada(P, 1, 2)   -- mesmo bloco também em layer 1, z 2
--
-- Atenção: o bloco deve ter sido criado com :criar_bloco() antes.
-- layer/z menores = desenhados primeiro (mais ao fundo).
-- =============================================================================
function Mapa:camada(id_bloco, layer, z)
    layer = layer or 0
    z     = z     or 0

    self:_get_ou_criar_camada(layer, z)

    local t = self._templates[id_bloco]
    if not t then
        error(("[mapa] camada(): bloco id=%d não existe"):format(id_bloco))
    end

    local key = layer .. ":" .. z
    if not t._destinos then t._destinos = {} end
    if not t._destinos[key] then
        t._destinos[key] = { layer = layer, z = z }
    end

    return self
end

-- =============================================================================
-- :objeto(nome, id, col, lin, raio, sprite, anim_cols, anim_lin, fps, loop)
-- Adiciona um objeto interativo ao mapa (NPC, baú, porta...).
-- raio = distância em tiles para ativar o trigger.
-- =============================================================================
function Mapa:objeto(nome, id, col, lin, raio, sprite,
                     anim_cols, anim_lin, fps, loop)
    local obj = {
        nome      = nome  or "objeto",
        id        = id    or (#self._objetos + 1),
        col       = col   or 0,
        lin       = lin   or 0,
        raio      = raio  or 1.5,
        sprite    = sprite,
        ativo     = true,
        anim_cols = anim_cols,
        anim_lin  = anim_lin  or 0,
        anim_fps  = fps       or 4,
        anim_loop = (loop ~= false),
    }
    table.insert(self._objetos, obj)
    return obj
end

-- =============================================================================
-- :criar_bloco(nome, id, col, lin, colide, loop, fps, sprite)
--
-- Define o protótipo de um tile. Use :camada() depois para posicioná-lo.
-- col/lin podem ser números (tile estático) ou tabelas (tile animado).
--
-- Exemplos:
--   local G = m:criar_bloco("Grama",  1, 0, 0, false, true, 0)
--   local P = m:criar_bloco("Parede", 2, 1, 1, true,  true, 0)
--   local A = m:criar_bloco("Agua",   3, {0,1,2}, 2, false, true, 6)    -- colunas animadas
--   local L = m:criar_bloco("Lava",   4, {0,1,2}, {2,2,3}, true, true, 8) -- col+lin animados
-- =============================================================================
function Mapa:criar_bloco(nome, id, col, lin, colide, loop, fps, sprite)
    local F     = Mapa.F
    local flags = 0

    if colide then flags = bor(flags, F.COLISOR) end

    local animado   = type(col) == "table"
    local anim_cols = nil
    local anim_lins = nil
    local sprite_col, sprite_lin

    if animado then
        flags      = bor(flags, F.ANIMADO)
        anim_cols  = col
        sprite_col = col[1]
        if type(lin) == "table" then
            anim_lins  = lin
            sprite_lin = lin[1]
        else
            sprite_lin = lin or 0
        end
    else
        sprite_col = col or 0
        sprite_lin = lin or 0
    end

    self._templates[id] = {
        nome       = nome,
        sprite_col = sprite_col,
        sprite_lin = sprite_lin,
        flags      = flags,
        sprite     = sprite,
        animado    = animado,
        anim_cols  = anim_cols,
        anim_lins  = anim_lins,
        anim_fps   = fps  or 4,
        anim_loop  = (loop ~= false),
        _destinos  = nil,   -- preenchido por :camada()
    }
    return id
end

-- =============================================================================
-- :carregar_matriz(layout)
--
-- Preenche o mapa a partir de um array 1D de IDs de bloco (0/nil = vazio).
-- Cada bloco vai para todas as camadas registradas via :camada().
-- Se nenhuma camada foi registrada, usa layer=0, z=0.
-- =============================================================================
function Mapa:carregar_matriz(layout)
    for i, bloco_id in ipairs(layout) do
        if bloco_id and bloco_id ~= 0 then
            local t = self._templates[bloco_id]
            if t then
                local col = (i - 1) % self.largura
                local lin = math.floor((i - 1) / self.largura)

                local destinos = t._destinos
                if not destinos or not next(destinos) then
                    destinos = { ["0:0"] = { layer = 0, z = 0 } }
                end

                for _, dest in pairs(destinos) do
                    local c = self:_get_ou_criar_camada(dest.layer, dest.z)
                    if t.animado then
                        c:_add_tile_animado(
                            col, lin,
                            t.sprite_lin,
                            t.anim_cols,
                            t.anim_lins,
                            t.anim_fps,
                            t.anim_loop,
                            t.flags,
                            t.sprite
                        )
                    else
                        c:_add_tile(
                            col, lin,
                            t.sprite_col, t.sprite_lin,
                            t.flags,
                            t.sprite
                        )
                    end
                end
            end
        end
    end
    return self
end

-- =============================================================================
-- :build()
-- Serializa o mapa para engine:carregar_mapa(). Camadas ordenadas por (layer, z).
-- =============================================================================
function Mapa:build()
    local ordenadas = {}
    for _, c in ipairs(self._camadas) do
        table.insert(ordenadas, c)
    end
    table.sort(ordenadas, function(a, b)
        if a.layer ~= b.layer then return a.layer < b.layer end
        return a.z < b.z
    end)

    local data = {
        largura      = self.largura,
        altura       = self.altura,
        tile_w       = self.tile_w,
        tile_h       = self.tile_h,
        sprite_atlas = self._atlas,
        camadas      = {},
        objetos      = {},
    }
    for _, c in ipairs(ordenadas) do
        table.insert(data.camadas, c:_to_table())
    end
    for _, o in ipairs(self._objetos) do
        table.insert(data.objetos, o)
    end
    return data
end

-- =============================================================================
-- Constantes de tile prontas
-- =============================================================================

local F = Mapa.F

Mapa.TILES = {
    GRAMA        = {col=0, lin=0, flags=F.NENHUM},
    TERRA        = {col=1, lin=0, flags=F.NENHUM},
    AREIA        = {col=2, lin=0, flags=F.NENHUM},
    NEVE         = {col=3, lin=0, flags=F.NENHUM},
    PAREDE       = {col=0, lin=1, flags=bor(F.COLISOR, F.SOMBRA)},
    PEDRA        = {col=1, lin=1, flags=F.COLISOR},
    TIJOLO       = {col=2, lin=1, flags=F.COLISOR},
    AGUA         = {col=0, lin=2, flags=F.AGUA},
    LAVA         = {col=1, lin=2, flags=bor(F.AGUA, F.COLISOR)},
    ESCADA_BAIXO = {col=0, lin=3, flags=F.ESCADA},
    ESCADA_CIMA  = {col=1, lin=3, flags=F.ESCADA},
    PORTA        = {col=0, lin=4, flags=F.TRIGGER},
    BAU          = {col=1, lin=4, flags=F.TRIGGER},
}

-- Extrai col, lin, flags de uma entrada de TILES, aplicando flags extras se necessário.
function Mapa.tile_de(t, flags_extra)
    local f = t.flags or 0
    if flags_extra then f = bor(f, flags_extra) end
    return t.col, t.lin, f
end

return Mapa