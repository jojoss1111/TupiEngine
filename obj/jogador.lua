-- jogador.lua
player = {}
player.__index = player

function player:new(engine, w, h, x, y)
    local instancia = {
        engine = engine,
        tamanho_w = w,
        tamanho_h = h,
        eixo_x = x,
        eixo_y = y,
        velocidade = 2,
        direcao = "baixo",
    }
    
    instancia.id_obj = engine:adicionar_objeto(x, y, 0, w, h, 100, 0, 0)
    
    setmetatable(instancia, player)    
    return instancia
end

function player:mover(mapa)
    local tentar_x = self.eixo_x
    local tentar_y = self.eixo_y

    -- LÓGICA HORIZONTAL (X)
    if self.engine:tecla_pressionada("d") then
        tentar_x = self.eixo_x + self.velocidade
        self.direcao = "direita"
    elseif self.engine:tecla_pressionada("a") then
        tentar_x = self.eixo_x - self.velocidade
        self.direcao = "esquerda"
    end

    -- Se não houver colisão no novo X, nós atualizamos a posição oficial do jogador
    if not mapa:tem_colisao(tentar_x, self.eixo_y, self.tamanho_w, self.tamanho_h) then
        self.eixo_x = tentar_x
    end

    -- LÓGICA VERTICAL (Y)
    if self.engine:tecla_pressionada("w") then
        tentar_y = self.eixo_y - self.velocidade
        self.direcao = "cima"
    elseif self.engine:tecla_pressionada("s") then
        tentar_y = self.eixo_y + self.velocidade
        self.direcao = "baixo"
    end

    -- Se não houver colisão no novo Y, nós atualizamos a posição oficial do jogador
    if not mapa:tem_colisao(self.eixo_x, tentar_y, self.tamanho_w, self.tamanho_h) then
        self.eixo_y = tentar_y
    end
    self.engine:posicionar_objeto(self.id_obj, self.eixo_x, self.eixo_y)
end

return player