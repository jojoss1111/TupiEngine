-- engine.lua

local ffi = require("engineffi")
local lib  = ffi.load("./libengine.so", true)
local C    = ffi.C   -- acessa engine_fov_* e outros símbolos globais
local bit  = require("bit")
local math = math

local E = {}
E.__index = E

-- Botões do mouse
E.ESQ  = 0
E.MEIO = 1
E.DIR  = 2


-- =============================================================================
-- INICIALIZAÇÃO E LOOP
-- =============================================================================

-- Cria a janela. Retorna nil se falhar.
-- Exemplo: local jogo = E.nova(320, 240, "Meu Jogo", 2)
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
    self._anim_atual = {}   -- [oid] → animação ativa atual
    return self
end

-- true enquanto a janela estiver aberta.
function E:rodando()
    return self._e.running ~= 0
end

--[[
    :atualizar()  — chame UMA VEZ por frame.
    Atualiza câmera, partículas, animações e áudio.

    Loop básico:
        while jogo:rodando() do
            jogo:eventos()
            jogo:atualizar()
            jogo:limpar()
            -- desenhe aqui
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

-- Desenha todos os objetos ativos, ordenados por (layer, z_order), além de
-- partículas e fade. Use junto com desenhar_mapa_ate/desenhar_mapa_de para
-- intercalar o mapa com objetos de camadas diferentes.
function E:desenhar()
    lib.engine_draw(self._e)
    lib.engine_particles_draw(self._e)
    lib.engine_fade_draw(self._e)
end

-- Desenha partículas e fade ativos. Use quando o loop intercala camadas
-- manualmente (com desenhar_layer/desenhar_mapa_ate/desenhar_mapa_de) e
-- precisa controlar onde efeitos aparecem na pilha de renderização.
function E:desenhar_efeitos()
    lib.engine_particles_draw(self._e)
    lib.engine_fade_draw(self._e)
end
--[[
    Desenha APENAS os objetos cuja camada (definida com :camada()) seja igual
    a `layer`, ordenados por z_order. Útil para intercalar objetos entre
    camadas do mapa sem usar engine_draw() completo.

    Exemplo — player entre camada 0 (chão) e camada 1 (telhado):
        v:desenhar_mapa_ate(mapa, 0)   -- tiles do chão
        v:desenhar_layer(0)            -- objetos na camada 0 (player no chão)
        v:desenhar_mapa_de(mapa, 1)    -- tiles do telhado por cima
        -- (objetos em camada >= 1 seriam desenhados por uma chamada separada)
]]
function E:desenhar_layer(layer)
    lib.engine_draw_layer(self._e, layer or 0)
end

-- Exibe o frame na tela. Chame no fim do loop.
function E:apresentar()
    lib.engine_flush(self._e)
    lib.engine_present(self._e)
end

-- Limita o FPS. Chame no fim do loop.
function E:fps(alvo)
    lib.engine_cap_fps(self._e, alvo or 60)
end

-- Libera recursos e fecha a janela.
function E:destruir()
    lib.engine_audio_destroy(self._e)
    lib.engine_destroy(self._e)
end

-- Segundos desde o início.
function E:tempo()
    return lib.engine_get_time(self._e)
end

-- Duração do último frame em segundos (delta time).
function E:delta()
    return lib.engine_get_delta(self._e)
end


-- =============================================================================
-- JANELA
-- =============================================================================

-- Cor de fundo. Valores 0-255.
function E:fundo(r, g, b)
    lib.engine_set_background(self._e, r, g, b)
end

-- Alterna janela / tela cheia.
function E:tela_cheia()
    lib.engine_toggle_fullscreen(self._e)
end

-- Retorna largura e altura da área de desenho em pixels lógicos.
function E:tamanho()
    return self._e.render_w, self._e.render_h
end


-- =============================================================================
-- SPRITES
-- =============================================================================

-- Carrega um PNG. Retorna sprite_id (-1 se não encontrar).
-- Exemplo: local sid = jogo:carregar_sprite("heroi.png")
function E:carregar_sprite(caminho)
    return lib.engine_load_sprite(self._e, caminho)
end

-- Carrega uma região de um spritesheet. Retorna sprite_id.
-- Exemplo: local sid = jogo:carregar_regiao("sheet.png", 0, 0, 32, 32)
function E:carregar_regiao(caminho, x, y, w, h)
    return lib.engine_load_sprite_region(self._e, caminho, x, y, w, h)
end


-- =============================================================================
-- OBJETOS
-- =============================================================================

-- Cria um objeto na posição (x,y). Retorna object_id.
-- Exemplo: local heroi = jogo:criar_objeto(100, 200, sid, 32, 32)
function E:criar_objeto(x, y, sid, w, h)
    return lib.engine_add_object(self._e, x, y, sid,
        w or 0, h or 0, 255, 255, 255)
end

-- Cria um objeto a partir de um tile específico de um spritesheet.
-- Exemplo: local arvore = jogo:criar_objeto_tile(64, 128, sid, 2, 0, 16, 16)
function E:criar_objeto_tile(x, y, sid, tx, ty, tw, th)
    return lib.engine_add_tile_object(self._e, x, y, sid, tx, ty, tw, th)
end

-- Remove um objeto da cena.
function E:remover_objeto(oid)
    lib.engine_remove_object(self._e, oid)
end

-- Move o objeto por deslocamento relativo.
-- Exemplo: jogo:mover(heroi, 2, 0)  → 2px para a direita
function E:mover(oid, dx, dy)
    lib.engine_move_object(self._e, oid, dx, dy)
end

-- Coloca o objeto em uma posição exata.
function E:posicionar(oid, x, y)
    lib.engine_set_object_pos(self._e, oid, x, y)
end

-- Retorna a posição atual do objeto (x, y).
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

-- Espelha o sprite. true/false para cada eixo.
-- Exemplo: jogo:espelhar(heroi, true, false)  → vira para a esquerda
function E:espelhar(oid, h, v)
    lib.engine_set_object_flip(self._e, oid, h and 1 or 0, v and 1 or 0)
end

-- Altera o tamanho do objeto. 1.0 = normal, 2.0 = dobro.
function E:escala(oid, sx, sy)
    lib.engine_set_object_scale(self._e, oid, sx or 1, sy or 1)
end

-- Rotaciona o objeto em graus (sentido horário).
function E:rotacao(oid, graus)
    lib.engine_set_object_rotation(self._e, oid, graus or 0)
end

-- Transparência. 0 = invisível, 1 = opaco.
function E:transparencia(oid, a)
    lib.engine_set_object_alpha(self._e, oid, a or 1)
end

-- Define em qual camada o objeto é desenhado (maior = na frente).
-- Exemplo: jogo:camada(heroi, 2, 0)
function E:camada(oid, layer, z)
    lib.engine_set_object_layer(self._e, oid, layer or 0, z or 0)
end

-- Define a hitbox do objeto (pode ser menor que o sprite).
-- Exemplo: jogo:hitbox(heroi, 6, 4, 20, 28)
function E:hitbox(oid, ox, oy, w, h)
    lib.engine_set_object_hitbox(self._e, oid, ox or 0, oy or 0, w, h)
end


-- =============================================================================
-- COLISÃO
-- =============================================================================

-- true se dois objetos estão se tocando.
function E:colidem(oid1, oid2)
    return lib.engine_check_collision(self._e, oid1, oid2) ~= 0
end

-- true se um ponto (px,py) está dentro da hitbox do objeto.
-- Útil para clicar em objetos com o mouse.
function E:colide_ponto(oid, px, py)
    return lib.engine_check_collision_point(self._e, oid, px, py) ~= 0
end

-- true se o objeto colide com um retângulo qualquer.
function E:colide_ret(oid, rx, ry, rw, rh)
    return lib.engine_check_collision_rect(self._e, oid, rx, ry, rw, rh) ~= 0
end

--[[
    :camada_colisao(oid, layer, mask)
    Define em qual(is) camada(s) de colisão o objeto pertence e com quais
    ele interage.  Ambos são bitmasks de 32 bits.

    Constantes prontas: E.LAYER_DEFAULT, E.LAYER_PLAYER, E.LAYER_ENEMY,
                        E.LAYER_BULLET, E.LAYER_WALL, E.LAYER_ITEM,
                        E.LAYER_SENSOR, E.LAYER_ALL

    Exemplo — bala do jogador não colide com outras balas:
        jogo:camada_colisao(bala, E.LAYER_BULLET,
                             bit.bor(E.LAYER_ENEMY, E.LAYER_WALL))

    Exemplo — item no chão (sensor, colide com jogador):
        jogo:camada_colisao(item, E.LAYER_ITEM, E.LAYER_PLAYER)
        jogo:sensor(item, true)
]]
function E:camada_colisao(oid, layer, mask)
    lib.engine_set_collision_layer(self._e, oid,
        layer or ffi.C.ENGINE_LAYER_DEFAULT,
        mask  or ffi.C.ENGINE_LAYER_ALL)
end

--[[
    :sensor(oid, ativo)
    Marca o objeto como sensor (ativo=true) ou sólido (ativo=false, padrão).

    Sensores reportam o overlap nas funções sgrid_*_colisao mas NÃO bloqueiam
    o movimento.  A lógica de resposta (coletar item, ativar trigger) fica
    inteiramente no código Lua.

    Exemplo:
        jogo:sensor(portal, true)
        -- no loop:
        if jogo:sgrid_primeira_colisao(jogador) == portal then
            -- trocar de fase
        end
]]
function E:sensor(oid, ativo)
    lib.engine_set_sensor(self._e, oid, ativo and 1 or 0)
end

-- true se o objeto é um sensor.
function E:eh_sensor(oid)
    return lib.engine_is_sensor(self._e, oid) ~= 0
end

-- Constantes de layer de colisão (espelham os defines do C)
E.LAYER_DEFAULT = 1        -- (1 << 0)
E.LAYER_PLAYER  = 2        -- (1 << 1)
E.LAYER_ENEMY   = 4        -- (1 << 2)
E.LAYER_BULLET  = 8        -- (1 << 3)
E.LAYER_WALL    = 16       -- (1 << 4)
E.LAYER_ITEM    = 32       -- (1 << 5)
E.LAYER_SENSOR  = 64       -- (1 << 6)
E.LAYER_ALL     = 0xFFFFFFFF


-- =============================================================================
-- CÂMERA
-- =============================================================================

-- Move a câmera para uma posição exata no mundo.
function E:cam_pos(x, y)
    lib.engine_camera_set(self._e, x, y)
end

-- Faz a câmera seguir um objeto. suavidade: 0=parado, 1=instantâneo (padrão 0.1).
function E:cam_seguir(oid, lerp)
    lib.engine_camera_follow(self._e, oid, lerp or 0.1)
end

-- Zoom da câmera. 1.0=normal, 2.0=zoom in, 0.5=zoom out.
function E:cam_zoom(z)
    lib.engine_camera_zoom(self._e, z or 1)
end

-- Sacode a câmera. Exemplo: jogo:cam_tremor(6, 0.3) → tremor de impacto.
function E:cam_tremor(intensidade, duracao)
    lib.engine_camera_shake(self._e, intensidade or 4, duracao or 0.3)
end

-- Converte posição do mundo para pixels na tela.
function E:mundo_para_tela(wx, wy)
    local sx, sy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_world_to_screen(self._e, wx, wy, sx, sy)
    return sx[0], sy[0]
end

-- Converte posição da tela para coordenadas do mundo.
function E:tela_para_mundo(sx, sy)
    local wx, wy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_screen_to_world(self._e, sx, sy, wx, wy)
    return wx[0], wy[0]
end


-- =============================================================================
-- PARTÍCULAS
-- =============================================================================

--[[
    :criar_emissor(cfg)  — cria um emissor de partículas. Retorna emitter_id.

    Campos de cfg (todos opcionais):
        x, y        — posição inicial
        vel         — {vx_min, vx_max, vy_min, vy_max}
        grav        — {acel_x, acel_y}
        vida        — {seg_min, seg_max}
        tamanho     — {inicio, fim}
        cor         — {r, g, b, a}  (0..1)
        cor_final   — {r, g, b, a}
        rate        — partículas por segundo (0 = só burst manual)
        max         — máximo simultâneo

    Exemplo:
        local fogo = jogo:criar_emissor({
            x=100, y=200,
            vel={-30,30,-60,-20},
            vida={0.3,0.8},
            cor={1,0.5,0,1}, cor_final={1,0,0,0},
            max=50
        })
        jogo:burst(fogo, 30)
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

-- Dispara N partículas de uma vez (explosão).
function E:burst(eid, n)
    lib.engine_emitter_burst(self._e, eid, n or 20)
end

-- Remove o emissor.
function E:remover_emissor(eid)
    lib.engine_emitter_remove(self._e, eid)
end


-- =============================================================================
-- ANIMAÇÃO
-- =============================================================================

--[[
    :criar_animacao(sid, tw, th, colunas, linhas, fps, loop, oid)
    Cria uma animação de tiles. A animação nasce pausada; use anim_tocar() para iniciar.

        sid     — spritesheet
        tw, th  — tamanho do tile em pixels
        colunas — frames: {0,1,2,3}
        linhas  — uma ou mais linhas: {0} ou {0,0,1,1}
        fps     — velocidade (frames por segundo)
        loop    — true = repete, false = toca uma vez
        oid     — objeto animado automaticamente

    Exemplo:
        local correr = jogo:criar_animacao(sid, 32, 32, {0,1,2,3}, {0}, 8, true, heroi)
        jogo:anim_tocar(correr)
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
        ativo=false, fim=false
    }
    self._anims[id] = anim
    return anim
end

-- Toca a animação. Se já for a ativa do objeto, não reinicia.
function E:anim_tocar(anim)
    local oid = anim.oid
    if oid then
        local atual = self._anim_atual[oid]
        if atual == anim then
            if anim.ativo and not anim.fim then return end
        elseif atual then
            atual.ativo = false
        end
        self._anim_atual[oid] = anim
    end
    if not anim.ativo or anim.fim then
        anim.idx = 1; anim.timer = 0; anim.fim = false
    end
    anim.ativo = true
    if oid and #anim.frames > 0 then
        local f = anim.frames[anim.idx]
        lib.engine_set_object_tile(self._e, oid, f[1], f[2])
    end
end

-- Para a animação. Opcionalmente exibe um frame estático (coluna, linha).
function E:anim_parar(anim, coluna, linha)
    anim.ativo = false; anim.idx = 1; anim.timer = 0; anim.fim = false
    if anim.oid and self._anim_atual[anim.oid] == anim then
        self._anim_atual[anim.oid] = nil
    end
    if coluna ~= nil and anim.oid then
        lib.engine_set_object_tile(self._e, anim.oid, coluna, linha or 0)
    end
end

-- Retorna a animação ativa do objeto, ou nil.
function E:anim_atual(oid)
    return self._anim_atual[oid]
end

-- true quando uma animação sem loop chegou ao último frame.
function E:anim_fim(anim)
    return anim.fim
end

-- Remove a animação da memória.
function E:anim_destruir(anim)
    if anim.oid and self._anim_atual[anim.oid] == anim then
        self._anim_atual[anim.oid] = nil
    end
    self._anims[anim._id] = nil
end

-- (interna) Avança todas as animações. Chamada por :atualizar().
function E:_atualizar_anims(dt)
    if dt > 0.1 then dt = 0.1 end
    for _, a in pairs(self._anims) do
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
    alvo 0=aparece (fade in), 1=escurece (fade out). velocidade em unidades/seg.

    Exemplo:
        jogo:fade(1, 2)        -- escurece em 0.5s
        -- aguarde jogo:fade_ok()
        jogo:fade(0, 2)        -- clareia
]]
function E:fade(alvo, vel, r, g, b)
    lib.engine_fade_to(self._e, alvo or 1, vel or 1, r or 0, g or 0, b or 0)
end

-- true quando o fade terminou.
function E:fade_ok()
    return lib.engine_fade_done(self._e) ~= 0
end


-- =============================================================================
-- DESENHO (primitivas e sprites diretos)
-- =============================================================================

-- Retângulo preenchido. Cores 0-255.
function E:ret(x, y, w, h, r, g, b)
    lib.engine_draw_rect(self._e, x, y, w, h, r, g, b)
end

-- Contorno de retângulo.
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

-- Retângulo semitransparente. alpha 0-1.
function E:overlay(x, y, w, h, r, g, b, alpha)
    lib.engine_draw_overlay(self._e, x, y, w, h, r, g, b, alpha or 0.5)
end

--[[
    :sprite(sid, x, y, sx, sy, sw, sh, opcoes)
    Desenha uma região (sx,sy,sw,sh) de um sprite na posição (x,y).

    opcoes (tabela opcional):
        escala_x, escala_y  — padrão 1
        rotacao             — graus, padrão 0
        alpha               — 0..1, padrão 1
        fh, fv              — espelhar H/V, padrão false
        invertido           — true = efeito de dano (cores invertidas)

    Exemplo simples:  jogo:sprite(sid, 0, 0, 0, 0, 320, 240)
    Com opções:       jogo:sprite(sid, 50, 100, 0, 0, 32, 32, {rotacao=45})
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
    :tilemap(mapa, linhas, colunas, sid, tw, th, ox, oy)
    Desenha um mapa de tiles a partir de uma tabela linear (linha por linha).

    Exemplo:
        local grade = {1,1,1, 1,0,1, 1,1,1}
        jogo:tilemap(grade, 3, 3, sid, 16, 16, 0, 0)
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
    :texto(x, y, txt, sid, fw, fh, chars_por_linha, offset_ascii, espacamento)
    Escreve texto usando uma fonte bitmap.

        fw, fh          — tamanho de cada caractere em pixels
        chars_por_linha — colunas na imagem da fonte (padrão 16)
        offset_ascii    — código ASCII do primeiro char (padrão 32 = espaço)
        espacamento     — pixels extras entre chars (padrão 0)

    Exemplo: jogo:texto(10, 10, "Olá!", sid_fonte, 8, 8)
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

-- Overlay escuro de noite. intensidade: 0=dia, 1=noite total.
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

-- Posição do cursor em coordenadas de tela (x, y).
function E:mouse_pos()
    local mx, my = ffi.new("int[1]"), ffi.new("int[1]")
    lib.engine_mouse_pos(self._e, mx, my)
    return mx[0], my[0]
end

-- +1 (scroll para cima), -1 (para baixo) ou 0.
function E:mouse_scroll()
    return lib.engine_mouse_scroll(self._e)
end


-- =============================================================================
-- ÁUDIO
-- =============================================================================

-- Inicializa o áudio. Chame logo após E.nova(). Retorna true se funcionou.
function E:audio_init()
    return lib.engine_audio_init(self._e) ~= 0
end

--[[
    :tocar(arquivo, loop, volume, pitch)  — toca um arquivo de áudio.
    Retorna handle para controlar depois. loop=false, volume=1.0, pitch=1.0.

    Exemplo:
        local musica = jogo:tocar("musica.ogg", true)
        local som    = jogo:tocar("pulo.wav")
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

-- Para e libera um som.
function E:parar(h)
    if h and h ~= -1 then lib.engine_audio_stop(self._e, h) end
end

-- Ajusta o volume em tempo real (0 a 1).
function E:volume(h, v)
    if h and h ~= -1 then lib.engine_audio_volume(self._e, h, v or 1) end
end

-- Ajusta o pitch em tempo real (0.5 a 2.0).
function E:pitch(h, p)
    if h and h ~= -1 then lib.engine_audio_pitch(self._e, h, p or 1) end
end

-- true quando um som sem loop terminou.
function E:audio_fim(h)
    if not h or h == -1 then return true end
    return lib.engine_audio_done(self._e, h) ~= 0
end


-- =============================================================================
-- SHADERS GLSL
-- =============================================================================
-- Fluxo: sh = jogo:shader_criar(vert, frag) → jogo:shader_usar(sh) → desenhar → jogo:shader_nenhum()

-- Vertex padrão (pass-through com câmera ortográfica).
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

-- Fragment: inverte as cores.
E.FRAG_NEGATIVO = [[
uniform sampler2D u_tex;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4 c = texture2D(u_tex, v_uv) * v_color;
    gl_FragColor = vec4(1.0 - c.rgb, c.a);
}
]]

-- Fragment: tinta colorida. Uniform vec4 "u_tint".
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

-- Compila e linka um shader GLSL. Retorna handle ou -1 em erro.
function E:shader_criar(vert_src, frag_src)
    return lib.engine_shader_create(self._e, vert_src, frag_src)
end

-- Libera um shader.
function E:shader_destruir(sh)
    if sh and sh ~= -1 then lib.engine_shader_destroy(self._e, sh) end
end

-- Ativa um shader para os próximos draws.
function E:shader_usar(sh)
    if sh and sh ~= -1 then lib.engine_shader_use(self._e, sh) end
end

-- Desativa o shader e volta ao modo padrão.
function E:shader_nenhum()
    lib.engine_shader_none(self._e)
end

--[[
    :shader_uniform(sh, nome, ...)  — define um uniform. Detecta o tipo pelo número de valores.

    Exemplos:
        jogo:shader_uniform(sh, "u_tempo",     0.5)           -- float
        jogo:shader_uniform(sh, "u_resolucao", 320, 240)      -- vec2
        jogo:shader_uniform(sh, "u_tint",      1, 0.5, 0, 1) -- vec4
        jogo:shader_uniform(sh, "u_modo",      1)             -- int
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
    :efeito(nome, ativar, ...)  — ativa/desativa um efeito visual pronto.
    Os shaders são compilados uma única vez e reutilizados.

    Nomes: "cinza", "negativo", "crt", "aberracao"

    Exemplos:
        jogo:efeito("cinza", true)
        jogo:efeito("crt", true, 320, 240)
        jogo:efeito("aberracao", true, 0.005)
        jogo:efeito("cinza", false)   -- desativa qualquer efeito
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
-- FBO (renderização offscreen)
-- =============================================================================
-- Use para efeitos que precisam de uma textura intermediária.
-- Fluxo: fbo=fbo_criar(w,h) → fbo_bind(fbo) → limpar()+desenhar()
--        → fbo_unbind() → sid=fbo_como_sprite(fbo) → sprite(sid,...)

-- Cria um framebuffer RGBA w×h. Retorna handle ou -1.
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

-- Registra o conteúdo do FBO como sprite. Retorna sprite_id.
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
-- FOV — Campo de Visão com Sombras
-- =============================================================================
--[[
    Fluxo:
        local fov = jogo:fov_novo(cols, lins, raio, modo)
        -- por frame:
        jogo:fov_calcular(fov, col_jogador, lin_jogador, funcao_parede)
        jogo:fov_sombra(fov, tw, th, ox, oy)   -- após o mundo, antes da UI

    Modos:
        0 = BASICO   — sem memória; tudo escuro fora da visão
        1 = NEBLINA  — tiles visitados ficam semi-visíveis (padrão)
        2 = SUAVE    — falloff gradual nas bordas
]]

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
    :fov_calcular(fov, col, lin, funcao_parede)
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
    Chame APÓS o mundo e ANTES da UI. ox,oy = mesmo deslocamento do tilemap.
]]
function E:fov_sombra(fov, tile_w, tile_h, offset_x, offset_y, r, g, b)
    C.engine_fov_draw_shadow(self._e, fov._vis,
        fov.map_cols, fov.map_rows,
        tile_w, tile_h,
        offset_x or 0, offset_y or 0,
        r or 0, g or 0, b or 0)
end

-- true se o tile está visível no frame atual.
function E:fov_visivel(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] >= 0.99
end

-- true se o tile já foi explorado ao menos uma vez (modo NEBLINA).
function E:fov_explorado(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] > 0.0
end


-- =============================================================================
-- GRADE ESPACIAL (colisões eficientes com muitos objetos)
-- =============================================================================
--[[
    Use quando tiver muitos objetos e precisar de colisões rápidas.
    Fluxo: sgrid_init() → (loop) sgrid_primeira_colisao() / sgrid_todas_colisoes()
    Objetos criados/removidos são inseridos/retirados da grade automaticamente.
]]

-- Ativa a grade. cell_size = tamanho de cada célula em pixels (0 = automático).
function E:sgrid_init(cell_size)
    lib.engine_sgrid_init(self._e, cell_size or 0)
end

-- Desativa e limpa a grade.
function E:sgrid_destruir()
    lib.engine_sgrid_destroy(self._e)
end

-- Reinsere todos os objetos (use após criar muitos de uma vez).
function E:sgrid_rebuild()
    lib.engine_sgrid_rebuild(self._e)
end

-- Retorna o ID do primeiro objeto que colide com oid, ou nil.
-- Exemplo: local alvo = jogo:sgrid_primeira_colisao(projetil)
function E:sgrid_primeira_colisao(oid)
    local r = lib.engine_sgrid_first_collision(self._e, oid)
    return r >= 0 and r or nil
end

-- Retorna tabela com IDs de todos os objetos que colidem com oid.
-- Exemplo: for _, id in ipairs(jogo:sgrid_todas_colisoes(explosao)) do ... end
function E:sgrid_todas_colisoes(oid, cap)
    cap = cap or 64
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_all_collisions(self._e, oid, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- true se a grade está ativa.
function E:sgrid_ativo()
    return self._e.sgrid.enabled ~= 0
end


-- =============================================================================
-- MAPA DE TILES
-- =============================================================================
--[[
    Fluxo básico:
        local mapa = engine:carregar_mapa("fase1.lua")
        -- no loop:
        engine:desenhar_mapa(mapa)
        if engine:colide_mapa(mapa, jogador) then ... end

    O arquivo .lua deve terminar com: return m:build()
]]

-- Carrega um arquivo .lua de mapa. Retorna o mapa pronto para uso.
function E:carregar_mapa(caminho)
    local fn, err = loadfile(caminho)
    if not fn then
        error("[carregar_mapa] Não consegui abrir '" .. caminho .. "': " .. tostring(err))
    end
    local dados = fn()
    if type(dados) ~= "table" then
        error("[carregar_mapa] '" .. caminho .. "' deve retornar m:build()")
    end

    if dados.sprite_atlas then
        dados._atlas_sid = self:carregar_sprite(dados.sprite_atlas)
    else
        dados._atlas_sid = -1
    end

    -- Monta tabela de colisão [lin][col] — feita uma única vez ao carregar.
    local COLISOR = 1
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

    -- Pré-calcula tiles por camada (layer, z) — feito uma única vez ao carregar.
    -- Estrutura: _buckets = { {layer=N, z=N, tiles={...}}, ... } ordenado por (layer,z).
    -- Isso preserva a ordem de renderização e permite intercalar objetos entre camadas.
    local tw = dados.tile_w or 16
    local th = dados.tile_h or 16

    local bucket_index = {}   -- "layer:z" → bucket
    local buckets      = {}

    for _, camada in ipairs(dados.camadas or {}) do
        if camada.visivel ~= false then
            local layer = camada.layer or 0
            local z     = camada.z     or 0
            local key   = layer .. ":" .. z

            if not bucket_index[key] then
                local b = { layer = layer, z = z, tiles = {} }
                bucket_index[key] = b
                buckets[#buckets + 1] = b
            end
            local bucket = bucket_index[key]

            for _, tile in ipairs(camada.tiles or {}) do
                bucket.tiles[#bucket.tiles + 1] = {
                    px         = tile.col        * tw,
                    py         = tile.lin        * th,
                    sx         = tile.sprite_col * tw,
                    sy         = tile.sprite_lin * th,
                    sprite_lin = tile.sprite_lin,
                    anim_cols  = tile.anim_cols,
                    anim_lins  = tile.anim_lins,
                    anim_fps   = tile.anim_fps,
                    -- anim_loop=false trava no último frame; nil/true = loop infinito
                    anim_loop  = tile.anim_loop,
                    -- momento (em segundos de jogo) em que a animação foi iniciada;
                    -- usado para calcular o frame correto com loop=false
                    anim_start = 0,
                }
            end
        end
    end

    -- Ordena buckets por (layer, z) para renderização correta.
    table.sort(buckets, function(a, b)
        if a.layer ~= b.layer then return a.layer < b.layer end
        return a.z < b.z
    end)

    dados._buckets = buckets
    dados._tile_w  = tw
    dados._tile_h  = th

    return dados
end

-- Desenha tiles de um bucket respeitando anim_loop.
local function _desenhar_bucket(lib, e_ptr, sid, tw, th, tempo, bucket)
    for _, t in ipairs(bucket.tiles) do
        local sx, sy = t.sx, t.sy
        if t.anim_cols then
            local fps  = t.anim_fps or 4
            local nf   = #t.anim_cols
            local loop = t.anim_loop  -- nil ou true = loop; false = congela no fim

            local frame
            if loop == false then
                -- Sem loop: calcula quantos frames já passaram e trava no último.
                local elapsed = tempo - (t.anim_start or 0)
                frame = math.floor(elapsed * fps) + 1
                if frame > nf then frame = nf end
            else
                -- Loop infinito (comportamento padrão).
                frame = math.floor(tempo * fps) % nf + 1
            end

            sx = t.anim_cols[frame] * tw
            sy = (t.anim_lins and t.anim_lins[frame] or t.sprite_lin) * th
        end
        lib.engine_draw_sprite_part(e_ptr, sid, t.px, t.py, sx, sy, tw, th)
    end
end

--[[
    :desenhar_mapa(mapa)
    Desenha TODAS as camadas do mapa em ordem (layer, z).
    Chame no loop ANTES de engine:desenhar() se o player deve ficar
    sempre por cima de tudo. Para intercalar, use as variantes abaixo.
]]
function E:desenhar_mapa(mapa)
    local sid = mapa._atlas_sid
    if not sid or sid < 0 then return end
    local tw    = mapa._tile_w
    local th    = mapa._tile_h
    local tempo = lib.engine_get_time(self._e)
    for _, bucket in ipairs(mapa._buckets or {}) do
        _desenhar_bucket(lib, self._e, sid, tw, th, tempo, bucket)
    end
end

--[[
    :desenhar_mapa_ate(mapa, layer_max)
    Desenha apenas as camadas com layer <= layer_max.
    Use para colocar objetos SOBRE determinadas camadas do mapa.

    Exemplo — player entre camada 0 (chão) e camada 1 (telhado):
        v:desenhar_mapa_ate(mapa, 0)   -- chão
        v:desenhar()                   -- player e objetos
        v:desenhar_mapa_de(mapa, 1)    -- telhado por cima
]]
function E:desenhar_mapa_ate(mapa, layer_max)
    local sid = mapa._atlas_sid
    if not sid or sid < 0 then return end
    local tw    = mapa._tile_w
    local th    = mapa._tile_h
    local tempo = lib.engine_get_time(self._e)
    for _, bucket in ipairs(mapa._buckets or {}) do
        if bucket.layer <= layer_max then
            _desenhar_bucket(lib, self._e, sid, tw, th, tempo, bucket)
        end
    end
end

--[[
    :desenhar_mapa_de(mapa, layer_min)
    Desenha apenas as camadas com layer >= layer_min.
    Use em par com desenhar_mapa_ate() para intercalar objetos.
]]
function E:desenhar_mapa_de(mapa, layer_min)
    local sid = mapa._atlas_sid
    if not sid or sid < 0 then return end
    local tw    = mapa._tile_w
    local th    = mapa._tile_h
    local tempo = lib.engine_get_time(self._e)
    for _, bucket in ipairs(mapa._buckets or {}) do
        if bucket.layer >= layer_min then
            _desenhar_bucket(lib, self._e, sid, tw, th, tempo, bucket)
        end
    end
end

--[[
    :colide_mapa(mapa, oid)  — true se o objeto bate em um tile sólido.
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