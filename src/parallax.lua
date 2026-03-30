-- parallax.lua
-- Sistema de Múltiplas Camadas de Background com Parallax Infinito.

local ffi = require("engineffi")

-- ============================================================
-- Módulo
-- ============================================================
local Parallax = {}
Parallax.__index = Parallax

-- ============================================================
-- Parallax.novo(e, render_w, render_h)
--
--   e          — instância de Engine (wrapper engine.lua)
--   render_w   — largura lógica do render em pixels virtuais
--   render_h   — altura  lógica do render em pixels virtuais
--
-- Retorna: instância de Parallax
-- ============================================================
function Parallax.novo(e, render_w, render_h)
    local self = setmetatable({}, Parallax)
    self._e        = e
    self._rw       = render_w
    self._rh       = render_h
    self._camadas  = {}   -- array ordenado de camadas
    self._tempo    = 0.0  -- acumulador de tempo para auto-scroll
    return self
end

-- ============================================================
-- _normalizar_cfg(cfg) — preenche valores padrão de uma config
-- ============================================================
local function _normalizar_cfg(cfg)
    cfg = cfg or {}
    local c = {}
    c.fator_x = cfg.fator_x ~= nil and cfg.fator_x or 0.0
    c.fator_y = cfg.fator_y ~= nil and cfg.fator_y or (c.fator_x * 0.5)
    c.alpha   = cfg.alpha   ~= nil and cfg.alpha   or 1.0
    c.escala  = cfg.escala  ~= nil and cfg.escala  or 1.0
    c.tile_x  = cfg.tile_x  ~= nil and cfg.tile_x  or true
    c.tile_y  = cfg.tile_y  ~= nil and cfg.tile_y  or false
    c.vel_x   = cfg.vel_x   ~= nil and cfg.vel_x   or 0.0
    c.vel_y   = cfg.vel_y   ~= nil and cfg.vel_y   or 0.0
    c.tint_r  = cfg.tint_r  ~= nil and cfg.tint_r  or 255
    c.tint_g  = cfg.tint_g  ~= nil and cfg.tint_g  or 255
    c.tint_b  = cfg.tint_b  ~= nil and cfg.tint_b  or 255
    -- offsets acumulados do auto-scroll
    c._scroll_x = 0.0
    c._scroll_y = 0.0
    return c
end

-- ============================================================
-- adicionar_sprite(caminho, cfg)
--
--   Carrega um PNG e adiciona como camada de parallax.
--   O sprite é tile-ado automaticamente para cobrir a tela.
--
--   caminho — path relativo ao PNG
--   cfg     — tabela de configuração (ver referência no topo)
--   Retorna: índice da camada (base 1)
-- ============================================================
function Parallax:adicionar_sprite(caminho, cfg)
    local c      = _normalizar_cfg(cfg)
    c.tipo       = "sprite"
    c.sprite_id  = self._e:carregar_sprite(caminho)
    if c.sprite_id < 0 then
        error("Parallax: não foi possível carregar '" .. caminho .. "'")
    end
    local sp = self._e._e.sprites[c.sprite_id]
    c.img_w  = sp.width  * c.escala
    c.img_h  = sp.height * c.escala
    table.insert(self._camadas, c)
    return #self._camadas
end

-- ============================================================
-- adicionar_sprite_regiao(caminho, rx,ry,rw,rh, cfg)
--
--   Recorta uma região do PNG para usar como camada.
--   Útil para sprite sheets onde cada linha é um background.
-- ============================================================
function Parallax:adicionar_sprite_regiao(caminho, rx, ry, rw, rh, cfg)
    local c      = _normalizar_cfg(cfg)
    c.tipo       = "sprite"
    c.sprite_id  = self._e:carregar_sprite_regiao(caminho, rx, ry, rw, rh)
    if c.sprite_id < 0 then
        error("Parallax: não foi possível carregar região de '" .. caminho .. "'")
    end
    c.img_w  = rw * c.escala
    c.img_h  = rh * c.escala
    table.insert(self._camadas, c)
    return #self._camadas
end

-- ============================================================
-- adicionar_cor(r, g, b, cfg)
--
--   Adiciona uma camada de cor sólida (fundo plano ou gradiente simples).
--   O alpha em cfg.alpha controla a opacidade.
-- ============================================================
function Parallax:adicionar_cor(r, g, b, cfg)
    local c   = _normalizar_cfg(cfg)
    c.tipo    = "cor"
    c.cor_r   = r or 0
    c.cor_g   = g or 0
    c.cor_b   = b or 0
    -- Camadas de cor ignoram fator (sempre fixas) — fator forçado a 0
    c.fator_x = 0.0
    c.fator_y = 0.0
    table.insert(self._camadas, c)
    return #self._camadas
end

-- ============================================================
-- adicionar_tilemap(mapa, linhas, colunas, sprite_id, tw, th, cfg)
--
--   Camada baseada em tilemap (ex.: chão de grama replicado).
--   O tilemap é desenhado com offset calculado pelo fator de parallax
--   e tile-ado caso o deslocamento ultrapasse os limites do mapa.
--
--   mapa    — tabela Lua linear (igual a E:tilemap())
--   linhas  — número de linhas do tilemap
--   colunas — número de colunas
--   sid     — sprite_id do tileset
--   tw, th  — dimensões de cada tile em pixels virtuais
-- ============================================================
function Parallax:adicionar_tilemap(mapa, linhas, colunas, sid, tw, th, cfg)
    local c     = _normalizar_cfg(cfg)
    c.tipo      = "tilemap"
    c.mapa      = mapa
    c.linhas    = linhas
    c.colunas   = colunas
    c.sid       = sid
    c.tw        = tw
    c.th        = th
    c.img_w     = colunas * tw * c.escala
    c.img_h     = linhas  * th * c.escala
    table.insert(self._camadas, c)
    return #self._camadas
end

-- ============================================================
-- _offset_parallax(c, cam_x, cam_y) → ox, oy
--
--   Calcula o offset de desenho para uma camada dado a posição
--   de câmera (ou jogador) e o fator de parallax.
--
--   A fórmula é:
--     offset = -(cam * fator) + scroll_automatico
--
--   O módulo (mod) garante tiling infinito: o offset é restrito
--   ao intervalo [0, img_w) de modo que a imagem se repita sem
--   saltos visíveis.
-- ============================================================
local function _offset_parallax(c, cam_x, cam_y)
    local ox = -(cam_x * c.fator_x) + c._scroll_x
    local oy = -(cam_y * c.fator_y) + c._scroll_y

    -- Wrapping: mantém ox/oy dentro de [-img_w, 0] para tiling suave
    if c.img_w and c.img_w > 0 then
        ox = ox % c.img_w
        if ox > 0 then ox = ox - c.img_w end
    end
    if c.img_h and c.img_h > 0 then
        oy = oy % c.img_h
        if oy > 0 then oy = oy - c.img_h end
    end

    return ox, oy
end

-- ============================================================
-- _desenhar_sprite_tiled(e, c, ox, oy, rw, rh)
--
--   Desenha o sprite da camada tile-ando para cobrir [0, rw] × [0, rh].
--   ox/oy são o offset de início (sempre <= 0 após _offset_parallax).
-- ============================================================
local function _desenhar_sprite_tiled(e, c, ox, oy, rw, rh)
    local iw = c.img_w
    local ih = c.img_h

    -- Limites de iteração: quantas cópias cobrem a tela
    local x_copies = c.tile_x and math.ceil((rw - ox) / iw) or 1
    local y_copies = c.tile_y and math.ceil((rh - oy) / ih) or 1

    local tr = c.tint_r / 255.0
    local tg = c.tint_g / 255.0
    local tb = c.tint_b / 255.0

    local sp = e._e.sprites[c.sprite_id]
    local sw = sp.width
    local sh = sp.height

    for cy = 0, y_copies - 1 do
        local py = oy + cy * ih
        -- Clamp vertical se não tile-ar
        if not c.tile_y then py = 0 end

        for cx = 0, x_copies - 1 do
            local px = ox + cx * iw
            -- Clamp horizontal se não tile-ar
            if not c.tile_x then px = 0 end

            ffi.C.engine_draw_sprite_part_ex(
                e._e,
                c.sprite_id,
                math.floor(px), math.floor(py),
                0, 0, sw, sh,
                c.escala, c.escala,
                0.0,       -- sem rotação
                c.alpha,
                0, 0       -- sem flip
            )

            if not c.tile_x then break end
        end
        if not c.tile_y then break end
    end
end

-- ============================================================
-- _desenhar_tilemap_tiled(e, c, ox, oy, rw, rh)
--
--   Desenha o tilemap tile-ando quando necessário.
--   Como engine_draw_tilemap aceita offset_x/y, aproveitamos isso
--   para o wrap horizontal.  Para tile_y, desenhamos múltiplas passadas.
-- ============================================================
local function _desenhar_tilemap_tiled(e, c, ox, oy, rw, rh)
    local mw = c.colunas * c.tw
    local mh = c.linhas  * c.th

    local x_copies = c.tile_x and math.ceil((rw - ox) / mw) or 1
    local y_copies = c.tile_y and math.ceil((rh - oy) / mh) or 1

    -- Constrói buffer C uma única vez e reutiliza nas passadas
    local n = c.linhas * c.colunas
    local buf = ffi.new("int[?]", n)
    for i = 1, n do buf[i-1] = c.mapa[i] or 0 end

    for cy = 0, y_copies - 1 do
        local py = math.floor(oy + cy * mh)
        if not c.tile_y then py = math.floor(oy) end

        for cx = 0, x_copies - 1 do
            local px = math.floor(ox + cx * mw)
            if not c.tile_x then px = math.floor(ox) end

            ffi.C.engine_draw_tilemap(
                e._e, buf,
                c.linhas, c.colunas,
                c.sid, c.tw, c.th,
                px, py
            )

            if not c.tile_x then break end
        end
        if not c.tile_y then break end
    end
end

-- ============================================================
-- desenhar(cam_x, cam_y, dt)
--
--   Desenha todas as camadas em ordem (índice 1 = mais ao fundo).
--
--   cam_x, cam_y — posição da câmera (ou player.x, player.y).
--                  Normalmente é e._e.camera.x / camera.y quando
--                  a câmera da engine está ativa.
--   dt           — delta time em segundos (para auto-scroll).
--                  Opcional; se omitido usa e:delta().
--
--   Deve ser chamado ANTES de engine_camera_push() / engine_draw()
--   para que o parallax fique atrás dos objetos do mundo.
-- ============================================================
function Parallax:desenhar(cam_x, cam_y, dt)
    local e  = self._e
    local rw = self._rw
    local rh = self._rh
    dt = dt or e:delta()

    for _, c in ipairs(self._camadas) do
        -- Atualiza scroll automático
        if c.vel_x ~= 0 or c.vel_y ~= 0 then
            c._scroll_x = c._scroll_x + c.vel_x * dt
            c._scroll_y = c._scroll_y + c.vel_y * dt
        end

        if c.tipo == "cor" then
            -- Overlay de cor simples — cobre toda a tela
            ffi.C.engine_draw_overlay(
                e._e, 0, 0, rw, rh,
                c.cor_r, c.cor_g, c.cor_b,
                c.alpha
            )

        elseif c.tipo == "sprite" then
            local ox, oy = _offset_parallax(c, cam_x, cam_y)
            _desenhar_sprite_tiled(e, c, ox, oy, rw, rh)

        elseif c.tipo == "tilemap" then
            local ox, oy = _offset_parallax(c, cam_x, cam_y)
            _desenhar_tilemap_tiled(e, c, ox, oy, rw, rh)
        end
    end

    -- Flush único ao final para submeter todos os quads acumulados
    ffi.C.engine_flush(e._e)
end

-- ============================================================
-- Getters / setters de camada por índice
-- ============================================================

--- Retorna a tabela interna de uma camada para inspeção direta.
function Parallax:camada(idx) return self._camadas[idx] end

--- Quantidade de camadas registradas.
function Parallax:num_camadas() return #self._camadas end

--- Altera o fator de parallax de uma camada em tempo de execução.
function Parallax:set_fator(idx, fx, fy)
    local c = self._camadas[idx]
    if not c then return end
    c.fator_x = fx
    c.fator_y = fy ~= nil and fy or (fx * 0.5)
end

--- Altera o alpha de uma camada.
function Parallax:set_alpha(idx, a)
    local c = self._camadas[idx]
    if c then c.alpha = a end
end

--- Define velocidade de auto-scroll em px/s para uma camada.
function Parallax:set_scroll(idx, vx, vy)
    local c = self._camadas[idx]
    if not c then return end
    c.vel_x = vx or 0
    c.vel_y = vy or 0
end

--- Reposiciona o offset acumulado de auto-scroll (útil ao trocar de fase).
function Parallax:reset_scroll(idx)
    local c = self._camadas[idx]
    if c then c._scroll_x = 0.0; c._scroll_y = 0.0 end
end

--- Remove uma camada. Reordena o array.
function Parallax:remover(idx)
    table.remove(self._camadas, idx)
end

--- Reposiciona uma camada (move do índice from para to).
function Parallax:reordenar(from, to)
    local c = table.remove(self._camadas, from)
    table.insert(self._camadas, to, c)
end

return Parallax
