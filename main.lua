-- main.lua
package.path = package.path .. ";./?.lua;./src/?.lua;./obj/?.lua"

local E = require("engine")
local SCREEN_W = 256
local SCREEN_H = 244

local function main()
    local v = E.nova(SCREEN_W, SCREEN_H, "", 2)
    if not v then
        print("Erro ao inicializar a Engine!")
        return
    end
    v:fundo(0, 0, 0)
    local x = SCREEN_W/2
    local y = SCREEN_H/2
    local velocidade = 4
    local tamanho_q = 16
    while v:rodando() do
        -- 1. Eventos e limpeza
        v:eventos()
        v:limpar()
        v:atualizar(0)
        if v:tecla("w") then
            x = x - velocidade
        elseif v:tecla("s") then
            x = x + velocidade
        elseif v:tecla("d") then
            y = y + velocidade
        elseif v:tecla("a") then
            y = y - velocidade
        end
        v:rect(x, y, tamanho_q, tamanho_q, 200, 10, 50)
        v:apresentar()
        v:fps(60)
    end

    v:destruir()
end

main()
