--[[
    mapa.lua — Módulo de descrição de mapas para a engine 2D.

    Este arquivo NÃO renderiza nada. Ele apenas expõe funções que ajudam
    a montar a tabela MapData que será passada para o C++ via FFI.

    O main.lua importa este módulo, constrói o mapa com as funções aqui
    definidas, e envia a tabela para mapa_carregar_lua() do C++.

    Uso básico em main.lua:
        local Mapa = require("mapa")

        local m = Mapa.novo(20, 15, 16, 16)          -- cols, linhas, tw, th
        m:atlas("assets/tileset.png")

        local chao = m:camada("chao", 0)             -- nome, z_order
        chao:fill(0, 0, 19, 14, 1, 0)                -- preenche toda área com tile col=1, lin=0
        chao:tile(5, 3, 2, 0, {"colisor"})           -- tile especial na coluna 5, linha 3

        local obj  = m:camada("objetos", 1)
        m:objeto(1, "bau", 5, 3, 1.5, {item="espada", quantidade="1"})

        return m:build()                              -- retorna tabela MapData para o C++
]]

local Mapa = {}
Mapa.__index = Mapa

-- =============================================================================
-- Camada
-- =============================================================================

local Camada = {}
Camada.__index = Camada

--[[
    :tile(col, lin, sprite_col, sprite_lin, flags, sprite)
    Define um tile específico na camada.

    col, lin      → posição no mapa (em tiles)
    sprite_col    → coluna na spritesheet
    sprite_lin    → linha na spritesheet
    flags         → tabela de strings: {"colisor", "trigger", ...}
    sprite        → caminho para sprite customizado (nil = usa atlas)

    Exemplo:
        chao:tile(3, 2, 0, 1, {"colisor"})
        chao:tile(7, 0, 2, 3, {}, "assets/water_animated.png")
]]
function Camada:tile(col, lin, sprite_col, sprite_lin, flags, sprite)
    table.insert(self._tiles, {
        col        = col,
        lin        = lin,
        sprite_col = sprite_col or 0,
        sprite_lin = sprite_lin or 0,
        flags      = flags or {},
        sprite     = sprite,  -- nil = usa atlas do mapa
    })
    return self
end

--[[
    :tile_animado(col, lin, frames, fps, flags)
    Define um tile com animação.

    frames → lista de sprite_ids (pré-carregados) para os frames
    fps    → velocidade da animação em frames por segundo

    Exemplo — água animada com 4 frames a 8 fps:
        chao:tile_animado(2, 5, {sid_agua1, sid_agua2, sid_agua3, sid_agua4}, 8)
]]
function Camada:tile_animado(col, lin, frames, fps, flags)
    local t = {
        col         = col,
        lin         = lin,
        sprite_col  = 0,
        sprite_lin  = 0,
        flags       = flags or {"animado"},
        anim_fps    = fps or 4,
        anim_frames = frames or {},
    }
    if not t.flags then t.flags = {} end
    -- garante que o flag "animado" está na lista
    local tem = false
    for _, v in ipairs(t.flags) do if v == "animado" then tem = true end end
    if not tem then table.insert(t.flags, "animado") end
    table.insert(self._tiles, t)
    return self
end

--[[
    :fill(col_ini, lin_ini, col_fim, lin_fim, sprite_col, sprite_lin, flags)
    Preenche um retângulo inteiro de tiles com o mesmo sprite.

    Exemplo — preencher o chão todo com o tile (0,0) da spritesheet:
        chao:fill(0, 0, 19, 14, 0, 0)

    Exemplo — borda sólida ao redor do mapa (20x15):
        chao:fill(0, 0, 19, 0,  2, 1, {"colisor"})  -- topo
        chao:fill(0,14, 19,14,  2, 1, {"colisor"})  -- base
        chao:fill(0, 0,  0,14,  2, 1, {"colisor"})  -- esquerda
        chao:fill(19,0, 19,14,  2, 1, {"colisor"})  -- direita
]]
function Camada:fill(col_ini, lin_ini, col_fim, lin_fim,
                     sprite_col, sprite_lin, flags)
    for l = lin_ini, lin_fim do
        for c = col_ini, col_fim do
            self:tile(c, l, sprite_col, sprite_lin, flags)
        end
    end
    return self
end

--[[
    :borda(largura_mapa, altura_mapa, sprite_col, sprite_lin)
    Atalho para criar uma borda sólida ao redor do mapa inteiro.

    Exemplo:
        chao:borda(20, 15, 3, 0)   -- tile (3,0) como parede nas bordas
]]
function Camada:borda(larg, alt, sc, sl)
    local f = {"colisor", "sombra"}
    self:fill(0,     0,      larg-1, 0,     sc, sl, f)
    self:fill(0,     alt-1,  larg-1, alt-1, sc, sl, f)
    self:fill(0,     0,      0,      alt-1, sc, sl, f)
    self:fill(larg-1,0,      larg-1, alt-1, sc, sl, f)
    return self
end

--[[
    :linha_h(col_ini, col_fim, lin, sprite_col, sprite_lin, flags)
    Desenha uma linha horizontal de tiles.
]]
function Camada:linha_h(ci, cf, lin, sc, sl, flags)
    for c = ci, cf do
        self:tile(c, lin, sc, sl, flags)
    end
    return self
end

--[[
    :linha_v(col, lin_ini, lin_fim, sprite_col, sprite_lin, flags)
    Desenha uma linha vertical de tiles.
]]
function Camada:linha_v(col, li, lf, sc, sl, flags)
    for l = li, lf do
        self:tile(col, l, sc, sl, flags)
    end
    return self
end

-- Converte a camada em tabela compatível com o loader C++
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

--[[
    Mapa.novo(largura, altura, tile_w, tile_h)
    Cria um novo mapa vazio.

    largura, altura → dimensões em tiles
    tile_w, tile_h  → dimensões de cada tile em pixels
]]
function Mapa.novo(largura, altura, tile_w, tile_h)
    local self = setmetatable({}, Mapa)
    self.largura  = largura  or 20
    self.altura   = altura   or 15
    self.tile_w   = tile_w   or 16
    self.tile_h   = tile_h   or 16
    self._atlas   = nil
    self._camadas = {}
    self._objetos = {}
    return self
end

--[[
    :atlas(caminho)
    Define o spritesheet principal usado por todos os tiles sem sprite próprio.

    Exemplo:
        m:atlas("assets/tileset.png")
]]
function Mapa:atlas(caminho)
    self._atlas = caminho
    return self
end

--[[
    :camada(nome, z_order, visivel)
    Cria e registra uma nova camada de tiles. Retorna o objeto Camada.

    nome    → identificador (ex: "chao", "paredes", "decoracao")
    z_order → ordem de renderização (menor = desenhado primeiro)
    visivel → true por padrão

    Exemplo:
        local chao     = m:camada("chao",     0)
        local paredes  = m:camada("paredes",  1)
        local itens    = m:camada("itens",    2)
]]
function Mapa:camada(nome, z_order, visivel)
    local c = setmetatable({
        nome    = nome,
        z_order = z_order or #self._camadas,
        visivel = visivel ~= false,  -- padrão true
        _tiles  = {},
    }, Camada)
    table.insert(self._camadas, c)
    return c
end

--[[
    :objeto(id, tipo, col, lin, raio, props, sprite)
    Registra um objeto especial no mapa (baú, NPC, porta, teleporte...).

    id     → identificador único do objeto
    tipo   → string: "bau", "porta", "npc", "teleporte", "script", "generico"
    col    → coluna do tile onde o objeto está
    lin    → linha do tile onde o objeto está
    raio   → distância em tiles para ativar o trigger (padrão 1.5)
    props  → tabela chave=valor com propriedades extras
    sprite → caminho para sprite do objeto (opcional)

    Exemplo — baú com item:
        m:objeto(1, "bau", 5, 3, 1.5, {item="espada", quantidade="1"})

    Exemplo — NPC com diálogo:
        m:objeto(2, "npc", 10, 7, 2.0,
            {nome="Velho", dialogo="Bem vindo, aventureiro!"},
            "assets/npc_velho.png")

    Exemplo — teleporte para outro mapa:
        m:objeto(3, "teleporte", 19, 7, 1.0,
            {destino="masmorra.lua", dest_col="1", dest_lin="7"})
]]
function Mapa:objeto(id, tipo, col, lin, raio, props, sprite)
    table.insert(self._objetos, {
        id     = id,
        tipo   = tipo   or "generico",
        col    = col    or 0,
        lin    = lin    or 0,
        raio   = raio   or 1.5,
        props  = props  or {},
        sprite = sprite,
        ativo  = true,
    })
    return self
end

--[[
    :build()
    Serializa o mapa para a tabela MapData que o C++ espera.
    Chame ao final da construção e retorne o resultado.

    Exemplo em main.lua:
        return m:build()
]]
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
-- Constantes de tile prontas para uso no main.lua
--
-- Use estas constantes para nomear seus tiles de forma legível.
-- Ajuste os valores (col, lin) conforme o seu spritesheet.
-- =============================================================================

Mapa.TILES = {
    -- Terreno
    GRAMA        = {col=0, lin=0},
    TERRA        = {col=1, lin=0},
    AREIA        = {col=2, lin=0},
    NEVE         = {col=3, lin=0},

    -- Estruturas
    PAREDE       = {col=0, lin=1, flags={"colisor","sombra"}},
    PEDRA        = {col=1, lin=1, flags={"colisor"}},
    TIJOLO       = {col=2, lin=1, flags={"colisor"}},

    -- Líquidos
    AGUA         = {col=0, lin=2, flags={"agua"}},
    LAVA         = {col=1, lin=2, flags={"agua","colisor"}},

    -- Navegação
    ESCADA_BAIXO = {col=0, lin=3, flags={"escada"}},
    ESCADA_CIMA  = {col=1, lin=3, flags={"escada"}},

    -- Interativos
    PORTA        = {col=0, lin=4, flags={"trigger"}},
    BAU          = {col=1, lin=4, flags={"trigger"}},
}

--[[
    Mapa.tile_de(constante, col_override, lin_override)
    Atalho para obter os parâmetros de um tile a partir de uma constante.

    Exemplo:
        chao:tile(3, 2, Mapa.tile_de(Mapa.TILES.PAREDE))
]]
function Mapa.tile_de(t, flags_extra)
    local f = {}
    if t.flags then for _, v in ipairs(t.flags) do table.insert(f, v) end end
    if flags_extra then for _, v in ipairs(flags_extra) do table.insert(f, v) end end
    return t.col, t.lin, f
end

return Mapa