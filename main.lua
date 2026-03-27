-- main.lua
package.path = package.path .. ";./?.lua;./src/?.lua;./obj/?.lua"

local Engine = require("engine")
local player = require("jogador")
local Mapa = require("mapa")

local SCREEN_W = 256
local SCREEN_H = 244

local function main()
    local v = Engine.nova(SCREEN_W, SCREEN_H, "", 2)
    if not v then
        print("Erro ao inicializar a Engine!")
        return
    end
    local x, y = 32, 32 
    v:cor_fundo(20, 30, 40)
    local mapa = Mapa:new(v, 16)
    local personagem = player:new(v, 16, 16, x, y)
    -- INÍCIO DO LOOP DO JOGO
    while v:rodando() do
        v:processar_eventos()
        v:limpar()
        personagem:mover(mapa)
        local largura_do_mapa_em_pixels = #mapa.dados[1] * mapa.tamanho_tile
        local altura_do_mapa_em_pixels = #mapa.dados * mapa.tamanho_tile
        local alvo_x = personagem.eixo_x - (SCREEN_W / 2)
        local alvo_y = personagem.eixo_y - (SCREEN_H / 2)
        local limite_x_max = largura_do_mapa_em_pixels - SCREEN_W
        local limite_y_max = altura_do_mapa_em_pixels - SCREEN_H
        local cam_x = math.max(0, math.min(alvo_x, limite_x_max))
        local cam_y = math.max(0, math.min(alvo_y, limite_y_max))
        v:habilitar_camera(true)
        v:posicionar_camera(cam_x, cam_y)
        mapa:desenhar()                
        v:desenhar()
        v:apresentar()
        v:atualizar(0)
        v:limitar_fps(60)
    end
    v:destruir()
end

main()