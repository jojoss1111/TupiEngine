-- engine.lua 

local ffi = require("engineffi")
local lib  = ffi.load("./libengine.so", true)   -- carrega a biblioteca C
local C    = ffi.C                               -- acessa símbolos globais (fov, etc.)
local math = math

local E = {}
E.__index = E

-- Constantes dos botões do mouse (use E.ESQ, E.MEIO, E.DIR)
E.ESQ  = 0
E.MEIO = 1
E.DIR  = 2


-- =============================================================================
-- INICIALIZAÇÃO E LOOP PRINCIPAL
-- =============================================================================

--[[
    E.nova(largura, altura, titulo, escala)
    Cria a janela e retorna o objeto da engine.
    Retorna nil se algo der errado.

    Exemplo:
        local jogo = E.nova(320, 240, "Meu Jogo", 2)
]]
function E.nova(largura, altura, titulo, escala)
    local ptr = ffi.new("Engine[1]")
    if lib.engine_init(ptr[0], largura, altura, titulo or "", escala or 1) == 0 then
        return nil
    end
    local self = setmetatable({}, E)
    self._e         = ptr[0]
    self._ptr       = ptr
    self._anims     = {}
    self._anim_id   = 0
    self._anim_atual = {}   -- [oid] = anim_ativa_atual
    return self
end

-- Retorna true enquanto a janela estiver aberta.
function E:rodando()
    return self._e.running ~= 0
end

--[[
    :atualizar()
    Chame UMA VEZ por frame. Atualiza câmera, partículas, animações e áudio.

    Exemplo de loop básico:
        while jogo:rodando() do
            jogo:eventos()
            jogo:atualizar()
            jogo:limpar()
            -- seu código de desenho aqui
            jogo:desenhar()
            jogo:apresentar()
            jogo:fps(60)
        end
]]
function E:atualizar()
    local dt = lib.engine_get_delta(self._e)
    lib.engine_update(self._e, dt)
    lib.engine_audio_update(self._e)
    self:_atualizar_anims(dt)
end

-- Processa teclado, mouse e eventos da janela. Chame no início do loop.
function E:eventos()
    lib.engine_poll_events(self._e)
end

-- Limpa a tela com a cor de fundo atual.
function E:limpar()
    lib.engine_clear(self._e)
end

-- Desenha todos os objetos ativos na cena.
function E:desenhar()
    lib.engine_draw(self._e)
    lib.engine_particles_draw(self._e)
    lib.engine_fade_draw(self._e)
end

-- Exibe o frame na tela (troca de buffers). Chame no fim do loop.
function E:apresentar()
    lib.engine_flush(self._e)
    lib.engine_present(self._e)
end

-- Limita os quadros por segundo. Chame no final do loop.
function E:fps(alvo)
    lib.engine_cap_fps(self._e, alvo or 60)
end

-- Libera recursos e fecha a janela.
function E:destruir()
    lib.engine_audio_destroy(self._e)
    lib.engine_destroy(self._e)
end

-- Retorna quantos segundos se passaram desde o início.
function E:tempo()
    return lib.engine_get_time(self._e)
end

-- Retorna a duração do último frame em segundos (delta time).
function E:delta()
    return lib.engine_get_delta(self._e)
end


-- =============================================================================
-- JANELA
-- =============================================================================

-- Define a cor de fundo. Valores de 0 a 255.
function E:fundo(r, g, b)
    lib.engine_set_background(self._e, r, g, b)
end

-- Alterna entre janela e tela cheia.
function E:tela_cheia()
    lib.engine_toggle_fullscreen(self._e)
end

-- Retorna a largura e altura da área de desenho (em pixels lógicos).
function E:tamanho()
    return self._e.render_w, self._e.render_h
end


-- =============================================================================
-- SPRITES (imagens)
-- =============================================================================

--[[
    :carregar_sprite(caminho)
    Carrega uma imagem PNG. Retorna um sprite_id usado nas outras funções.
    Retorna -1 se o arquivo não for encontrado.

    Exemplo:
        local sid_heroi = jogo:carregar_sprite("imagens/heroi.png")
]]
function E:carregar_sprite(caminho)
    return lib.engine_load_sprite(self._e, caminho)
end

--[[
    :carregar_regiao(caminho, x, y, largura, altura)
    Carrega apenas uma parte de uma imagem (útil para spritesheets).
    Retorna um sprite_id.

    Exemplo — pega o primeiro frame 32x32 de uma spritesheet:
        local sid_frame1 = jogo:carregar_regiao("sheet.png", 0, 0, 32, 32)
]]
function E:carregar_regiao(caminho, x, y, w, h)
    return lib.engine_load_sprite_region(self._e, caminho, x, y, w, h)
end


-- =============================================================================
-- OBJETOS (entidades da cena)
-- =============================================================================

--[[
    :criar_objeto(x, y, sprite_id, largura, altura)
    Cria um objeto na posição (x, y) com o sprite indicado.
    Retorna um object_id que você usa para mover, rotacionar, remover etc.

    Exemplo:
        local heroi = jogo:criar_objeto(100, 200, sid_heroi, 32, 32)
]]
function E:criar_objeto(x, y, sid, w, h)
    return lib.engine_add_object(self._e, x, y, sid,
        w or 0, h or 0, 255, 255, 255)
end

--[[
    :criar_objeto_tile(x, y, sprite_id, coluna_tile, linha_tile, tw, th)
    Cria um objeto usando um tile específico de uma spritesheet.

    Exemplo — tile na coluna 2, linha 0, cada tile com 16x16 pixels:
        local arvore = jogo:criar_objeto_tile(64, 128, sid_sheet, 2, 0, 16, 16)
]]
function E:criar_objeto_tile(x, y, sid, tx, ty, tw, th)
    return lib.engine_add_tile_object(self._e, x, y, sid, tx, ty, tw, th)
end

-- Remove um objeto da cena.
function E:remover_objeto(oid)
    lib.engine_remove_object(self._e, oid)
end

--[[
    :mover(object_id, dx, dy)
    Move o objeto por deslocamento relativo.

    Exemplo — andar 2 pixels para a direita:
        jogo:mover(heroi, 2, 0)
]]
function E:mover(oid, dx, dy)
    lib.engine_move_object(self._e, oid, dx, dy)
end

--[[
    :posicionar(object_id, x, y)
    Coloca o objeto em uma posição exata.

    Exemplo:
        jogo:posicionar(heroi, 50, 100)
]]
function E:posicionar(oid, x, y)
    lib.engine_set_object_pos(self._e, oid, x, y)
end

--[[
    :posicao(object_id)
    Retorna a posição atual do objeto (x, y).

    Exemplo:
        local px, py = jogo:posicao(heroi)
]]
function E:posicao(oid)
    local ox, oy = ffi.new("int[1]"), ffi.new("int[1]")
    lib.engine_get_object_pos(self._e, oid, ox, oy)
    return ox[0], oy[0]
end

-- Troca o sprite do objeto.
function E:objeto_sprite(oid, sid)
    lib.engine_set_object_sprite(self._e, oid, sid)
end

-- Define qual tile da spritesheet o objeto exibe (coluna, linha).
function E:objeto_tile(oid, coluna, linha)
    lib.engine_set_object_tile(self._e, oid, coluna, linha)
end

--[[
    :espelhar(object_id, horizontal, vertical)
    Espelha o sprite. Passe true/false para cada eixo.

    Exemplo — espelhar na horizontal (virar para a esquerda):
        jogo:espelhar(heroi, true, false)
]]
function E:espelhar(oid, h, v)
    lib.engine_set_object_flip(self._e, oid, h and 1 or 0, v and 1 or 0)
end

--[[
    :escala(object_id, sx, sy)
    Altera o tamanho do objeto. 1.0 = tamanho normal, 2.0 = dobro.

    Exemplo — dobrar o tamanho:
        jogo:escala(heroi, 2.0, 2.0)
]]
function E:escala(oid, sx, sy)
    lib.engine_set_object_scale(self._e, oid, sx or 1, sy or 1)
end

--[[
    :rotacao(object_id, graus)
    Rotaciona o objeto (sentido horário).

    Exemplo — 45 graus:
        jogo:rotacao(heroi, 45)
]]
function E:rotacao(oid, graus)
    lib.engine_set_object_rotation(self._e, oid, graus or 0)
end

--[[
    :transparencia(object_id, valor)
    Define a transparência. 0 = invisível, 1 = completamente opaco.

    Exemplo — 50% transparente:
        jogo:transparencia(heroi, 0.5)
]]
function E:transparencia(oid, a)
    lib.engine_set_object_alpha(self._e, oid, a or 1)
end

--[[
    :camada(object_id, camada, z)
    Define em qual camada o objeto é desenhado (maior = na frente).

    Exemplo — colocar na camada 2:
        jogo:camada(heroi, 2, 0)
]]
function E:camada(oid, layer, z)
    lib.engine_set_object_layer(self._e, oid, layer or 0, z or 0)
end

--[[
    :hitbox(object_id, offset_x, offset_y, largura, altura)
    Define a área de colisão do objeto (pode ser menor que o sprite).

    Exemplo — hitbox centralizada de 20x28 em um sprite de 32x32:
        jogo:hitbox(heroi, 6, 4, 20, 28)
]]
function E:hitbox(oid, ox, oy, w, h)
    lib.engine_set_object_hitbox(self._e, oid, ox or 0, oy or 0, w, h)
end


-- =============================================================================
-- COLISÃO
-- =============================================================================

--[[
    :colidem(oid1, oid2)
    Verifica se dois objetos estão se tocando. Retorna true ou false.

    Exemplo:
        if jogo:colidem(heroi, inimigo) then
            -- lógica de dano
        end
]]
function E:colidem(oid1, oid2)
    return lib.engine_check_collision(self._e, oid1, oid2) ~= 0
end

--[[
    :colide_ponto(object_id, px, py)
    Verifica se um ponto está dentro da hitbox do objeto.
    Útil para clicar em objetos com o mouse.

    Exemplo:
        local mx, my = jogo:mouse_pos()
        if jogo:colide_ponto(botao, mx, my) then ... end
]]
function E:colide_ponto(oid, px, py)
    return lib.engine_check_collision_point(self._e, oid, px, py) ~= 0
end

--[[
    :colide_ret(object_id, rx, ry, largura, altura)
    Verifica se um objeto colide com um retângulo qualquer.

    Exemplo:
        if jogo:colide_ret(heroi, 0, 200, 320, 10) then
            -- heroi tocou o chão
        end
]]
function E:colide_ret(oid, rx, ry, rw, rh)
    return lib.engine_check_collision_rect(self._e, oid, rx, ry, rw, rh) ~= 0
end


-- =============================================================================
-- CÂMERA
-- =============================================================================

--[[
    :cam_pos(x, y)
    Move a câmera para uma posição exata no mundo.
]]
function E:cam_pos(x, y)
    lib.engine_camera_set(self._e, x, y)
end

--[[
    :cam_seguir(object_id, suavidade)
    Faz a câmera seguir um objeto suavemente.
    suavidade vai de 0 (parado) a 1 (instantâneo). Padrão: 0.1.

    Exemplo — câmera segue o herói suavemente:
        jogo:cam_seguir(heroi, 0.1)
]]
function E:cam_seguir(oid, lerp)
    lib.engine_camera_follow(self._e, oid, lerp or 0.1)
end

-- Zoom da câmera. 1.0 = normal, 2.0 = aproximado, 0.5 = afastado.
function E:cam_zoom(z)
    lib.engine_camera_zoom(self._e, z or 1)
end

--[[
    :cam_tremor(intensidade, duracao)
    Sacode a câmera por `duracao` segundos.

    Exemplo — tremor de impacto:
        jogo:cam_tremor(6, 0.3)
]]
function E:cam_tremor(intensidade, duracao)
    lib.engine_camera_shake(self._e, intensidade or 4, duracao or 0.3)
end

-- Converte uma posição do mundo para a posição na tela.
function E:mundo_para_tela(wx, wy)
    local sx, sy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_world_to_screen(self._e, wx, wy, sx, sy)
    return sx[0], sy[0]
end

-- Converte uma posição da tela para a posição no mundo.
function E:tela_para_mundo(sx, sy)
    local wx, wy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_screen_to_world(self._e, sx, sy, wx, wy)
    return wx[0], wy[0]
end


-- =============================================================================
-- PARTÍCULAS
-- =============================================================================

--[[
    :criar_emissor(cfg)
    Cria um emissor de partículas. Retorna um emitter_id.

    Campos de cfg (todos opcionais):
        x, y          — posição inicial
        vel           — {vx_min, vx_max, vy_min, vy_max}
        grav          — {aceleração_x, aceleração_y}
        vida          — {tempo_min, tempo_max} em segundos
        tamanho       — {tamanho_inicial, tamanho_final}
        cor           — {r, g, b, a}  (valores de 0 a 1)
        cor_final     — {r, g, b, a}
        rate          — partículas por segundo (0 = apenas burst manual)
        max           — máximo de partículas simultâneas

    Exemplo — explosão de fagulhas:
        local fogo = jogo:criar_emissor({
            x=100, y=200,
            vel={-30, 30, -60, -20},
            vida={0.3, 0.8},
            cor={1, 0.5, 0, 1},
            cor_final={1, 0, 0, 0},
            max=50
        })
        jogo:burst(fogo, 30)  -- dispara 30 partículas de uma vez
]]
function E:criar_emissor(cfg)
    local em   = ffi.new("ParticleEmitter")
    local vel  = cfg.vel       or {}
    local grav = cfg.grav      or {}
    local vida = cfg.vida      or {}
    local tam  = cfg.tamanho   or {}
    local c0   = cfg.cor       or {}
    local c1   = cfg.cor_final or {}
    em.x             = cfg.x        or 0
    em.y             = cfg.y        or 0
    em.vx_min        = vel[1]       or -20
    em.vx_max        = vel[2]       or  20
    em.vy_min        = vel[3]       or -50
    em.vy_max        = vel[4]       or -20
    em.ax            = grav[1]      or   0
    em.ay            = grav[2]      or  80
    em.life_min      = vida[1]      or 0.5
    em.life_max      = vida[2]      or 1.5
    em.size_start    = tam[1]       or   4
    em.size_end      = tam[2]       or   0
    em.r0=c0[1] or 1; em.g0=c0[2] or 1; em.b0=c0[3] or 0.2; em.a0=c0[4] or 1
    em.r1=c1[1] or 1; em.g1=c1[2] or 0; em.b1=c1[3] or 0;   em.a1=c1[4] or 0
    em.sprite_id     = cfg.sprite_id or -1
    em.rate          = cfg.rate      or  0
    em.max_particles = cfg.max       or 100
    return lib.engine_emitter_add(self._e, em)
end

-- Move o ponto de emissão do emissor.
function E:emissor_pos(eid, x, y)
    lib.engine_emitter_set_pos(self._e, eid, x, y)
end

-- Dispara N partículas de uma vez (efeito de explosão).
function E:burst(eid, n)
    lib.engine_emitter_burst(self._e, eid, n or 20)
end

-- Remove o emissor.
function E:remover_emissor(eid)
    lib.engine_emitter_remove(self._e, eid)
end


-- =============================================================================
-- ANIMAÇÃO (sprites animados)
-- =============================================================================

--[[
    :criar_animacao(sprite_id, tw, th, colunas, linhas, fps, loop, object_id)
    Cria uma animação de tiles a partir de uma spritesheet.

    sprite_id  — spritesheet carregada com carregar_sprite
    tw, th     — tamanho de cada tile em pixels
    colunas    — lista de colunas dos frames: {0, 1, 2, 3}
    linhas     — lista de linhas, ou {linha_única}: {0}
    fps        — velocidade da animação (frames por segundo)
    loop       — true = repete, false = toca uma vez
    object_id  — objeto que será animado automaticamente

    Exemplo — personagem com 4 frames na linha 0, a 8 fps, em loop:
        local anim_correr = jogo:criar_animacao(
            sid_sheet, 32, 32,
            {0, 1, 2, 3}, {0},
            8, true, heroi
        )
]]
function E:criar_animacao(sid, tw, th, colunas, linhas, fps, loop, oid)
    local id = self._anim_id + 1
    self._anim_id = id
    local frames = {}
    local nl = #linhas
    for i, col in ipairs(colunas) do
        local lin = nl == 1 and linhas[1] or (linhas[i] or linhas[nl])
        frames[i] = {col, lin}
    end
    local anim = {
        _id=id, sid=sid, tw=tw, th=th, fps=fps or 8,
        loop=(loop ~= false), oid=oid,
        frames=frames, idx=1, timer=0,
        ativo=false, fim=false   -- nasce PARADA; use anim_tocar() para iniciar
    }
    self._anims[id] = anim
    -- NÃO aplica o tile aqui para não sobrescrever outra animação ativa
    return anim
end

--[[
    :anim_tocar(anim)
    Toca a animação. Se ela já for a animação ativa do objeto, não reinicia.
    Se outra animação estiver ativa no mesmo objeto, ela é pausada primeiro.
]]
function E:anim_tocar(anim)
    local oid = anim.oid
    if oid then
        local atual = self._anim_atual[oid]
        if atual == anim then
            -- já é a ativa: apenas garante que está rodando (ex.: estava no fim)
            if anim.ativo and not anim.fim then return end
        elseif atual then
            -- pausa a anterior sem alterar o tile (quem manda no tile agora é anim)
            atual.ativo = false
        end
        self._anim_atual[oid] = anim
    end
    -- (re)inicia do frame 1 se estava inativa ou chegou ao fim
    if not anim.ativo or anim.fim then
        anim.idx = 1; anim.timer = 0; anim.fim = false
    end
    anim.ativo = true
    if oid and #anim.frames > 0 then
        local f = anim.frames[anim.idx]
        lib.engine_set_object_tile(self._e, oid, f[1], f[2])
    end
end

--[[
    :anim_parar(anim [, coluna, linha])
    Para a animação. Se for a ativa do objeto, libera o slot.
    Opcional: exibe um frame estático (coluna, linha) ao parar.
]]
function E:anim_parar(anim, coluna, linha)
    anim.ativo = false; anim.idx = 1; anim.timer = 0; anim.fim = false
    if anim.oid and self._anim_atual[anim.oid] == anim then
        self._anim_atual[anim.oid] = nil
    end
    if coluna ~= nil and anim.oid then
        lib.engine_set_object_tile(self._e, anim.oid, coluna, linha or 0)
    end
end

--[[
    :anim_atual(object_id)
    Retorna a animação que está tocando atualmente no objeto, ou nil.
]]
function E:anim_atual(oid)
    return self._anim_atual[oid]
end

-- Retorna true quando uma animação sem loop chegou ao último frame.
function E:anim_fim(anim)
    return anim.fim
end

-- Remove a animação da memória e libera o slot ativo se necessário.
function E:anim_destruir(anim)
    if anim.oid and self._anim_atual[anim.oid] == anim then
        self._anim_atual[anim.oid] = nil
    end
    self._anims[anim._id] = nil
end

-- (Interna) Avança todas as animações. Chamada automaticamente por :atualizar().
function E:_atualizar_anims(dt)
    if dt > 0.1 then dt = 0.1 end
    for _, a in pairs(self._anims) do
        -- só avança se for a animação ativa do objeto (ou sem objeto associado)
        if a.ativo and not a.fim and #a.frames > 0 then
            if not a.oid or self._anim_atual[a.oid] == a then
                local dur = 1.0 / a.fps
                a.timer = a.timer + dt
                while a.timer >= dur do
                    a.timer = a.timer - dur
                    if a.loop then
                        a.idx = (a.idx % #a.frames) + 1
                    elseif a.idx < #a.frames then
                        a.idx = a.idx + 1
                    else
                        a.fim = true; a.ativo = false; break
                    end
                end
                if not a.fim and a.oid then
                    local f = a.frames[a.idx]
                    lib.engine_set_object_tile(self._e, a.oid, f[1], f[2])
                end
            end
        end
    end
end


-- =============================================================================
-- FADE (transição de tela)
-- =============================================================================

--[[
    :fade(alvo, velocidade, r, g, b)
    Faz fade de entrada ou saída.
    alvo: 0 = fade in (aparece), 1 = fade out (escurece)
    velocidade: unidades por segundo (padrão 1)

    Exemplo — fade para preto ao trocar de fase:
        jogo:fade(1, 2)   -- escurece em 0.5 segundo
        -- espere jogo:fade_ok() == true
        jogo:fade(0, 2)   -- clareia novamente
]]
function E:fade(alvo, vel, r, g, b)
    lib.engine_fade_to(self._e, alvo or 1, vel or 1, r or 0, g or 0, b or 0)
end

-- Retorna true quando o fade terminou.
function E:fade_ok()
    return lib.engine_fade_done(self._e) ~= 0
end


-- =============================================================================
-- DESENHO (primitivas e sprites diretos)
-- =============================================================================

-- Retângulo preenchido. Cores de 0 a 255.
function E:ret(x, y, w, h, r, g, b)
    lib.engine_draw_rect(self._e, x, y, w, h, r, g, b)
end

-- Contorno de retângulo com espessura (padrão 1).
function E:ret_contorno(x, y, w, h, r, g, b, espessura)
    lib.engine_draw_rect_outline(self._e, x, y, w, h, r, g, b, espessura or 1)
end

-- Linha entre dois pontos.
function E:linha(x0, y0, x1, y1, r, g, b, espessura)
    lib.engine_draw_line(self._e, x0, y0, x1, y1, r, g, b, espessura or 1)
end

-- Círculo. preenchido=true por padrão.
function E:circulo(cx, cy, raio, r, g, b, preenchido)
    lib.engine_draw_circle(self._e, cx, cy, raio, r, g, b,
        preenchido ~= false and 1 or 0)
end

-- Retângulo semitransparente (sobreposição). alpha de 0 a 1.
function E:overlay(x, y, w, h, r, g, b, alpha)
    lib.engine_draw_overlay(self._e, x, y, w, h, r, g, b, alpha or 0.5)
end

--[[
    :sprite(sprite_id, x, y, sx, sy, sw, sh, opcoes)
    Desenha parte de um sprite na posição (x, y).
    sx, sy, sw, sh = região da imagem fonte (x, y, largura, altura).

    opcoes é uma tabela opcional:
        escala_x  — escala horizontal (padrão 1)
        escala_y  — escala vertical (padrão 1)
        rotacao   — graus (padrão 0)
        alpha     — transparência 0..1 (padrão 1)
        fh        — espelhar horizontal (padrão false)
        fv        — espelhar vertical (padrão false)
        invertido — true para efeito de dano (cores invertidas)

    Exemplo simples:
        jogo:sprite(sid_fundo, 0, 0, 0, 0, 320, 240)

    Exemplo com opções:
        jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, {rotacao=45, alpha=0.8})
]]
function E:sprite(sid, x, y, sx, sy, sw, sh, op)
    if op then
        if op.invertido then
            lib.engine_draw_sprite_part_inverted(self._e, sid, x, y, sx, sy, sw, sh)
        else
            lib.engine_draw_sprite_part_ex(self._e, sid, x, y, sx, sy, sw, sh,
                op.escala_x or 1, op.escala_y or 1,
                op.rotacao or 0, op.alpha or 1,
                op.fh and 1 or 0, op.fv and 1 or 0)
        end
    else
        lib.engine_draw_sprite_part(self._e, sid, x, y, sx, sy, sw, sh)
    end
end

--[[
    :tilemap(mapa, linhas, colunas, sprite_id, tw, th, ox, oy)
    Desenha um mapa de tiles a partir de uma tabela linear (linha por linha).

    Exemplo com mapa 5x5, tiles de 16x16:
        local mapa = {
            1,1,1,1,1,
            1,0,0,0,1,
            1,0,0,0,1,
            1,0,0,0,1,
            1,1,1,1,1,
        }
        jogo:tilemap(mapa, 5, 5, sid_tiles, 16, 16, 0, 0)
]]
function E:tilemap(mapa, linhas, colunas, sid, tw, th, ox, oy)
    local n = linhas * colunas
    local c = ffi.new("int[?]", n)
    for i = 1, n do c[i-1] = mapa[i] or 0 end
    lib.engine_draw_tilemap(self._e, c, linhas, colunas, sid, tw, th, ox or 0, oy or 0)
end


-- =============================================================================
-- TEXTO E UI
-- =============================================================================

--[[
    :texto(x, y, texto, sprite_id, fw, fh, chars_por_linha, offset_ascii, espacamento)
    Escreve texto usando uma fonte bitmap (imagem com os caracteres).

    fw, fh          — tamanho de cada caractere em pixels
    chars_por_linha — quantos caracteres por linha na imagem (padrão 16)
    offset_ascii    — código ASCII do primeiro caractere (padrão 32 = espaço)
    espacamento     — pixels extras entre caracteres (padrão 0)

    Exemplo:
        jogo:texto(10, 10, "Olá mundo!", sid_fonte, 8, 8)
]]
function E:texto(x, y, txt, fsid, fw, fh, chars_por_linha, offset_ascii, espacamento)
    lib.engine_draw_text(self._e, x, y, txt, fsid, fw, fh,
        chars_por_linha or 16, offset_ascii or 32, espacamento or 0)
end

-- Caixa decorada com tileset de bordas 3×3.
function E:caixa(x, y, w, h, sid, tw, th)
    lib.engine_draw_box(self._e, x, y, w, h, sid, tw, th)
end

-- Caixa de texto com título e conteúdo.
function E:caixa_texto(x, y, w, h, titulo, conteudo,
                        bsid, btw, bth, fsid, fw, fh,
                        chars_por_linha, offset_ascii, espacamento)
    lib.engine_draw_text_box(self._e, x, y, w, h, titulo, conteudo,
        bsid, btw, bth, fsid, fw, fh,
        chars_por_linha or 16, offset_ascii or 32, espacamento or 0)
end


-- =============================================================================
-- EFEITOS VISUAIS
-- =============================================================================

-- Efeito de chuva. gotas = tabela de {x, y}.
function E:chuva(larg, alt, frame, gotas, gw, gh)
    local n = #gotas
    local gx, gy = ffi.new("int[?]", n), ffi.new("int[?]", n)
    for i, g in ipairs(gotas) do gx[i-1]=g[1]; gy[i-1]=g[2] end
    lib.engine_draw_rain(self._e, larg, alt, frame, gx, gy, n, gw or 1, gh or 4)
end

-- Overlay escuro de noite. intensidade de 0 (dia) a 1 (noite total).
function E:noite(larg, alt, intensidade)
    lib.engine_draw_night(self._e, larg, alt, intensidade or 0.5, 0)
end


-- =============================================================================
-- INPUT — TECLADO
-- =============================================================================

-- true enquanto a tecla estiver pressionada.
function E:tecla(t)
    return lib.engine_key_down(self._e, t:lower()) ~= 0
end

-- true apenas no frame em que a tecla foi pressionada (sem repetição).
function E:tecla_press(t)
    return lib.engine_key_pressed(self._e, t:lower()) ~= 0
end

-- true apenas no frame em que a tecla foi solta.
function E:tecla_solta(t)
    return lib.engine_key_released(self._e, t:lower()) ~= 0
end


-- =============================================================================
-- INPUT — MOUSE
-- =============================================================================

-- true enquanto o botão estiver pressionado. Use E.ESQ, E.MEIO ou E.DIR.
function E:mouse_segurado(b)
    return lib.engine_mouse_down(self._e, b) ~= 0
end

-- true apenas no frame em que o botão foi pressionado.
function E:mouse_press(b)
    return lib.engine_mouse_pressed(self._e, b) ~= 0
end

-- true apenas no frame em que o botão foi solto.
function E:mouse_solta(b)
    return lib.engine_mouse_released(self._e, b) ~= 0
end

-- Retorna a posição do cursor em coordenadas de tela (x, y).
function E:mouse_pos()
    local mx, my = ffi.new("int[1]"), ffi.new("int[1]")
    lib.engine_mouse_pos(self._e, mx, my)
    return mx[0], my[0]
end

-- Retorna +1 (scroll para cima), -1 (para baixo) ou 0.
function E:mouse_scroll()
    return lib.engine_mouse_scroll(self._e)
end


-- =============================================================================
-- ÁUDIO
-- =============================================================================

--[[
    :audio_init()
    Inicializa o subsistema de áudio. Chame logo após E.nova().
    Retorna true se funcionou.

    Exemplo:
        local jogo = E.nova(320, 240, "Jogo")
        jogo:audio_init()
]]
function E:audio_init()
    return lib.engine_audio_init(self._e) ~= 0
end

--[[
    :tocar(arquivo, loop, volume, pitch)
    Toca um arquivo de áudio. Retorna um handle para controlar depois.
    loop=false, volume=1.0, pitch=1.0

    Exemplo:
        local musica = jogo:tocar("musica.ogg", true)   -- loop
        local som    = jogo:tocar("pulo.wav")            -- toca uma vez
]]
function E:tocar(arquivo, loop, vol, pitch)
    return lib.engine_audio_play(self._e, arquivo,
        loop and 1 or 0, vol or 1, pitch or 1, -1)
end

-- Pausa um som.
function E:pausar(h)
    if h and h ~= -1 then lib.engine_audio_pause(self._e, h) end
end

-- Retoma um som pausado.
function E:retomar(h)
    if h and h ~= -1 then lib.engine_audio_resume(self._e, h) end
end

-- Para e libera um som completamente.
function E:parar(h)
    if h and h ~= -1 then lib.engine_audio_stop(self._e, h) end
end

-- Ajusta o volume de um som em tempo real (0 a 1).
function E:volume(h, v)
    if h and h ~= -1 then lib.engine_audio_volume(self._e, h, v or 1) end
end

-- Ajusta o pitch de um som em tempo real (0.5 a 2.0).
function E:pitch(h, p)
    if h and h ~= -1 then lib.engine_audio_pitch(self._e, h, p or 1) end
end

-- Retorna true quando um som sem loop terminou.
function E:audio_fim(h)
    if not h or h == -1 then return true end
    return lib.engine_audio_done(self._e, h) ~= 0
end


-- =============================================================================
-- SHADERS GLSL (efeitos visuais avançados)
-- =============================================================================
-- Shaders são programas que rodam na GPU e mudam a aparência dos pixels.
-- Fluxo: sh = jogo:shader_criar(vert, frag) → jogo:shader_usar(sh) → desenhar → jogo:shader_nenhum()

-- Vertex shader padrão (pass-through com câmera ortográfica).
E.VERT_PADRAO = [[
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_uv        = gl_MultiTexCoord0.xy;
    v_color     = gl_Color;
}
]]

-- Fragment: escala de cinza.
E.FRAG_CINZA = [[
uniform sampler2D u_tex;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4  c   = texture2D(u_tex, v_uv) * v_color;
    float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4(lum, lum, lum, c.a);
}
]]

-- Fragment: inverte as cores (efeito negativo).
E.FRAG_NEGATIVO = [[
uniform sampler2D u_tex;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4 c = texture2D(u_tex, v_uv) * v_color;
    gl_FragColor = vec4(1.0 - c.rgb, c.a);
}
]]

-- Fragment: tinta colorida via uniform vec4 "u_tint".
E.FRAG_TINT = [[
uniform sampler2D u_tex;
uniform vec4 u_tint;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4 c = texture2D(u_tex, v_uv) * v_color;
    gl_FragColor = vec4(c.rgb * u_tint.rgb, c.a * u_tint.a);
}
]]

-- Fragment: aberração cromática. Uniform vec2 "u_offset" (ex: 0.003, 0.0).
E.FRAG_ABERRACAO = [[
uniform sampler2D u_tex;
uniform vec2      u_offset;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    float r = texture2D(u_tex, v_uv + u_offset).r;
    float g = texture2D(u_tex, v_uv           ).g;
    float b = texture2D(u_tex, v_uv - u_offset).b;
    float a = texture2D(u_tex, v_uv           ).a;
    gl_FragColor = vec4(r, g, b, a) * v_color;
}
]]

-- Fragment: efeito CRT (scanlines + vinheta). Uniform vec2 "u_resolucao".
E.FRAG_CRT = [[
uniform sampler2D u_tex;
uniform vec2      u_resolucao;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4  c       = texture2D(u_tex, v_uv) * v_color;
    float scan    = sin(v_uv.y * u_resolucao.y * 3.14159) * 0.04;
    vec2  d       = v_uv - 0.5;
    float vinheta = 1.0 - dot(d, d) * 1.4;
    gl_FragColor  = vec4(c.rgb * (1.0 - scan) * vinheta, c.a);
}
]]

-- Compila e linka um shader GLSL. Retorna um shader handle ou -1 em erro.
function E:shader_criar(vert_src, frag_src)
    return lib.engine_shader_create(self._e, vert_src, frag_src)
end

-- Libera um shader da memória.
function E:shader_destruir(sh)
    if sh and sh ~= -1 then lib.engine_shader_destroy(self._e, sh) end
end

-- Ativa um shader para os próximos draws.
function E:shader_usar(sh)
    if sh and sh ~= -1 then lib.engine_shader_use(self._e, sh) end
end

-- Desativa qualquer shader e volta ao modo padrão.
function E:shader_nenhum()
    lib.engine_shader_none(self._e)
end

--[[
    :shader_uniform(shader, nome, ...)
    Define um uniform no shader. Detecta automaticamente o tipo pelo número de valores.

    Exemplos:
        jogo:shader_uniform(sh, "u_tempo",     0.5)              -- float
        jogo:shader_uniform(sh, "u_resolucao", 320, 240)         -- vec2
        jogo:shader_uniform(sh, "u_tint",      1, 0.5, 0, 1)    -- vec4
        jogo:shader_uniform(sh, "u_modo",      1)                -- int (use inteiro)
]]
function E:shader_uniform(sh, nome, a, b, c, d)
    if not sh or sh == -1 then return end
    if d ~= nil then
        lib.engine_shader_set_vec4(self._e, sh, nome, a, b, c, d)
    elseif b ~= nil then
        lib.engine_shader_set_vec2(self._e, sh, nome, a, b)
    elseif math.type and math.type(a) == "integer" then
        lib.engine_shader_set_int(self._e, sh, nome, a)
    else
        lib.engine_shader_set_float(self._e, sh, nome, a)
    end
end

--[[
    :efeito(nome, ativar, ...)
    Ativa ou desativa um efeito visual pré-pronto. Os shaders são compilados
    na primeira chamada e reutilizados depois.

    Nomes disponíveis: "cinza", "negativo", "crt", "aberracao"
    Para "crt", passe largura e altura opcionalmente.
    Para "aberracao", passe o offset (padrão 0.003).

    Exemplos:
        jogo:efeito("cinza", true)                  -- ativa preto e branco
        jogo:efeito("cinza", false)                 -- desativa
        jogo:efeito("crt", true, 320, 240)          -- ativa CRT
        jogo:efeito("aberracao", true, 0.005)       -- aberração cromática forte
]]
function E:efeito(nome, ativar, ...)
    if not ativar then
        self:shader_nenhum()
        return
    end
    local args = {...}
    if nome == "cinza" then
        if not self._sh_cinza then
            self._sh_cinza = self:shader_criar(E.VERT_PADRAO, E.FRAG_CINZA)
        end
        self:shader_usar(self._sh_cinza)

    elseif nome == "negativo" then
        if not self._sh_neg then
            self._sh_neg = self:shader_criar(E.VERT_PADRAO, E.FRAG_NEGATIVO)
        end
        self:shader_usar(self._sh_neg)

    elseif nome == "crt" then
        if not self._sh_crt then
            self._sh_crt = self:shader_criar(E.VERT_PADRAO, E.FRAG_CRT)
        end
        self:shader_usar(self._sh_crt)
        self:shader_uniform(self._sh_crt, "u_resolucao",
            args[1] or self._e.render_w, args[2] or self._e.render_h)

    elseif nome == "aberracao" then
        if not self._sh_aber then
            self._sh_aber = self:shader_criar(E.VERT_PADRAO, E.FRAG_ABERRACAO)
        end
        self:shader_usar(self._sh_aber)
        self:shader_uniform(self._sh_aber, "u_offset", args[1] or 0.003, 0.0)
    end
end


-- =============================================================================
-- FBO (Framebuffer — renderização offscreen)
-- =============================================================================
-- Útil para efeitos que precisam desenhar em uma textura intermediária.
-- Fluxo: fbo=fbo_criar(w,h) → fbo_bind(fbo) → limpar()+desenhar()
--        → fbo_unbind() → sid=fbo_como_sprite(fbo) → sprite(sid, ...)

-- Cria um framebuffer com textura RGBA de tamanho w×h. Retorna handle ou -1.
function E:fbo_criar(w, h)
    return lib.engine_fbo_create(self._e, w, h)
end

-- Libera o framebuffer.
function E:fbo_destruir(fh)
    if fh and fh ~= -1 then lib.engine_fbo_destroy(self._e, fh) end
end

-- Redireciona o rendering para este framebuffer.
function E:fbo_bind(fh)
    if fh and fh ~= -1 then lib.engine_fbo_bind(self._e, fh) end
end

-- Volta a desenhar na tela normal.
function E:fbo_unbind()
    lib.engine_fbo_unbind(self._e)
end

-- Registra o conteúdo do framebuffer como um sprite. Retorna sprite_id.
function E:fbo_como_sprite(fh)
    if not fh or fh == -1 then return -1 end
    local tex = lib.engine_fbo_texture(self._e, fh)
    if tex == 0 then return -1 end
    local sid = self._e.sprite_count
    if sid >= 64 then return -1 end
    self._e.sprites[sid].texture = tex
    self._e.sprites[sid].width   = self._e.fbos[fh].width
    self._e.sprites[sid].height  = self._e.fbos[fh].height
    self._e.sprites[sid].loaded  = 1
    self._e.sprite_count = sid + 1
    return sid
end


-- =============================================================================
-- FOV — Campo de Visão com Sombras (para jogos com neblina de guerra)
-- =============================================================================
-- Fluxo:
--   local fov = jogo:fov_novo(colunas_mapa, linhas_mapa, raio, modo)
--   jogo:fov_calcular(fov, coluna_jogador, linha_jogador, funcao_parede)
--   jogo:fov_sombra(fov, tw, th, ox, oy)   -- desenhe APÓS o mundo, ANTES da UI
--
-- Modos:
--   0 = BASICO    — sem memória, tudo escuro fora da visão
--   1 = NEBLINA   — tiles visitados ficam semi-visíveis (padrão)
--   2 = SUAVE     — claridade com suavização nas bordas

-- Cria uma sessão de FOV para um mapa de map_cols×map_rows tiles.
function E:fov_novo(map_cols, map_rows, raio, modo)
    local obj = {
        map_cols = map_cols,
        map_rows = map_rows,
        raio     = raio or 8,
        modo     = modo or 1,
    }
    local n       = map_cols * map_rows
    obj._vis_n    = n
    obj._vis      = ffi.new("float[?]", n)
    ffi.fill(obj._vis, n * ffi.sizeof("float"), 0)
    obj._params               = ffi.new("FovParams")
    obj._params.map_cols      = map_cols
    obj._params.map_rows      = map_rows
    obj._params.radius        = obj.raio
    obj._params.mode          = obj.modo
    obj._params.vis           = obj._vis
    obj._params.is_blocking   = nil
    obj._params.user_data     = nil
    return obj
end

--[[
    :fov_calcular(fov, coluna, linha, funcao_parede)
    Recalcula o campo de visão a partir da posição do jogador.
    funcao_parede(col, row) deve retornar true se o tile bloqueia a visão.

    Exemplo:
        jogo:fov_calcular(fov, px, py, function(col, row)
            return mapa[row * cols + col + 1] == PAREDE
        end)
]]
function E:fov_calcular(fov, col, row, is_parede)
    fov._params.origin_col = col
    fov._params.origin_row = row
    local cb = ffi.cast("FovBlockFn", function(c, r, _)
        return is_parede(c, r) and 1 or 0
    end)
    fov._params.is_blocking = cb
    fov._params.user_data   = nil
    C.engine_fov_compute(fov._params)
    cb:free()
end

-- Altera o raio de visão para o próximo fov_calcular().
function E:fov_raio(fov, r)
    fov.raio = r
    fov._params.radius = r
end

-- Zera o mapa de visão (use ao trocar de mapa).
function E:fov_reset(fov)
    ffi.fill(fov._vis, fov._vis_n * ffi.sizeof("float"), 0)
end

--[[
    :fov_sombra(fov, tw, th, ox, oy, r, g, b)
    Desenha a camada de escuridão sobre o mapa.
    Chame APÓS desenhar o mundo e ANTES da UI.
    ox, oy = mesmo deslocamento usado no tilemap.
    r, g, b = cor da sombra (padrão 0, 0, 0 = preto).
]]
function E:fov_sombra(fov, tile_w, tile_h, offset_x, offset_y, r, g, b)
    C.engine_fov_draw_shadow(self._e, fov._vis,
        fov.map_cols, fov.map_rows,
        tile_w, tile_h,
        offset_x or 0, offset_y or 0,
        r or 0, g or 0, b or 0)
end

-- Retorna true se o tile está visível no frame atual.
function E:fov_visivel(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] >= 0.99
end

-- Retorna true se o tile já foi explorado ao menos uma vez (modo NEBLINA).
function E:fov_explorado(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] > 0.0
end


-- =============================================================================
-- GRADE ESPACIAL (colisões eficientes com muitos objetos)
-- =============================================================================
-- Use quando tiver muitos objetos e precisar checar colisões rápidas.
-- Objetos criados e removidos são inseridos/retirados da grade automaticamente.
-- Fluxo: sgrid_init() → (loop) sgrid_primeira_colisao() ou sgrid_todas_colisoes()

-- Ativa a grade. cell_size = tamanho de cada célula em pixels (0 = automático).
function E:sgrid_init(cell_size)
    lib.engine_sgrid_init(self._e, cell_size or 0)
end

-- Desativa e limpa a grade.
function E:sgrid_destruir()
    lib.engine_sgrid_destroy(self._e)
end

-- Reinsere todos os objetos na grade (use após criar muitos de uma vez).
function E:sgrid_rebuild()
    lib.engine_sgrid_rebuild(self._e)
end

--[[
    :sgrid_primeira_colisao(object_id)
    Retorna o ID do primeiro objeto que colide com oid (AABB exato).
    Retorna nil se nenhum colidir.

    Exemplo:
        local alvo = jogo:sgrid_primeira_colisao(projetil)
        if alvo then jogo:remover_objeto(alvo) end
]]
function E:sgrid_primeira_colisao(oid)
    local r = lib.engine_sgrid_first_collision(self._e, oid)
    return r >= 0 and r or nil
end

--[[
    :sgrid_todas_colisoes(object_id, max)
    Retorna uma tabela com os IDs de todos os objetos que colidem com oid.

    Exemplo:
        for _, inimigo in ipairs(jogo:sgrid_todas_colisoes(explosao)) do
            causar_dano(inimigo)
        end
]]
function E:sgrid_todas_colisoes(oid, cap)
    cap = cap or 64
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_all_collisions(self._e, oid, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- Retorna true se a grade está ativa.
function E:sgrid_ativo()
    return self._e.sgrid.enabled ~= 0
end


-- =============================================================================
-- MAPA DE TILES
--
-- Fluxo simples para iniciantes:
--
--   local mapa = engine:carregar_mapa("fase1.lua")
--   -- no loop:
--   engine:desenhar_mapa(mapa)
--   if engine:colide_mapa(mapa, obj_player) then ... end
--
-- O arquivo .lua de mapa usa o módulo mapa.lua e deve terminar com return m:build()
-- =============================================================================

--[[
    :carregar_mapa(caminho)
    Carrega um arquivo .lua de mapa e retorna o mapa pronto para uso.
    O arquivo deve terminar com: return m:build()

    Exemplo:
        local mapa = engine:carregar_mapa("fase1.lua")
        local mapa = engine:carregar_mapa("mapas/fazenda.lua")
]]
function E:carregar_mapa(caminho)
    local fn, err = loadfile(caminho)
    if not fn then
        error("[carregar_mapa] Não consegui abrir '" .. caminho .. "': " .. tostring(err))
    end
    local dados = fn()
    if type(dados) ~= "table" then
        error("[carregar_mapa] '" .. caminho .. "' deve retornar m:build()")
    end

    -- Carrega o sprite atlas definido no mapa
    if dados.sprite_atlas then
        dados._atlas_sid = self:carregar_sprite(dados.sprite_atlas)
    else
        dados._atlas_sid = -1
    end

    -- Monta tabela de colisão [lin][col] = true — feito UMA vez ao carregar
    local COLISOR = 1   -- MAPA_FLAG_COLISOR = bit 0
    local col_map = {}
    for _, camada in ipairs(dados.camadas or {}) do
        if camada.visivel ~= false then
            for _, tile in ipairs(camada.tiles or {}) do
                if tile.flags and bit.band(tile.flags, COLISOR) ~= 0 then
                    if not col_map[tile.lin] then col_map[tile.lin] = {} end
                    col_map[tile.lin][tile.col] = true
                end
            end
        end
    end
    dados._colisores = col_map

    -- Pré-calcula lista de pixels para renderizar — feito UMA vez ao carregar
    local tw = dados.tile_w or 16
    local th = dados.tile_h or 16
    local render = {}
    for _, camada in ipairs(dados.camadas or {}) do
        if camada.visivel ~= false then
            for _, tile in ipairs(camada.tiles or {}) do
                render[#render + 1] = {
                    px = tile.col        * tw,
                    py = tile.lin        * th,
                    sx = tile.sprite_col * tw,
                    sy = tile.sprite_lin * th,
                }
            end
        end
    end
    dados._render = render
    dados._tile_w = tw
    dados._tile_h = th

    return dados
end

--[[
    :desenhar_mapa(mapa)
    Desenha todos os tiles do mapa. Chame no loop ANTES de engine:desenhar().

    Exemplo:
        engine:desenhar_mapa(mapa)
        engine:desenhar()
]]
function E:desenhar_mapa(mapa)
    local sid = mapa._atlas_sid
    local tw  = mapa._tile_w
    local th  = mapa._tile_h
    if not sid or sid < 0 then return end
    for _, t in ipairs(mapa._render) do
        lib.engine_draw_sprite_part(self._e, sid, t.px, t.py, t.sx, t.sy, tw, th)
    end
end

--[[
    :colide_mapa(mapa, obj_id)
    Retorna true se o objeto está colidindo com um tile sólido do mapa.
    Usa a hitbox do objeto automaticamente.

    Exemplo:
        if engine:colide_mapa(mapa, jogador) then
            engine:mover(jogador, -dx, -dy)
        end
]]
function E:colide_mapa(mapa, oid)
    local ox = ffi.new("int[1]")
    local oy = ffi.new("int[1]")
    lib.engine_get_object_pos(self._e, oid, ox, oy)

    -- Dimensões da hitbox do objeto
    local obj = self._e.objects[oid]
    local hox, hoy, hw, hh
    if obj.hitbox.enabled ~= 0 then
        hox = obj.hitbox.offset_x
        hoy = obj.hitbox.offset_y
        hw  = obj.hitbox.width
        hh  = obj.hitbox.height
    else
        hox = 0
        hoy = 0
        hw  = obj.tile_w > 0 and obj.tile_w or obj.width
        hh  = obj.tile_h > 0 and obj.tile_h or obj.height
    end

    local x0 = ox[0] + hox
    local y0 = oy[0] + hoy
    local x1 = x0 + hw - 1
    local y1 = y0 + hh - 1

    local tw  = mapa._tile_w
    local th  = mapa._tile_h
    local col = mapa._colisores

    local function solido(px, py)
        local c = math.floor(px / tw)
        local l = math.floor(py / th)
        return col[l] ~= nil and col[l][c] == true
    end

    return solido(x0, y0) or solido(x1, y0)
        or solido(x0, y1) or solido(x1, y1)
end


return E