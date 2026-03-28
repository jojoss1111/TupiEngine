-- main.lua
package.path = package.path .. ";./?.lua;./src/?.lua;./obj/?.lua"

local E = require("engine")
local player = require("jogador")
local Mapa   = require("mapa")

local SCREEN_W = 256
local SCREEN_H = 244

local function main()
    local v = E.nova(SCREEN_W, SCREEN_H, "", 2)
    if not v then
        print("Erro ao inicializar a Engine!")
        return
    end

    v:fundo(0, 0, 0)

    local mapa           = Mapa:new(v, 16, "sprites/casa.png")
    local sprite_player  = v:carregar_sprite("sprites/player_sprites.png")
    local item           = v:carregar_sprite("sprites/itens.png")
    local personagem     = player:new(v, 16, 16, 32, 32, sprite_player, item)

    while v:rodando() do
        -- 1. Eventos e limpeza
        v:eventos()
        v:limpar()
        v:atualizar(0)       -- animações já são atualizadas internamente aqui

        -- 2. Lógica do jogador
        personagem:mover(mapa)

        -- 3. Calcula câmera (centralizada no jogador, limitada ao mapa)
        local alvo_x    = personagem.eixo_x - (SCREEN_W / 2)
        local alvo_y    = personagem.eixo_y - (SCREEN_H / 2)
        local limite_x  = mapa.largura_px - SCREEN_W
        local limite_y  = mapa.altura_px  - SCREEN_H
        local cam_x     = math.max(0, math.min(alvo_x, limite_x))
        local cam_y     = math.max(0, math.min(alvo_y, limite_y))

        -- 4. Desenha o mapa SEM câmera (ele já recebe o offset manualmente)
        v:cam_ativa(false)
        mapa:desenhar(cam_x, cam_y)

        -- 5. Desenha objetos (jogador, espada, etc.) COM câmera
        v:cam_ativa(true)
        v:cam_pos(cam_x, cam_y)
        v:desenhar()

        -- 6. Apresenta o frame
        v:apresentar()
        v:fps(60)
    end

    v:destruir()
end

main()