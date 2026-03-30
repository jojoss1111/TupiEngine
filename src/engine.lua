-- engine.lua
-- Wrapper Lua da engine 2D. Todas as funções em português.
-- Carrega a lib com RTLD_GLOBAL para que fov.cpp seja visível via ffi.C.

local ffi = require("engineffi")
local lib  = ffi.load("./libengine.so", true)   -- true = RTLD_GLOBAL
local C    = ffi.C                               -- acessa símbolos globais (fov, etc.)
local math = math

local E = {}
E.__index = E

-- Botões do mouse
E.ESQ  = 0
E.MEIO = 1
E.DIR  = 2

-- ================================================================
-- INIT / LOOP
-- ================================================================

-- Abre janela e retorna o objeto engine. Retorna nil em falha.
function E.nova(largura, altura, titulo, escala)
    local ptr = ffi.new("Engine[1]")
    if lib.engine_init(ptr[0], largura, altura, titulo or "", escala or 1) == 0 then
        return nil
    end
    local self = setmetatable({}, E)
    self._e       = ptr[0]
    self._ptr     = ptr
    self._anims   = {}
    self._anim_id = 0
    return self
end

-- true enquanto a janela estiver aberta.
function E:rodando() return self._e.running ~= 0 end

-- Processa eventos de teclado, mouse e janela.
function E:eventos() lib.engine_poll_events(self._e) end

-- Limpa a tela com a cor de fundo.
function E:limpar() lib.engine_clear(self._e) end

-- Desenha todos os objetos ativos.
function E:desenhar() lib.engine_draw(self._e) end

-- Envia o batch pendente para a GPU.
function E:flush() lib.engine_flush(self._e) end

-- Exibe o frame na tela (swap de buffers).
function E:apresentar() lib.engine_present(self._e) end

-- Limita FPS. Chame no final do loop.
function E:fps(alvo) lib.engine_cap_fps(self._e, alvo or 60) end

-- Atualiza câmera, partículas, animações e áudio. dt=0 usa delta interno.
function E:atualizar(dt)
    local d = dt or 0
    lib.engine_update(self._e, d)
    lib.engine_audio_update(self._e)
    self:_atualizar_anims(d)
end

-- Libera recursos e fecha a janela.
function E:destruir()
    lib.engine_audio_destroy(self._e)
    lib.engine_destroy(self._e)
end

-- Segundos desde o início.
function E:tempo() return lib.engine_get_time(self._e) end

-- Duração do último frame em segundos.
function E:delta() return lib.engine_get_delta(self._e) end

-- ================================================================
-- JANELA
-- ================================================================

-- Cor de fundo RGB (0-255).
function E:fundo(r, g, b) lib.engine_set_background(self._e, r, g, b) end

-- Alterna janela / tela cheia.
function E:tela_cheia() lib.engine_toggle_fullscreen(self._e) end

-- Retorna largura e altura lógica do render.
function E:tamanho() return self._e.render_w, self._e.render_h end

-- ================================================================
-- SPRITES
-- ================================================================

-- Carrega PNG inteiro. Retorna sprite_id (>= 0) ou -1 em falha.
function E:carregar_sprite(caminho)
    return lib.engine_load_sprite(self._e, caminho)
end

-- Carrega região de um PNG. Retorna sprite_id.
function E:carregar_sprite_regiao(caminho, x, y, w, h)
    return lib.engine_load_sprite_region(self._e, caminho, x, y, w, h)
end

-- ================================================================
-- OBJETOS
-- ================================================================

-- Cria objeto com sprite e tint RGB. Retorna object_id.
function E:criar_objeto(x, y, sid, w, h, r, g, b)
    return lib.engine_add_object(self._e, x, y, sid,
        w or 0, h or 0, r or 255, g or 255, b or 255)
end

-- Cria objeto usando tile de tileset. Retorna object_id.
function E:criar_objeto_tile(x, y, sid, tx, ty, tw, th)
    return lib.engine_add_tile_object(self._e, x, y, sid, tx, ty, tw, th)
end

-- Remove objeto da cena.
function E:remover_objeto(oid) lib.engine_remove_object(self._e, oid) end

-- Move por deslocamento relativo (dx, dy).
function E:mover(oid, dx, dy) lib.engine_move_object(self._e, oid, dx, dy) end

-- Posiciona em coordenadas absolutas.
function E:pos(oid, x, y) lib.engine_set_object_pos(self._e, oid, x, y) end

-- Retorna x, y do objeto.
function E:get_pos(oid)
    local ox, oy = ffi.new("int[1]"), ffi.new("int[1]")
    lib.engine_get_object_pos(self._e, oid, ox, oy)
    return ox[0], oy[0]
end

-- Troca o sprite do objeto.
function E:objeto_sprite(oid, sid) lib.engine_set_object_sprite(self._e, oid, sid) end

-- Define qual tile do tileset o objeto exibe.
function E:objeto_tile(oid, tx, ty) lib.engine_set_object_tile(self._e, oid, tx, ty) end

-- Espelha o objeto horizontal (h) e/ou vertical (v).
function E:espelhar(oid, h, v)
    lib.engine_set_object_flip(self._e, oid, h and 1 or 0, v and 1 or 0)
end

-- Escala do objeto (1.0 = normal).
function E:escala(oid, sx, sy) lib.engine_set_object_scale(self._e, oid, sx or 1, sy or 1) end

-- Rotação em graus (sentido horário).
function E:rotacao(oid, graus) lib.engine_set_object_rotation(self._e, oid, graus or 0) end

-- Transparência do objeto (0 = invisível, 1 = opaco).
function E:alfa(oid, a) lib.engine_set_object_alpha(self._e, oid, a or 1) end

-- Camada e ordem Z para profundidade de desenho.
function E:camada(oid, layer, z) lib.engine_set_object_layer(self._e, oid, layer or 0, z or 0) end

-- Hitbox com offset e tamanho independentes do sprite.
function E:hitbox(oid, ox, oy, w, h)
    lib.engine_set_object_hitbox(self._e, oid, ox or 0, oy or 0, w, h)
end

-- ================================================================
-- COLISÃO
-- ================================================================

-- AABB entre dois objetos. true se colidem.
function E:colidem(oid1, oid2)
    return lib.engine_check_collision(self._e, oid1, oid2) ~= 0
end

-- Objeto vs retângulo. true se colidem.
function E:colide_rect(oid, rx, ry, rw, rh)
    return lib.engine_check_collision_rect(self._e, oid, rx, ry, rw, rh) ~= 0
end

-- true se o ponto (px,py) está dentro da hitbox do objeto.
function E:colide_ponto(oid, px, py)
    return lib.engine_check_collision_point(self._e, oid, px, py) ~= 0
end

-- AABB entre dois retângulos puros.
function E:rects_colidem(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx+bw and ax+aw > bx and ay < by+bh and ay+ah > by
end

-- true se o ponto está dentro do retângulo.
function E:ponto_em_rect(px, py, rx, ry, rw, rh)
    return px >= rx and px < rx+rw and py >= ry and py < ry+rh
end

-- true se dois círculos colidem.
function E:circulos_colidem(cx1, cy1, r1, cx2, cy2, r2)
    local dx, dy = cx1-cx2, cy1-cy2
    local s = r1+r2
    return dx*dx + dy*dy < s*s
end

-- Vetor de sobreposição (ox, oy) entre dois rects. nil se não colidem.
function E:sobreposicao(ax, ay, aw, ah, bx, by, bw, bh)
    local ox = math.min(ax+aw, bx+bw) - math.max(ax, bx)
    local oy = math.min(ay+ah, by+bh) - math.max(ay, by)
    if ox <= 0 or oy <= 0 then return nil end
    if ox < oy then
        return ox * (ax+aw/2 < bx+bw/2 and -1 or 1), 0
    else
        return 0, oy * (ay+ah/2 < by+bh/2 and -1 or 1)
    end
end

-- ================================================================
-- CÂMERA
-- ================================================================

-- Posiciona a câmera em coordenadas de mundo.
function E:cam_pos(x, y) lib.engine_camera_set(self._e, x, y) end

-- Move a câmera por deslocamento relativo.
function E:cam_mover(dx, dy) lib.engine_camera_move(self._e, dx, dy) end

-- Zoom da câmera (1.0 = normal).
function E:cam_zoom(z) lib.engine_camera_zoom(self._e, z or 1) end

-- Câmera segue objeto com suavidade 0..1.
function E:cam_seguir(oid, lerp) lib.engine_camera_follow(self._e, oid, lerp or 0.1) end

-- Tremor de câmera com intensidade e duração em segundos.
function E:cam_tremor(intensidade, duracao)
    lib.engine_camera_shake(self._e, intensidade or 4, duracao or 0.3)
end

-- Ativa (true) ou desativa (false) a câmera.
function E:cam_ativa(sim) lib.engine_camera_enable(self._e, sim ~= false and 1 or 0) end

-- Converte coordenadas de mundo para tela. Retorna sx, sy.
function E:mundo_para_tela(wx, wy)
    local sx, sy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_world_to_screen(self._e, wx, wy, sx, sy)
    return sx[0], sy[0]
end

-- Converte coordenadas de tela para mundo. Retorna wx, wy.
function E:tela_para_mundo(sx, sy)
    local wx, wy = ffi.new("float[1]"), ffi.new("float[1]")
    lib.engine_screen_to_world(self._e, sx, sy, wx, wy)
    return wx[0], wy[0]
end

-- ================================================================
-- PARTÍCULAS
-- ================================================================

-- Cria emissor de partículas. Retorna emitter_id.
-- cfg: {x, y, vel={vxmin,vxmax,vymin,vymax}, grav={ax,ay},
--        vida={min,max}, tamanho={ini,fim},
--        cor={r,g,b,a}, cor_final={r,g,b,a}, sprite_id, rate, max}
function E:criar_emissor(cfg)
    local em  = ffi.new("ParticleEmitter")
    local vel = cfg.vel       or {}
    local grav= cfg.grav      or {}
    local vida= cfg.vida      or {}
    local tam = cfg.tamanho   or {}
    local c0  = cfg.cor       or {}
    local c1  = cfg.cor_final or {}
    em.x            = cfg.x        or 0
    em.y            = cfg.y        or 0
    em.vx_min       = vel[1]       or -20
    em.vx_max       = vel[2]       or  20
    em.vy_min       = vel[3]       or -50
    em.vy_max       = vel[4]       or -20
    em.ax           = grav[1]      or   0
    em.ay           = grav[2]      or  80
    em.life_min     = vida[1]      or 0.5
    em.life_max     = vida[2]      or 1.5
    em.size_start   = tam[1]       or   4
    em.size_end     = tam[2]       or   0
    em.r0=c0[1] or 1; em.g0=c0[2] or 1; em.b0=c0[3] or 0.2; em.a0=c0[4] or 1
    em.r1=c1[1] or 1; em.g1=c1[2] or 0; em.b1=c1[3] or 0;   em.a1=c1[4] or 0
    em.sprite_id    = cfg.sprite_id or -1
    em.rate         = cfg.rate      or  0
    em.max_particles= cfg.max       or 100
    return lib.engine_emitter_add(self._e, em)
end

-- Reposiciona o ponto de emissão.
function E:emissor_pos(eid, x, y) lib.engine_emitter_set_pos(self._e, eid, x, y) end

-- Emite N partículas de uma vez (explosão).
function E:emissor_burst(eid, n) lib.engine_emitter_burst(self._e, eid, n or 20) end

-- Remove o emissor.
function E:remover_emissor(eid) lib.engine_emitter_remove(self._e, eid) end

-- Desenha todas as partículas ativas.
function E:desenhar_particulas() lib.engine_particles_draw(self._e) end

-- ================================================================
-- ANIMAÇÃO (tiles de sprite sheet)
-- ================================================================

-- Cria animação de tiles. colunas={0,1,2}, linhas={0} ou {0,1,2}.
-- Retorna handle de animação (tabela interna).
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
        ativo=true, fim=false
    }
    self._anims[id] = anim
    if oid and #frames > 0 then
        lib.engine_set_object_tile(self._e, oid, frames[1][1], frames[1][2])
    end
    return anim
end

-- Para a animação. col/lin opcional: exibe frame fixo ao parar.
function E:anim_parar(anim, col, lin)
    anim.ativo = false; anim.idx = 1; anim.timer = 0
    if col ~= nil and anim.oid then
        lib.engine_set_object_tile(self._e, anim.oid, col, lin or 0)
    end
end

-- Reinicia e toca a animação do início.
function E:anim_tocar(anim)
    anim.idx = 1; anim.timer = 0; anim.fim = false; anim.ativo = true
    if anim.oid and #anim.frames > 0 then
        local f = anim.frames[1]
        lib.engine_set_object_tile(self._e, anim.oid, f[1], f[2])
    end
end

-- true se animação one-shot chegou ao fim.
function E:anim_fim(anim) return anim.fim end

-- Remove a animação da memória.
function E:anim_destruir(anim) self._anims[anim._id] = nil end

-- Interna: avança todas as animações. Chamada automaticamente por atualizar().
function E:_atualizar_anims(dt)
    if dt <= 0 then dt = lib.engine_get_delta(self._e) end
    if dt > 0.1 then dt = 0.1 end
    for _, a in pairs(self._anims) do
        if a.ativo and not a.fim and #a.frames > 0 then
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

-- ================================================================
-- FADE
-- ================================================================

-- Fade para alpha alvo (0=transparente, 1=tela preta). vel em unidades/s.
function E:fade(alvo, vel, r, g, b)
    lib.engine_fade_to(self._e, alvo or 1, vel or 1, r or 0, g or 0, b or 0)
end

-- Desenha overlay de fade. Chame após desenhar().
function E:fade_draw() lib.engine_fade_draw(self._e) end

-- true quando o fade chegou ao alvo.
function E:fade_ok() return lib.engine_fade_done(self._e) ~= 0 end

-- ================================================================
-- DESENHO (primitivas e sprites)
-- ================================================================

-- Retângulo preenchido.
function E:ret(x, y, w, h, r, g, b)
    lib.engine_draw_rect(self._e, x, y, w, h, r, g, b)
end

-- Contorno de retângulo com espessura (padrão 1).
function E:ret_contorno(x, y, w, h, r, g, b, esp)
    lib.engine_draw_rect_outline(self._e, x, y, w, h, r, g, b, esp or 1)
end

-- Linha entre dois pontos.
function E:linha(x0, y0, x1, y1, r, g, b, esp)
    lib.engine_draw_line(self._e, x0, y0, x1, y1, r, g, b, esp or 1)
end

-- Círculo. preenchido=true por padrão.
function E:circulo(cx, cy, raio, r, g, b, preenchido)
    lib.engine_draw_circle(self._e, cx, cy, raio, r, g, b,
        preenchido ~= false and 1 or 0)
end

-- Overlay semitransparente. alpha 0..1 (padrão 0.5).
function E:overlay(x, y, w, h, r, g, b, a)
    lib.engine_draw_overlay(self._e, x, y, w, h, r, g, b, a or 0.5)
end

-- Desenha parte de sprite em (x,y).
function E:desenhar_sprite(sid, x, y, sx, sy, sw, sh)
    lib.engine_draw_sprite_part(self._e, sid, x, y, sx, sy, sw, sh)
end

-- Desenha parte de sprite com transformações.
-- op: {ex=1, ey=1, rot=0, alpha=1, fh=false, fv=false}
function E:desenhar_sprite_ex(sid, x, y, sx, sy, sw, sh, op)
    op = op or {}
    lib.engine_draw_sprite_part_ex(self._e, sid, x, y, sx, sy, sw, sh,
        op.ex or 1, op.ey or 1, op.rot or 0, op.alpha or 1,
        op.fh and 1 or 0, op.fv and 1 or 0)
end

-- Desenha sprite com cores invertidas (efeito de dano).
function E:desenhar_sprite_invertido(sid, x, y, sx, sy, sw, sh)
    lib.engine_draw_sprite_part_inverted(self._e, sid, x, y, sx, sy, sw, sh)
end

-- Desenha tilemap a partir de tabela linear (linha a linha).
function E:tilemap(mapa, linhas, colunas, sid, tw, th, ox, oy)
    local n = linhas * colunas
    local c = ffi.new("int[?]", n)
    for i = 1, n do c[i-1] = mapa[i] or 0 end
    lib.engine_draw_tilemap(self._e, c, linhas, colunas, sid, tw, th, ox or 0, oy or 0)
end

-- ================================================================
-- UI / TEXTO
-- ================================================================

-- Texto com fonte bitmap. chars_linha=16, offset_ascii=32.
function E:texto(x, y, txt, fsid, fw, fh, chars_linha, offset_ascii, espacamento)
    lib.engine_draw_text(self._e, x, y, txt, fsid, fw, fh,
        chars_linha or 16, offset_ascii or 32, espacamento or 0)
end

-- Caixa decorada com tileset de bordas 3×3.
function E:caixa(x, y, w, h, sid, tw, th)
    lib.engine_draw_box(self._e, x, y, w, h, sid, tw, th)
end

-- Caixa de texto com título e conteúdo.
function E:caixa_texto(x, y, w, h, titulo, conteudo,
                        bsid, btw, bth, fsid, fw, fh,
                        chars_linha, offset_ascii, esp)
    lib.engine_draw_text_box(self._e, x, y, w, h, titulo, conteudo,
        bsid, btw, bth, fsid, fw, fh,
        chars_linha or 16, offset_ascii or 32, esp or 0)
end

-- ================================================================
-- EFEITOS
-- ================================================================

-- Efeito de chuva. gotas é tabela de {x, y}.
function E:chuva(larg, alt, frame, gotas, gw, gh)
    local n = #gotas
    local gx, gy = ffi.new("int[?]", n), ffi.new("int[?]", n)
    for i, g in ipairs(gotas) do gx[i-1]=g[1]; gy[i-1]=g[2] end
    lib.engine_draw_rain(self._e, larg, alt, frame, gx, gy, n, gw or 1, gh or 4)
end

-- Overlay escuro de noite. intensidade 0..1.
function E:noite(larg, alt, intensidade, offset)
    lib.engine_draw_night(self._e, larg, alt, intensidade or 0.5, offset or 0)
end

-- ================================================================
-- INPUT — TECLADO
-- ================================================================

-- true enquanto a tecla estiver segurada.
function E:tecla(t) return lib.engine_key_down(self._e, t:lower()) ~= 0 end

-- true apenas no frame em que a tecla foi pressionada.
function E:tecla_press(t) return lib.engine_key_pressed(self._e, t:lower()) ~= 0 end

-- true apenas no frame em que a tecla foi solta.
function E:tecla_solta(t) return lib.engine_key_released(self._e, t:lower()) ~= 0 end

-- ================================================================
-- INPUT — MOUSE
-- ================================================================

-- true enquanto o botão estiver pressionado. Botões: E.ESQ / E.MEIO / E.DIR.
function E:mouse_segurado(b) return lib.engine_mouse_down(self._e, b) ~= 0 end

-- true apenas no frame em que o botão foi pressionado.
function E:mouse_press(b) return lib.engine_mouse_pressed(self._e, b) ~= 0 end

-- true apenas no frame em que o botão foi solto.
function E:mouse_solta(b) return lib.engine_mouse_released(self._e, b) ~= 0 end

-- Posição do cursor em coordenadas lógicas. Retorna x, y.
function E:mouse_pos()
    local mx, my = ffi.new("int[1]"), ffi.new("int[1]")
    lib.engine_mouse_pos(self._e, mx, my)
    return mx[0], my[0]
end

-- +1 (rolou pra cima), -1 (pra baixo) ou 0.
function E:mouse_scroll() return lib.engine_mouse_scroll(self._e) end

-- ================================================================
-- ÁUDIO
-- ================================================================

-- Inicializa o subsistema de áudio. Chame após nova(). Retorna true se ok.
function E:audio_init() return lib.engine_audio_init(self._e) ~= 0 end

-- Toca um arquivo de áudio. Retorna handle.
-- loop=false, vol=1.0, pitch=1.0, apos=-1 (handle para retomar após término).
function E:tocar(arquivo, loop, vol, pitch, apos)
    return lib.engine_audio_play(self._e, arquivo,
        loop and 1 or 0, vol or 1, pitch or 1, apos or -1)
end

-- Pausa a faixa.
function E:pausar(h) if h and h ~= -1 then lib.engine_audio_pause(self._e, h) end end

-- Retoma faixa pausada.
function E:retomar(h) if h and h ~= -1 then lib.engine_audio_resume(self._e, h) end end

-- Para e libera a faixa definitivamente.
function E:parar(h) if h and h ~= -1 then lib.engine_audio_stop(self._e, h) end end

-- Ajusta volume em tempo real (0..1).
function E:volume(h, v) if h and h ~= -1 then lib.engine_audio_volume(self._e, h, v or 1) end end

-- Ajusta pitch em tempo real (0.5..2.0).
function E:pitch(h, p) if h and h ~= -1 then lib.engine_audio_pitch(self._e, h, p or 1) end end

-- true se a faixa sem loop terminou.
function E:audio_fim(h)
    if not h or h == -1 then return true end
    return lib.engine_audio_done(self._e, h) ~= 0
end

-- ================================================================
-- FBO (Framebuffer Object — renderização offscreen)
-- ================================================================
-- Fluxo: fbo=fbo_criar(w,h) → fbo_bind(fbo) → limpar()+desenhar()
--        → fbo_unbind() → sid=fbo_sprite(fbo) → desenhar_sprite(sid,...)

-- Cria FBO com textura RGBA w×h. Retorna handle ou -1.
function E:fbo_criar(w, h)
    return lib.engine_fbo_create(self._e, w, h)
end

-- Libera o FBO e sua textura.
function E:fbo_destruir(fh)
    if fh and fh ~= -1 then lib.engine_fbo_destroy(self._e, fh) end
end

-- Redireciona o rendering para este FBO.
function E:fbo_bind(fh)
    if fh and fh ~= -1 then lib.engine_fbo_bind(self._e, fh) end
end

-- Restaura o framebuffer padrão (tela).
function E:fbo_unbind() lib.engine_fbo_unbind(self._e) end

-- Retorna handle de textura (unsigned int) do FBO.
function E:fbo_textura(fh)
    if not fh or fh == -1 then return 0 end
    return lib.engine_fbo_texture(self._e, fh)
end

-- Registra a textura do FBO como sprite reutilizável. Retorna sprite_id.
function E:fbo_sprite(fh)
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

-- ================================================================
-- SHADERS GLSL
-- ================================================================
-- Fluxo: sh=shader_criar(vert,frag) → shader_usar(sh) → desenhar()
--        → shader_nenhum()

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

-- Fragment: escala de cinza (luminância BT.601).
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

-- Fragment: inverte as cores (negativo fotográfico).
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

-- Fragment: aberração cromática — uniform vec2 "u_offset" (ex: vec2(0.003,0)).
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

-- Fragment: efeito CRT — scanlines + vinheta. Uniform vec2 "u_resolucao".
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

-- Compila e linka shader GLSL. Retorna ShaderHandle ou -1.
function E:shader_criar(vert_src, frag_src)
    return lib.engine_shader_create(self._e, vert_src, frag_src)
end

-- Libera o shader.
function E:shader_destruir(sh)
    if sh and sh ~= -1 then lib.engine_shader_destroy(self._e, sh) end
end

-- Ativa o shader para os próximos draw calls.
function E:shader_usar(sh)
    if sh and sh ~= -1 then lib.engine_shader_use(self._e, sh) end
end

-- Desativa shaders e volta ao pipeline padrão.
function E:shader_nenhum() lib.engine_shader_none(self._e) end

-- Uniforms por nome.
function E:shader_int(sh, nome, v)
    if sh and sh ~= -1 then lib.engine_shader_set_int(self._e, sh, nome, v) end
end
function E:shader_float(sh, nome, v)
    if sh and sh ~= -1 then lib.engine_shader_set_float(self._e, sh, nome, v) end
end
function E:shader_vec2(sh, nome, x, y)
    if sh and sh ~= -1 then lib.engine_shader_set_vec2(self._e, sh, nome, x, y) end
end
function E:shader_vec4(sh, nome, x, y, z, w)
    if sh and sh ~= -1 then lib.engine_shader_set_vec4(self._e, sh, nome, x, y, z, w) end
end

-- Atalhos de efeito: ativam/desativam shader pré-compilado.
function E:efeito_cinza(ativar)
    if ativar then
        if not self._sh_cinza then
            self._sh_cinza = self:shader_criar(E.VERT_PADRAO, E.FRAG_CINZA)
        end
        self:shader_usar(self._sh_cinza)
    else self:shader_nenhum() end
end

function E:efeito_negativo(ativar)
    if ativar then
        if not self._sh_neg then
            self._sh_neg = self:shader_criar(E.VERT_PADRAO, E.FRAG_NEGATIVO)
        end
        self:shader_usar(self._sh_neg)
    else self:shader_nenhum() end
end

function E:efeito_crt(ativar, rw, rh)
    if ativar then
        if not self._sh_crt then
            self._sh_crt = self:shader_criar(E.VERT_PADRAO, E.FRAG_CRT)
        end
        self:shader_usar(self._sh_crt)
        self:shader_vec2(self._sh_crt, "u_resolucao",
            rw or self._e.render_w, rh or self._e.render_h)
    else self:shader_nenhum() end
end

function E:efeito_aberracao(ativar, offset)
    if ativar then
        if not self._sh_aber then
            self._sh_aber = self:shader_criar(E.VERT_PADRAO, E.FRAG_ABERRACAO)
        end
        self:shader_usar(self._sh_aber)
        self:shader_vec2(self._sh_aber, "u_offset", offset or 0.003, 0.0)
    else self:shader_nenhum() end
end

-- ================================================================
-- SPATIAL GRID (particionamento para colisões eficientes)
-- ================================================================
-- Fluxo: sgrid_init() → sgrid_rebuild() → (loop) sgrid_primeira_colisao()
-- Objetos criados/movidos/destruídos atualizam o grid automaticamente.

-- Ativa o grid. cell_size=0 usa padrão (64px).
function E:sgrid_init(cell_size)
    lib.engine_sgrid_init(self._e, cell_size or 0)
end

-- Desativa e limpa o grid.
function E:sgrid_destruir() lib.engine_sgrid_destroy(self._e) end

-- Reinsere todos os objetos ativos no grid (use após criar muitos de uma vez).
function E:sgrid_rebuild() lib.engine_sgrid_rebuild(self._e) end

-- Insere objeto manualmente (criar_objeto já faz isso).
function E:sgrid_inserir(oid) lib.engine_sgrid_insert_object(self._e, oid) end

-- Remove objeto manualmente (remover_objeto já faz isso).
function E:sgrid_remover(oid) lib.engine_sgrid_remove_object(self._e, oid) end

-- Atualiza posição no grid (só necessário se modificar obj.x/y via FFI diretamente).
function E:sgrid_atualizar(oid) lib.engine_sgrid_update_object(self._e, oid) end

-- Retorna tabela de IDs candidatos a colidir com o retângulo. cap=máx resultados.
function E:sgrid_query_rect(x, y, w, h, cap)
    cap = cap or 64
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_query_rect(self._e, x, y, w, h, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- Candidatos a colidir com a hitbox do objeto oid.
function E:sgrid_query_objeto(oid, cap)
    cap = cap or 64
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_query_object(self._e, oid, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- Candidatos a colidir com o ponto (px, py).
function E:sgrid_query_ponto(px, py, cap)
    cap = cap or 32
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_query_point(self._e, px, py, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- Retorna oid do primeiro objeto que colide com oid (AABB exato). nil se nenhum.
function E:sgrid_primeira_colisao(oid)
    local r = lib.engine_sgrid_first_collision(self._e, oid)
    return r >= 0 and r or nil
end

-- Retorna tabela com todos os oids que colidem com oid (AABB exato).
function E:sgrid_todas_colisoes(oid, cap)
    cap = cap or 64
    local buf = ffi.new("int[?]", cap)
    local n   = lib.engine_sgrid_all_collisions(self._e, oid, buf, cap)
    local t = {}
    for i = 0, n-1 do t[i+1] = buf[i] end
    return t
end

-- true se o grid está ativo.
function E:sgrid_ativo() return self._e.sgrid.enabled ~= 0 end

-- Debug: retorna contagem de objetos na célula que contém (wx, wy).
function E:sgrid_debug_celula(wx, wy)
    if not self:sgrid_ativo() then return 0, -1 end
    local g   = self._e.sgrid
    local cs  = g.cell_size
    local col = math.max(0, math.min(g.cols-1, math.floor(wx/cs)))
    local row = math.max(0, math.min(g.rows-1, math.floor(wy/cs)))
    local idx = row * g.cols + col
    return g.cells[idx].count, idx
end

-- ================================================================
-- FOV (Field of View — Shadowcasting 2D)
-- ================================================================
-- Fluxo:
--   fov = v:fov_novo(cols, rows, raio, modo)   -- inicialização
--   v:fov_calcular(fov, player_col, player_row, function(col,row)
--       return mapa:bloqueia(col, row)           -- true = parede
--   end)
--   v:fov_sombra(fov, tile_w, tile_h, ox, oy)  -- após desenhar mundo, antes da UI
--
-- Modos: 0=BASIC (sem memória), 1=FOG_WAR (tiles visitados ficam semi-visíveis),
--        2=SMOOTH (falloff suave).
--
-- ATENÇÃO: fov.cpp deve estar compilado junto com Engine.cpp na libengine.so.
-- Exemplo de compilação:
--   g++ -shared -fPIC -o libengine.so src/Engine.cpp src/fov.cpp ...

-- Cria sessão de FOV. map_cols×map_rows = dimensões do mapa em tiles.
-- raio = tiles de visibilidade (padrão 8). modo = 0/1/2 (padrão 1).
function E:fov_novo(map_cols, map_rows, raio, modo)
    local obj = {}
    obj.map_cols = map_cols
    obj.map_rows = map_rows
    obj.raio     = raio or 8
    obj.modo     = modo or 1

    local n     = map_cols * map_rows
    obj._vis_n  = n
    obj._vis    = ffi.new("float[?]", n)
    ffi.fill(obj._vis, n * ffi.sizeof("float"), 0)

    obj._params             = ffi.new("FovParams")
    obj._params.map_cols    = map_cols
    obj._params.map_rows    = map_rows
    obj._params.radius      = obj.raio
    obj._params.mode        = obj.modo
    obj._params.vis         = obj._vis
    obj._params.is_blocking = nil
    obj._params.user_data   = nil

    return obj
end

-- Recalcula o campo de visão. is_parede(col,row) → true se o tile bloqueia.
-- Chame uma vez por frame após mover o personagem.
function E:fov_calcular(fov, col, row, is_parede)
    fov._params.origin_col = col
    fov._params.origin_row = row

    local cb = ffi.cast("FovBlockFn", function(c, r, _)
        return is_parede(c, r) and 1 or 0
    end)
    fov._params.is_blocking = cb
    fov._params.user_data   = nil

    C.engine_fov_compute(fov._params)   -- usa ffi.C (símbolo global da .so)

    cb:free()
end

-- Versão de alto desempenho: recebe FovBlockFn já convertida via ffi.cast.
-- Reutilize o mesmo cb entre frames para evitar alocação/liberação.
-- Libere com cb:free() ao final da fase.
function E:fov_calcular_cb(fov, col, row, c_callback, c_user_data)
    fov._params.origin_col  = col
    fov._params.origin_row  = row
    fov._params.is_blocking = c_callback
    fov._params.user_data   = c_user_data or nil
    C.engine_fov_compute(fov._params)
end

-- Altera o raio de visão. Afeta o próximo fov_calcular().
function E:fov_raio(fov, r)
    fov.raio = r
    fov._params.radius = r
end

-- Altera o modo (0/1/2). Afeta o próximo fov_calcular().
function E:fov_modo(fov, modo)
    fov.modo = modo
    fov._params.mode = modo
end

-- Zera o array vis[]. Use ao trocar de mapa para apagar fog-of-war acumulado.
function E:fov_reset(fov)
    ffi.fill(fov._vis, fov._vis_n * ffi.sizeof("float"), 0)
end

-- Desenha a camada de escuridão sobre o tilemap.
-- Chame APÓS desenhar o mundo e ANTES da UI/HUD.
-- offset_x/y = mesmo deslocamento usado no tilemap. r,g,b = cor da sombra (padrão 0,0,0).
function E:fov_sombra(fov, tile_w, tile_h, offset_x, offset_y, r, g, b)
    C.engine_fov_draw_shadow(self._e, fov._vis,
        fov.map_cols, fov.map_rows,
        tile_w, tile_h,
        offset_x or 0, offset_y or 0,
        r or 0, g or 0, b or 0)
end

-- true se o tile (col,row) está no cone de visão ATUAL.
function E:fov_visivel(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] >= 0.99
end

-- true se o tile já foi explorado ao menos uma vez (modo FOG_WAR).
function E:fov_explorado(fov, col, row)
    if col < 0 or col >= fov.map_cols then return false end
    if row < 0 or row >= fov.map_rows then return false end
    return fov._vis[row * fov.map_cols + col] > 0.0
end

-- Valor bruto de visibilidade do tile (0.0 = escuro, 1.0 = visível).
function E:fov_get(fov, col, row)
    if col < 0 or col >= fov.map_cols then return 0.0 end
    if row < 0 or row >= fov.map_rows then return 0.0 end
    return fov._vis[row * fov.map_cols + col]
end

-- Visualiza octantes e raio (somente em desenvolvimento).
function E:fov_debug(fov, col, row, tile_w, tile_h, offset_x, offset_y)
    C.engine_fov_draw_debug(self._e, col, row, fov.raio,
        tile_w, tile_h,
        offset_x or 0, offset_y or 0)
end

return E