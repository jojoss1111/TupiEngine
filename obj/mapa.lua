-- mapa.lua
local Mapa = {}
Mapa.__index = Mapa

local TILE_CHAO   = 0
local TILE_PAREDE = 1

local TILESET = {
    [TILE_CHAO]   = { col = 0, lin = 2 },
    [TILE_PAREDE] = { col = 5, lin = 3 },
}

local DADOS = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
}

function Mapa:new(engine, tamanho_tile, caminho_png)
    local inst = setmetatable({}, Mapa)

    inst.engine      = engine
    inst.tile_w      = tamanho_tile
    inst.tile_h      = tamanho_tile
    inst.dados       = DADOS
    inst.num_linhas  = #DADOS
    inst.num_colunas = #DADOS[1]

    -- Largura / altura total do mapa em pixels (útil para limitar câmera)
    inst.largura_px  = inst.num_colunas * tamanho_tile
    inst.altura_px   = inst.num_linhas  * tamanho_tile

    -- Carrega o sprite completo do tileset (engine_draw_tilemap precisa do sprite inteiro)
    inst.sprite_id = caminho_png and engine:carregar_sprite(caminho_png) or -1

    -- Monta a tabela linear que engine:desenhar_tilemap() espera.
    -- Cada valor é o índice linear do tile no tileset: col + lin * (tileset_w / tile_w)
    -- Calculamos aqui uma vez para não repetir no loop.
    inst._tilemap_linear = inst:_construir_tilemap_linear()

    return inst
end

function Mapa:_construir_tilemap_linear()
    -- Calcula tiles_por_linha com base no maior col definido em TILESET + 1
    -- (a engine vai usar isso para calcular src_x = tile_id % tiles_por_linha)
    -- Mas engine_draw_tilemap usa: src_x = (tile_id % tpr) * tile_w
    --                              src_y = (tile_id / tpr) * tile_h
    -- onde tpr = largura_do_sprite / tile_w.
    -- Então construímos o ID como: col + lin * tpr
    -- Não temos tpr aqui, mas podemos usar um valor fixo ou calcular do maior col.
    -- Como o tileset da casa tem pelo menos 8 colunas (col=5 é parede), usamos 8.
    local tpr = 8  -- tiles por linha no tileset (ajuste se o PNG tiver outra largura)

    local linear = {}
    for lin = 1, self.num_linhas do
        for col = 1, self.num_colunas do
            local valor = self.dados[lin][col]
            local ts    = TILESET[valor]
            local id    = ts.col + ts.lin * tpr
            linear[#linear + 1] = id
        end
    end
    return linear
end

function Mapa:desenhar(cam_x, cam_y)
    local ox = -(cam_x or 0)
    local oy = -(cam_y or 0)

    self.engine:tilemap(
        self._tilemap_linear,
        self.num_linhas,
        self.num_colunas,
        self.sprite_id,
        self.tile_w,
        self.tile_h,
        ox, oy
    )
end


function Mapa:tem_colisao(x, y, w, h)
    local t  = self.tile_w
    local c0 = math.floor(x / t) + 1
    local c1 = math.floor((x + w - 1) / t) + 1
    local l0 = math.floor(y / t) + 1
    local l1 = math.floor((y + h - 1) / t) + 1

    for lin = l0, l1 do
        for col = c0, c1 do
            local linha = self.dados[lin]
            if linha and linha[col] == TILE_PAREDE then
                return true
            end
        end
    end
    return false
end

return Mapa