-- jogador.lua
player = {}
player.__index = player

local ANIMACOES = {
    baixo_andar    = { colunas = {3, 6}, linhas = {0}, loop = true  },
    cima_andar     = { colunas = {5, 8}, linhas = {0}, loop = true  },
    esquerda_andar = { colunas = {4, 7}, linhas = {0}, loop = true  },
    direita_andar  = { colunas = {4, 7}, linhas = {0}, loop = true  },
    baixo_ataque   = { colunas = {0, 1, 2}, linhas = {2}, loop = false },
    cima_ataque    = { colunas = {0, 1, 2}, linhas = {2}, loop = false },
    esquerda_ataque= { colunas = {0, 1, 2}, linhas = {2}, loop = false },
    direita_ataque = { colunas = {0, 1, 2}, linhas = {2}, loop = false },
}

local PARADO = {
    baixo    = { col = 0, lin = 0 },
    cima     = { col = 2, lin = 0 },
    esquerda = { col = 1, lin = 0 },
    direita  = { col = 1, lin = 0 },
}

function player:new(engine, w, h, x, y, sprite)
    local instancia = {
        engine    = engine,
        tamanho_w = w,
        tamanho_h = h,
        eixo_x    = x,
        eixo_y    = y,
        sprite_id = sprite,
        velocidade = 2,
        direcao   = "baixo",
        _anim_atual = "",  -- chave da animação rodando agora
    }
    instancia.obj_espada = nil 
    instancia.id_obj = engine:criar_objeto_tile(x, y, sprite, 0, 0, w, h)
    setmetatable(instancia, player)

    -- Cria uma animação para cada entrada da tabela
    instancia.anims = {}
    for chave, cfg in pairs(ANIMACOES) do
        instancia.anims[chave] = engine:criar_animacao(sprite, w, h, cfg.colunas, cfg.linhas, 5, cfg.loop, instancia.id_obj)
    end

    return instancia
end

-- Toca uma animação pelo nome — ignora se já estiver tocando
function player:_tocar(chave)
    if self._anim_atual == chave then return end
    local anim = self.anims[chave]
    if not anim then return end
    self.engine:anim_tocar(anim)
    self._anim_atual = chave
end

-- Para tudo e mostra o frame parado da direção atual
function player:_parar()
    local chave = "parado_" .. self.direcao
    if self._anim_atual == chave then return end
    local f = PARADO[self.direcao]
    for _, anim in pairs(self.anims) do
        self.engine:anim_parar(anim, f.col, f.lin)
    end
    self._anim_atual = chave
end

function player:mover(mapa)
    local tentar_x = self.eixo_x
    local tentar_y = self.eixo_y

    -- Ataque: espera terminar antes de aceitar qualquer input
    if self._anim_atual:find("_ataque") then
        local anim = self.anims[self._anim_atual]
        if not self.engine:anim_fim(anim) then
            self.engine:pos(self.id_obj, self.eixo_x, self.eixo_y)
            return
        end
        self:_parar()
    end

    local movendo = false
    if self.engine:tecla("w") then
        tentar_y      = self.eixo_y - self.velocidade
        self.direcao  = "cima"
        movendo       = true
    elseif self.engine:tecla("s") then
        tentar_y      = self.eixo_y + self.velocidade
        self.direcao  = "baixo"
        movendo       = true
    elseif self.engine:tecla("a") then
        tentar_x      = self.eixo_x - self.velocidade
        self.direcao  = "esquerda"
        self.engine:espelhar(self.id_obj, false, false)
        movendo       = true
    elseif self.engine:tecla("d") then
        tentar_x      = self.eixo_x + self.velocidade
        self.direcao  = "direita"
        self.engine:espelhar(self.id_obj, true, false)
        movendo       = true
    end

    if self.engine:tecla_press("e") then
        self:_tocar(self.direcao .. "_ataque")
        if self.direcao == "baixo" then
            self.obj_espada = self.engine:criar_objeto_tile(self.eixo_x + 10, self.eixo_y + 10, 1, 2, 1, 16, 16)
        end
        if self.direcao == "cima" then
            self.obj_espada = self.engine:criar_objeto_tile(self.eixo_x + 10, self.eixo_y - 10, 1, 2, 1, 16, 16)
        end
        if self.direcao == "esquerda" then
            self.obj_espada = self.engine:criar_objeto(self.eixo_x - 10, self.eixo_y + 10, 1, 15, 5, 200, 200, 255)
        end
        if self.direcao == "direita" then
            self.obj_espada = self.engine:criar_objeto(self.eixo_x + 10, self.eixo_y + 10, 1, 15, 5, 200, 200, 255)
        end
    elseif movendo then
        self:_tocar(self.direcao .. "_andar")
        if self.obj_espada then
            self.engine:remover_objeto(self.obj_espada)
            self.obj_espada = nil
        end
    else
        self:_parar()
        if self.obj_espada then
            self.engine:remover_objeto(self.obj_espada)
            self.obj_espada = nil
        end
    end

    if not mapa:tem_colisao(tentar_x, self.eixo_y, self.tamanho_w, self.tamanho_h) then
        self.eixo_x = tentar_x
    end
    if not mapa:tem_colisao(self.eixo_x, tentar_y, self.tamanho_w, self.tamanho_h) then
        self.eixo_y = tentar_y
    end

    self.engine:pos(self.id_obj, self.eixo_x, self.eixo_y)
end

return player