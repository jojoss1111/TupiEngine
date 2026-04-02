--mapa.lua
local bit = require("bit")
local bor, lshift = bit.bor, bit.lshift

local Mapa = {}
Mapa.__index = Mapa

-- Flags de bloco (bitmask, espelham MAPA_FLAG_* do C++)
Mapa.F = {
    NENHUM  = 0,
    COLISOR = lshift(1, 0),   -- 0x01
    TRIGGER = lshift(1, 1),   -- 0x02
    AGUA    = lshift(1, 2),   -- 0x04
    ESCADA  = lshift(1, 3),   -- 0x08
    SOMBRA  = lshift(1, 4),   -- 0x10
    ANIMADO = lshift(1, 5),   -- 0x20
}

-- =============================================================================
-- Camada — interface interna; populada via carregar_matriz()
-- =============================================================================

local Camada = {}
Camada.__index = Camada

-- Insere um tile estático na camada.
-- Uso interno — chamado por carregar_matriz() com dados do template.
function Camada:_add_tile(col, lin, sprite_col, sprite_lin, flags, sprite)
    table.insert(self._tiles, {
        col        = col,
        lin        = lin,
        sprite_col = sprite_col,
        sprite_lin = sprite_lin,
        flags      = flags,
        sprite     = sprite,   -- nil = usa atlas do mapa
    })
end

-- Insere um tile animado na camada.
-- Uso interno — chamado por carregar_matriz() quando o template tem anim_cols.
function Camada:_add_tile_animado(col, lin, sprite_lin, anim_cols, anim_lins,
                                   fps, loop, flags, sprite)
    table.insert(self._tiles, {
        col        = col,
        lin        = lin,
        sprite_col = anim_cols[1],   -- frame inicial
        sprite_lin = sprite_lin,
        flags      = bor(flags, Mapa.F.ANIMADO),
        sprite     = sprite,
        anim_fps   = fps,
        anim_cols  = anim_cols,
        anim_lins  = anim_lins,      -- nil = todas na mesma linha (sprite_lin)
        anim_loop  = loop,
    })
end

-- Serializa a camada para o loader C++.
function Camada:_to_table()
    return {
        nome    = self.nome,
        z_order = self.z_order,
        visivel = self.visivel,
        tiles   = self._tiles,
    }
end

-- =============================================================================
-- Mapa
-- =============================================================================

-- Mapa.novo(largura, altura, tile_w, tile_h)
function Mapa.novo(largura, altura, tile_w, tile_h)
    local self = setmetatable({}, Mapa)
    self.largura    = largura or 20
    self.altura     = altura  or 15
    self.tile_w     = tile_w  or 16
    self.tile_h     = tile_h  or 16
    self._atlas     = nil
    self._camadas   = {}
    self._objetos   = {}
    self._templates = {}   -- protótipos registrados por criar_bloco()
    return self
end

-- :atlas(caminho)  — spritesheet principal do mapa
function Mapa:atlas(caminho)
    self._atlas = caminho
    return self
end

-- :camada(nome, z_order, visivel)
-- Cria uma camada e a registra no mapa.
-- z_order menor = desenhado primeiro (fundo).
function Mapa:camada(nome, z_order, visivel)
    local c = setmetatable({
        nome    = nome,
        z_order = z_order or #self._camadas,
        visivel = visivel ~= false,
        _tiles  = {},
    }, Camada)
    table.insert(self._camadas, c)
    return c
end

-- :objeto(nome, id, col, lin, raio, sprite, anim_cols, anim_lin, fps, loop)
--
--   nome      — nome descritivo (ex: "Bau", "Porta")
--   id        — identificador único (inteiro)
--   col, lin  — posição em tiles no mapa
--   raio      — distância (em tiles) para ativar o trigger
--   sprite    — caminho da spritesheet (nil = usa atlas do mapa)
--   anim_cols — {0,1,2,3} para objeto animado  (nil = estático)
--   anim_lin  — linha fixa no atlas durante a animação
--   fps       — frames por segundo (ignorado se anim_cols for nil)
--   loop      — true = anima em loop; false = congela no último frame
--
-- Exemplo estático:  M:objeto("Bau",   1, 5, 3, 1.5, "sprites/itens.png")
-- Exemplo animado:   M:objeto("Fogueira", 2, 8, 4, 2.0, nil, {0,1,2}, 1, 6, true)
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
        -- animação (opcionais)
        anim_cols = anim_cols,                        -- nil = estático
        anim_lin  = anim_lin  or 0,
        anim_fps  = fps       or 4,
        anim_loop = (loop ~= false),                  -- padrão true
    }
    table.insert(self._objetos, obj)
    return obj
end

-- :criar_bloco(nome, id, col, lin, colide, loop, fps, camada, sprite)
--
--   nome   — nome descritivo do bloco (só para debug)
--   id     — chave usada na matriz de layout (inteiro > 0)
--   col    — coluna(s) no atlas:
--              número  → tile estático        ex: 3
--              tabela  → frames de animação   ex: {0,1,2,3}
--   lin    — linha(s) no atlas:
--              número  → linha fixa           ex: 2
--              tabela  → uma linha por frame  ex: {0,0,1,1}
--              (ignorado se col for número)
--   colide — true/1 → adiciona flag COLISOR automaticamente
--   loop   — true = animação em loop; false = congela no último frame
--              (ignorado para tiles estáticos)
--   fps    — frames por segundo da animação (padrão 4)
--   camada — índice da camada de destino (0-based, padrão 0)
--   sprite — spritesheet alternativa (nil = usa atlas do mapa)
--
-- Exemplos:
--   M:criar_bloco("Grama",    1,  0,      0,     false, true,  0)
--   M:criar_bloco("Parede",   2,  1,      1,     true,  true,  0)
--   M:criar_bloco("Agua",     3, {0,1,2}, 2,     false, true,  6)
--   M:criar_bloco("Lava",     4, {0,1,2}, {2,2,3}, true, true, 8)
function Mapa:criar_bloco(nome, id, col, lin, colide, loop, fps, camada, sprite)
    local F    = Mapa.F
    local flags = 0

    if colide then flags = bor(flags, F.COLISOR) end

    local animado   = type(col) == "table"
    local anim_cols = nil
    local anim_lins = nil
    local sprite_col, sprite_lin

    if animado then
        flags       = bor(flags, F.ANIMADO)
        anim_cols   = col                              -- sequência de colunas
        sprite_col  = col[1]                          -- frame inicial

        if type(lin) == "table" then
            anim_lins  = lin
            sprite_lin = lin[1]
        else
            sprite_lin = lin or 0                     -- linha fixa
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
        camada     = camada or 0,
        sprite     = sprite,
        -- animação
        animado    = animado,
        anim_cols  = anim_cols,
        anim_lins  = anim_lins,
        anim_fps   = fps  or 4,
        anim_loop  = (loop ~= false),                 -- padrão true
    }
    return id
end

-- :carregar_matriz(layout, camada_idx)
--
-- Preenche o mapa com um array 1D de IDs de bloco (0 ou nil = vazio).
-- Cria camadas automaticamente se necessário.
-- O tile animado ou estático é escolhido conforme o template.
function Mapa:carregar_matriz(layout, camada_idx)
    camada_idx = camada_idx or 0

    for i, bloco_id in ipairs(layout) do
        if bloco_id and bloco_id ~= 0 then
            local t = self._templates[bloco_id]
            if t then
                local col = (i - 1) % self.largura
                local lin = math.floor((i - 1) / self.largura)
                local cam = t.camada or camada_idx

                -- garante que a camada existe
                while #self._camadas < cam + 1 do
                    self:camada("camada_" .. #self._camadas, #self._camadas)
                end

                local c = self._camadas[cam + 1]

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
    return self
end

-- :build()  — serializa o mapa para a engine; chame no final e retorne o resultado
function Mapa:build()
    local data = {
        largura      = self.largura,
        altura       = self.altura,
        tile_w       = self.tile_w,
        tile_h       = self.tile_h,
        sprite_atlas = self._atlas,
        camadas      = {},
        objetos      = {},
    }
    for _, c in ipairs(self._camadas) do
        table.insert(data.camadas, c:_to_table())
    end
    for _, o in ipairs(self._objetos) do
        table.insert(data.objetos, o)
    end
    return data
end

-- =============================================================================
-- Constantes de tile prontas (ajuste col/lin conforme seu spritesheet)
-- =============================================================================

local F = Mapa.F

Mapa.TILES = {
    -- Terreno
    GRAMA        = {col=0, lin=0, flags=F.NENHUM},
    TERRA        = {col=1, lin=0, flags=F.NENHUM},
    AREIA        = {col=2, lin=0, flags=F.NENHUM},
    NEVE         = {col=3, lin=0, flags=F.NENHUM},

    -- Estruturas
    PAREDE       = {col=0, lin=1, flags=bor(F.COLISOR, F.SOMBRA)},
    PEDRA        = {col=1, lin=1, flags=F.COLISOR},
    TIJOLO       = {col=2, lin=1, flags=F.COLISOR},

    -- Líquidos
    AGUA         = {col=0, lin=2, flags=F.AGUA},
    LAVA         = {col=1, lin=2, flags=bor(F.AGUA, F.COLISOR)},

    -- Navegação
    ESCADA_BAIXO = {col=0, lin=3, flags=F.ESCADA},
    ESCADA_CIMA  = {col=1, lin=3, flags=F.ESCADA},

    -- Interativos
    PORTA        = {col=0, lin=4, flags=F.TRIGGER},
    BAU          = {col=1, lin=4, flags=F.TRIGGER},
}

-- Mapa.tile_de(constante, flags_extra?)  → sc, sl, flags
function Mapa.tile_de(t, flags_extra)
    local f = t.flags or 0
    if flags_extra then f = bor(f, flags_extra) end
    return t.col, t.lin, f
end

return Mapa