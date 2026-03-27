-- mapa.lua
local Mapa = {}
Mapa.__index = Mapa

function Mapa:new(engine, tamanho_tile)
    local instancia = {
        engine = engine,
        tamanho_tile = tamanho_tile, -- Tamanho de cada quadrado (ex: 16x16)
        
        -- Dicionário configurando o que cada número faz
        -- r, g, b = Cores | colisor = Verdadeiro/Falso para bater
        tipos = {
            [0] = { r = 0,   g = 0,   b = 0,   colisor = false, visivel = false }, -- Chão / Nada
            [1] = { r = 150, g = 150, b = 150, colisor = true,  visivel = true },  -- Parede Cinza
            [2] = { r = 0,   g = 0,   b = 255, colisor = true,  visivel = true },  -- Água / Bloco Azul
            [3] = { r = 0,   g = 255, b = 0,   colisor = false, visivel = true },  -- Grama (Pode pisar)
        },

        -- O nosso mapa (Matriz Numérica)
        -- Aqui você "desenha" o mapa usando os números acima
        dados = {
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1},
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        }
    }
    setmetatable(instancia, Mapa)
    return instancia
end

-- Função para desenhar o mapa na tela
function Mapa:desenhar()
    for linha = 1, #self.dados do
        for coluna = 1, #self.dados[linha] do
            local valor = self.dados[linha][coluna]
            local tipo = self.tipos[valor]
            
            if tipo and tipo.visivel then
                -- Calcula a posição na tela (em Lua o array começa no 1, por isso o -1)
                local x = (coluna - 1) * self.tamanho_tile
                local y = (linha - 1) * self.tamanho_tile
                
                self.engine:desenhar_retangulo(x, y, self.tamanho_tile, self.tamanho_tile, tipo.r, tipo.g, tipo.b)
            end
        end
    end
end

-- Função que verifica se uma área (como o jogador) está tocando num colisor
function Mapa:tem_colisao(x, y, w, h)
    -- Descobre quais colunas e linhas o retângulo está ocupando
    local coluna_esq = math.floor(x / self.tamanho_tile) + 1
    local coluna_dir = math.floor((x + w - 1) / self.tamanho_tile) + 1
    local linha_cima = math.floor(y / self.tamanho_tile) + 1
    local linha_baixo = math.floor((y + h - 1) / self.tamanho_tile) + 1

    -- Verifica só os blocos que estão nessas posições calculadas
    for linha = linha_cima, linha_baixo do
        for coluna = coluna_esq, coluna_dir do
            if self.dados[linha] and self.dados[linha][coluna] then
                local valor = self.dados[linha][coluna]
                local tipo = self.tipos[valor]
                if tipo and tipo.colisor then
                    return true -- Achou uma parede!
                end
            end
        end
    end
    return false -- Caminho livre!
end

return Mapa