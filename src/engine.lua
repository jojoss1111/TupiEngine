-- engine.lua

local ffi = require("engineffi")
local lib = ffi.load("./libengine.so")
local math = math

-- ----------------------------------------------------------------
-- Constantes exportadas
-- ----------------------------------------------------------------
local BOTAO_ESQUERDO = 0  -- botão esquerdo do mouse
local BOTAO_MEIO     = 1  -- botão do meio do mouse
local BOTAO_DIREITO  = 2  -- botão direito do mouse

-- ----------------------------------------------------------------
-- Metatype
-- ----------------------------------------------------------------
local Engine = {}
Engine.__index = Engine

-- Cria e inicializa uma nova instância da engine.
function Engine.nova(largura, altura, titulo, escala)
    local e  = ffi.new("Engine[1]")
    local ok = lib.engine_init(e[0], largura, altura, titulo, escala or 1)
    if ok == 0 then return nil end
    local self     = setmetatable({}, Engine)
    self._e        = e[0]
    self._ptr      = e
    -- Constantes de botão do mouse acessíveis via instância
    self.MOUSE_ESQ    = BOTAO_ESQUERDO
    self.MOUSE_MEIO   = BOTAO_MEIO
    self.MOUSE_DIR    = BOTAO_DIREITO
    return self
end

-- ================================================================
-- CICLO PRINCIPAL
-- ================================================================

-- Retorna true enquanto o jogo deve continuar rodando.
function Engine:rodando()
    return self._e.running ~= 0
end

-- Processa eventos de teclado, mouse e janela.
function Engine:processar_eventos()
    lib.engine_poll_events(self._e)
end

-- Limpa a tela com a cor de fundo definida.
function Engine:limpar()
    lib.engine_clear(self._e)
end

-- Desenha todos os objetos ativos na cena.
function Engine:desenhar()
    lib.engine_draw(self._e)
end

-- Força envio do batch de desenho para a GPU.
function Engine:enviar()
    lib.engine_flush(self._e)
end

-- Exibe o frame renderizado na janela (swap de buffers).
function Engine:apresentar()
    lib.engine_present(self._e)
end

-- Limita a taxa de quadros ao valor desejado (em FPS).
function Engine:limitar_fps(fps)
    lib.engine_cap_fps(self._e, fps or 60)
end

-- Atualiza câmera, partículas, animações e fade com o delta informado.
-- Passe 0 para usar o delta calculado internamente.
function Engine:atualizar(dt)
    lib.engine_update(self._e, dt or 0)
end

-- Libera todos os recursos e fecha a janela.
function Engine:destruir()
    lib.engine_destroy(self._e)
end

-- ================================================================
-- TEMPO
-- ================================================================

-- Retorna o tempo em segundos desde que a engine foi iniciada.
function Engine:tempo()
    return lib.engine_get_time(self._e)
end

-- Retorna a duração do último frame em segundos (delta time).
function Engine:delta()
    return lib.engine_get_delta(self._e)
end

-- ================================================================
-- JANELA / DISPLAY
-- ================================================================

-- Define a cor de fundo da tela (r, g, b de 0 a 255).
function Engine:cor_fundo(r, g, b)
    lib.engine_set_background(self._e, r, g, b)
end

-- Alterna entre modo janela e tela cheia pixel-perfect.
function Engine:alternar_tela_cheia()
    lib.engine_toggle_fullscreen(self._e)
end

-- Retorna largura e altura da área de renderização lógica.
function Engine:tamanho_render()
    return self._e.render_w, self._e.render_h
end

-- Retorna largura e altura da janela física em pixels.
function Engine:tamanho_janela()
    return self._e.win_w, self._e.win_h
end

-- Retorna true se a engine estiver em modo tela cheia.
function Engine:tela_cheia()
    return self._e.fullscreen ~= 0
end

-- ================================================================
-- SPRITES
-- ================================================================

-- Carrega um arquivo PNG como sprite e retorna seu ID.
function Engine:carregar_sprite(caminho)
    return lib.engine_load_sprite(self._e, caminho)
end

-- Carrega uma região retangular de um PNG como sprite e retorna o ID.
function Engine:carregar_sprite_regiao(caminho, x, y, w, h)
    return lib.engine_load_sprite_region(self._e, caminho, x, y, w, h)
end

-- ================================================================
-- OBJETOS
-- ================================================================

-- Adiciona um objeto simples com sprite e cor de tint; retorna o ID.
function Engine:adicionar_objeto(x, y, sprite_id, largura, altura, r, g, b)
    return lib.engine_add_object(self._e, x, y, sprite_id,
                                  largura or 0, altura or 0,
                                  r or 255, g or 255, b or 255)
end

-- Adiciona um objeto usando um tile de um tileset; retorna o ID.
function Engine:adicionar_objeto_tile(x, y, sprite_id, tile_x, tile_y, tile_w, tile_h)
    return lib.engine_add_tile_object(self._e, x, y, sprite_id,
                                       tile_x, tile_y, tile_w, tile_h)
end

-- Move um objeto por um deslocamento relativo (dx, dy).
function Engine:mover_objeto(oid, dx, dy)
    lib.engine_move_object(self._e, oid, dx, dy)
end

-- Define a posição absoluta de um objeto.
function Engine:posicionar_objeto(oid, x, y)
    lib.engine_set_object_pos(self._e, oid, x, y)
end

-- Troca o sprite de um objeto em tempo real.
function Engine:trocar_sprite(oid, sprite_id)
    lib.engine_set_object_sprite(self._e, oid, sprite_id)
end

-- Retorna a posição atual (x, y) de um objeto.

function Engine:posicao_objeto(oid)
    local ox = ffi.new("int[1]")
    local oy = ffi.new("int[1]")
    lib.engine_get_object_pos(self._e, oid, ox, oy)
    return ox[0], oy[0]
end

-- Define qual tile do tileset um objeto deve exibir.
function Engine:definir_tile(oid, tile_x, tile_y)
    lib.engine_set_object_tile(self._e, oid, tile_x, tile_y)
end

-- Define o espelhamento horizontal e/ou vertical de um objeto.
function Engine:espelhar_objeto(oid, flip_h, flip_v)
    lib.engine_set_object_flip(self._e, oid,
                                flip_h and 1 or 0,
                                flip_v and 1 or 0)
end

-- Define a escala (sx, sy) de um objeto sem alterar o sprite original.
function Engine:escalar_objeto(oid, sx, sy)
    lib.engine_set_object_scale(self._e, oid, sx or 1.0, sy or 1.0)
end

-- Define a rotação (em graus, sentido horário) de um objeto.
function Engine:rotacionar_objeto(oid, graus)
    lib.engine_set_object_rotation(self._e, oid, graus or 0.0)
end

-- Define a transparência de um objeto (0.0 = invisível, 1.0 = opaco).
function Engine:transparencia_objeto(oid, alpha)
    lib.engine_set_object_alpha(self._e, oid, alpha or 1.0)
end

-- Define a camada (layer) e a ordem Z de um objeto para controle de profundidade.
function Engine:camada_objeto(oid, layer, z_order)
    lib.engine_set_object_layer(self._e, oid, layer or 0, z_order or 0)
end

-- Define uma hitbox customizada com offset e tamanho independentes do sprite.
function Engine:hitbox_objeto(oid, offset_x, offset_y, largura, altura)
    lib.engine_set_object_hitbox(self._e, oid,
                                  offset_x or 0, offset_y or 0,
                                  largura, altura)
end

-- Remove (desativa) um objeto da cena.
function Engine:remover_objeto(oid)
    lib.engine_remove_object(self._e, oid)
end

-- ================================================================
-- COLISÃO
-- ================================================================

-- Verifica colisão AABB entre dois objetos pelo ID; retorna true se colidem.
function Engine:colidem(oid1, oid2)
    return lib.engine_check_collision(self._e, oid1, oid2) ~= 0
end

-- Verifica se um objeto (pelo ID) colide com um retângulo de coordenadas.
function Engine:objeto_colide_rect(oid, rx, ry, rw, rh)
    return lib.engine_check_collision_rect(self._e, oid, rx, ry, rw, rh) ~= 0
end

-- Verifica se um ponto (px, py) está dentro da hitbox de um objeto.
function Engine:objeto_contem_ponto(oid, px, py)
    return lib.engine_check_collision_point(self._e, oid, px, py) ~= 0
end

-- ----------------------------------------------------------------
-- Colisões puras por coordenadas (sem precisar de objetos)
-- ----------------------------------------------------------------

-- Verifica colisão AABB entre dois retângulos.
-- Parâmetros: x,y,w,h do primeiro rect e x,y,w,h do segundo rect.
-- Retorna true se colidem.
function Engine:colide_rect(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw
       and ax + aw > bx
       and ay < by + bh
       and ay + ah > by
end

-- Verifica se um ponto (px, py) está dentro de um retângulo.
function Engine:ponto_em_rect(px, py, rx, ry, rw, rh)
    return px >= rx and px < rx + rw
       and py >= ry and py < ry + rh
end

-- Verifica colisão entre dois círculos.
-- cx1,cy1,r1 = centro e raio do primeiro; cx2,cy2,r2 = do segundo.
function Engine:colide_circulo(cx1, cy1, r1, cx2, cy2, r2)
    local dx = cx1 - cx2
    local dy = cy1 - cy2
    local dist2 = dx*dx + dy*dy
    local rsum  = r1 + r2
    return dist2 < rsum * rsum
end

-- Verifica se um ponto está dentro de um círculo.
function Engine:ponto_em_circulo(px, py, cx, cy, raio)
    local dx = px - cx
    local dy = py - cy
    return dx*dx + dy*dy < raio * raio
end

-- Retorna o vetor de sobreposição (overlap_x, overlap_y) entre dois rects.
-- Útil para resolver a colisão empurrando o objeto para fora.
-- Retorna nil se não há colisão.
function Engine:sobreposicao_rect(ax, ay, aw, ah, bx, by, bw, bh)
    local ox = math.min(ax + aw, bx + bw) - math.max(ax, bx)
    local oy = math.min(ay + ah, by + bh) - math.max(ay, by)
    if ox <= 0 or oy <= 0 then return nil end
    -- Retorna o eixo de menor sobreposição com sinal de direção
    if ox < oy then
        local sinal = ax + aw/2 < bx + bw/2 and -1 or 1
        return ox * sinal, 0
    else
        local sinal = ay + ah/2 < by + bh/2 and -1 or 1
        return 0, oy * sinal
    end
end

-- ================================================================
-- CÂMERA
-- ================================================================

-- Posiciona a câmera em coordenadas de mundo absolutas.
function Engine:posicionar_camera(x, y)
    lib.engine_camera_set(self._e, x, y)
end

-- Move a câmera por um deslocamento relativo.
function Engine:mover_camera(dx, dy)
    lib.engine_camera_move(self._e, dx, dy)
end

-- Define o zoom da câmera (1.0 = normal, 2.0 = 2× mais perto).
function Engine:zoom_camera(zoom)
    lib.engine_camera_zoom(self._e, zoom or 1.0)
end

-- Faz a câmera seguir um objeto com interpolação suave (lerp 0..1).
function Engine:camera_seguir(oid, suavidade)
    lib.engine_camera_follow(self._e, oid, suavidade or 0.1)
end

-- Ativa um efeito de tremor de câmera com intensidade e duração em segundos.
function Engine:tremor_camera(intensidade, duracao)
    lib.engine_camera_shake(self._e, intensidade or 4.0, duracao or 0.3)
end

-- Ativa ou desativa a câmera (0 = sem câmera, comportamento estático).
function Engine:habilitar_camera(ativo)
    lib.engine_camera_enable(self._e, ativo and 1 or 0)
end

-- Converte coordenadas de mundo para coordenadas de tela.
function Engine:mundo_para_tela(wx, wy)
    local sx = ffi.new("float[1]")
    local sy = ffi.new("float[1]")
    lib.engine_world_to_screen(self._e, wx, wy, sx, sy)
    return sx[0], sy[0]
end

-- Converte coordenadas de tela para coordenadas de mundo.
function Engine:tela_para_mundo(sx, sy)
    local wx = ffi.new("float[1]")
    local wy = ffi.new("float[1]")
    lib.engine_screen_to_world(self._e, sx, sy, wx, wy)
    return wx[0], wy[0]
end

-- ================================================================
-- PARTÍCULAS
-- ================================================================

-- Cria um emissor de partículas a partir de uma tabela de configuração.
-- Retorna o ID do emissor. Campos da tabela:
--   x, y, vx_min, vx_max, vy_min, vy_max, ax, ay,
--   life_min, life_max, size_start, size_end,
--   r0,g0,b0,a0 (cor inicial), r1,g1,b1,a1 (cor final),
--   sprite_id (-1 = rect), rate (0 = só burst), max_particles
function Engine:criar_emissor(cfg)
    local em = ffi.new("ParticleEmitter")
    em.x          = cfg.x          or 0
    em.y          = cfg.y          or 0
    em.vx_min     = cfg.vx_min     or -20
    em.vx_max     = cfg.vx_max     or  20
    em.vy_min     = cfg.vy_min     or -50
    em.vy_max     = cfg.vy_max     or -20
    em.ax         = cfg.ax         or  0
    em.ay         = cfg.ay         or  80   -- gravidade padrão
    em.life_min   = cfg.life_min   or  0.5
    em.life_max   = cfg.life_max   or  1.5
    em.size_start = cfg.size_start or  4
    em.size_end   = cfg.size_end   or  0
    em.r0 = cfg.r0 or 1; em.g0 = cfg.g0 or 1; em.b0 = cfg.b0 or 0.2; em.a0 = cfg.a0 or 1
    em.r1 = cfg.r1 or 1; em.g1 = cfg.g1 or 0; em.b1 = cfg.b1 or 0;   em.a1 = cfg.a1 or 0
    em.sprite_id    = cfg.sprite_id    or -1
    em.rate         = cfg.rate         or  0
    em.max_particles= cfg.max_particles or 100
    return lib.engine_emitter_add(self._e, em)
end

-- Move o ponto de emissão de um emissor existente.
function Engine:posicionar_emissor(eid, x, y)
    lib.engine_emitter_set_pos(self._e, eid, x, y)
end

-- Emite uma explosão de N partículas de uma vez (burst).
function Engine:explodir_particulas(eid, quantidade)
    lib.engine_emitter_burst(self._e, eid, quantidade or 20)
end

-- Remove um emissor da cena.
function Engine:remover_emissor(eid)
    lib.engine_emitter_remove(self._e, eid)
end

-- Desenha todas as partículas ativas.
function Engine:desenhar_particulas()
    lib.engine_particles_draw(self._e)
end

-- ================================================================
-- ANIMAÇÃO
-- ================================================================

-- Cria um animador de sprites; retorna o ID.
-- sprite_ids: tabela com os IDs dos frames
-- fps: velocidade da animação
-- loop: true = repete, false = toca uma vez
-- objeto_id: objeto controlado (-1 = nenhum)
function Engine:criar_animacao(sprite_ids, fps, loop, objeto_id)
    local n    = #sprite_ids
    local carr = ffi.new("int[?]", n)
    for i = 1, n do carr[i-1] = sprite_ids[i] end
    return lib.engine_animator_add(self._e, carr, n,
                                    fps or 10,
                                    loop ~= false and 1 or 0,
                                    objeto_id or -1)
end

-- Inicia ou reinicia a reprodução de uma animação.
function Engine:tocar_animacao(aid)
    lib.engine_animator_play(self._e, aid)
end

-- Para a reprodução de uma animação no frame atual.
function Engine:parar_animacao(aid)
    lib.engine_animator_stop(self._e, aid)
end

-- Reinicia uma animação do primeiro frame.
function Engine:reiniciar_animacao(aid)
    lib.engine_animator_reset(self._e, aid)
end

-- Retorna true se uma animação one-shot terminou.
function Engine:animacao_terminou(aid)
    return lib.engine_animator_finished(self._e, aid) ~= 0
end

-- ================================================================
-- FADE
-- ================================================================

-- Inicia um fade para a transparência alvo (0=sem fade, 1=tela preta).
-- velocidade em unidades/segundo; r,g,b = cor do fade.
function Engine:fade(alvo, velocidade, r, g, b)
    lib.engine_fade_to(self._e,
                        alvo or 1.0,
                        velocidade or 1.0,
                        r or 0, g or 0, b or 0)
end

-- Desenha o overlay de fade sobre a tela (chame após engine:desenhar()).
function Engine:desenhar_fade()
    lib.engine_fade_draw(self._e)
end

-- Retorna true quando o fade chegou ao valor alvo.
function Engine:fade_completo()
    return lib.engine_fade_done(self._e) ~= 0
end

-- ================================================================
-- DESENHO DE PRIMITIVAS
-- ================================================================

-- Desenha um retângulo sólido colorido.
function Engine:desenhar_retangulo(x, y, w, h, r, g, b)
    lib.engine_draw_rect(self._e, x, y, w, h, r, g, b)
end

-- Desenha apenas o contorno de um retângulo com a espessura dada.
function Engine:desenhar_contorno(x, y, w, h, r, g, b, espessura)
    lib.engine_draw_rect_outline(self._e, x, y, w, h, r, g, b, espessura or 1)
end

-- Desenha uma linha entre dois pontos com a espessura dada.
function Engine:desenhar_linha(x0, y0, x1, y1, r, g, b, espessura)
    lib.engine_draw_line(self._e, x0, y0, x1, y1, r, g, b, espessura or 1)
end

-- Desenha um círculo (preenchido ou contorno) no ponto central dado.
function Engine:desenhar_circulo(cx, cy, raio, r, g, b, preenchido)
    lib.engine_draw_circle(self._e, cx, cy, raio, r, g, b,
                            preenchido ~= false and 1 or 0)
end

-- Desenha um retângulo semitransparente (overlay) com alpha 0..1.
function Engine:desenhar_overlay(x, y, w, h, r, g, b, alpha)
    lib.engine_draw_overlay(self._e, x, y, w, h, r, g, b, alpha or 0.5)
end

-- ================================================================
-- EFEITOS ESPECIAIS
-- ================================================================

-- Desenha efeito de chuva com gotas passadas como tabela {{x,y}, ...}.
function Engine:desenhar_chuva(larg, alt, frame, gotas, gota_w, gota_h)
    local n  = #gotas
    local gx = ffi.new("int[?]", n)
    local gy = ffi.new("int[?]", n)
    for i, g in ipairs(gotas) do gx[i-1] = g[1]; gy[i-1] = g[2] end
    lib.engine_draw_rain(self._e, larg, alt, frame,
                          gx, gy, n, gota_w or 1, gota_h or 4)
end

-- Desenha um overlay escuro para simular noite (intensidade 0..1).
function Engine:desenhar_noite(larg, alt, intensidade, offset)
    lib.engine_draw_night(self._e, larg, alt, intensidade or 0.5, offset or 0)
end

-- ================================================================
-- TILEMAP
-- ================================================================

-- Desenha um tilemap a partir de uma tabela linear (linha a linha).
function Engine:desenhar_tilemap(tilemap, linhas, colunas,
                                   sprite_id, tile_w, tile_h,
                                   offset_x, offset_y)
    local n    = linhas * colunas
    local carr = ffi.new("int[?]", n)
    for i = 1, n do carr[i-1] = tilemap[i] or 0 end
    lib.engine_draw_tilemap(self._e, carr, linhas, colunas,
                             sprite_id, tile_w, tile_h,
                             offset_x or 0, offset_y or 0)
end

-- ================================================================
-- SPRITE PARTS
-- ================================================================

-- Desenha uma parte de um sprite (recorte retangular) na posição dada.
function Engine:desenhar_parte_sprite(sprite_id, x, y, sx, sy, sw, sh)
    lib.engine_draw_sprite_part(self._e, sprite_id, x, y, sx, sy, sw, sh)
end

-- Desenha uma parte de sprite com escala, rotação, alpha e flip.
function Engine:desenhar_parte_sprite_ex(sprite_id, x, y, sx, sy, sw, sh,
                                          escala_x, escala_y, rotacao, alpha,
                                          flip_h, flip_v)
    lib.engine_draw_sprite_part_ex(self._e, sprite_id, x, y, sx, sy, sw, sh,
                                    escala_x or 1, escala_y or 1,
                                    rotacao or 0, alpha or 1,
                                    flip_h and 1 or 0, flip_v and 1 or 0)
end

-- Desenha uma parte de sprite com cores invertidas (efeito dano).
function Engine:desenhar_parte_sprite_invertido(sprite_id, x, y, sx, sy, sw, sh)
    lib.engine_draw_sprite_part_inverted(self._e, sprite_id, x, y, sx, sy, sw, sh)
end

-- ================================================================
-- TEXTO / UI
-- ================================================================

-- Desenha texto usando uma fonte bitmap (spritesheet de caracteres).
function Engine:desenhar_texto(x, y, texto, font_sid, font_w, font_h,
                                chars_por_linha, offset_ascii, espacamento)
    lib.engine_draw_text(self._e, x, y, texto,
                          font_sid, font_w, font_h,
                          chars_por_linha or 16,
                          offset_ascii    or 32,
                          espacamento     or 0)
end

-- Desenha uma caixa decorada usando um tileset de bordas 3×3.
function Engine:desenhar_caixa(x, y, w, h, box_sid, tile_w, tile_h)
    lib.engine_draw_box(self._e, x, y, w, h, box_sid, tile_w, tile_h)
end

-- Desenha uma caixa de texto com título, conteúdo e quebra automática de linha.
function Engine:desenhar_caixa_texto(x, y, w, h, titulo, conteudo,
                                      box_sid, box_tw, box_th,
                                      font_sid, font_w, font_h,
                                      chars_por_linha, offset_ascii, espacamento)
    lib.engine_draw_text_box(self._e, x, y, w, h, titulo, conteudo,
                              box_sid, box_tw, box_th,
                              font_sid, font_w, font_h,
                              chars_por_linha or 16,
                              offset_ascii    or 32,
                              espacamento     or 0)
end

-- ================================================================
-- INPUT — TECLADO
-- ================================================================

function Engine:tecla_pressionada(tecla)
    return lib.engine_key_down(self._e, tecla:lower()) ~= 0
end

function Engine:tecla_apertou(tecla)
    return lib.engine_key_pressed(self._e, tecla:lower()) ~= 0
end

function Engine:tecla_soltou(tecla)
    return lib.engine_key_released(self._e, tecla:lower()) ~= 0
end

-- ================================================================
-- INPUT — MOUSE
-- ================================================================

-- Retorna true enquanto o botão do mouse estiver pressionado.
-- botao: Engine.MOUSE_ESQ, MOUSE_MEIO ou MOUSE_DIR
function Engine:mouse_pressionado(botao)
    return lib.engine_mouse_down(self._e, botao) ~= 0
end

-- Retorna true apenas no frame em que o botão foi pressionado.
function Engine:mouse_apertou(botao)
    return lib.engine_mouse_pressed(self._e, botao) ~= 0
end

-- Retorna true apenas no frame em que o botão foi solto.
function Engine:mouse_soltou(botao)
    return lib.engine_mouse_released(self._e, botao) ~= 0
end

-- Retorna a posição (x, y) do cursor em coordenadas lógicas.
function Engine:posicao_mouse()
    local mx = ffi.new("int[1]")
    local my = ffi.new("int[1]")
    lib.engine_mouse_pos(self._e, mx, my)
    return mx[0], my[0]
end

-- Retorna +1 se o scroll foi para cima, -1 para baixo, 0 se parado.
function Engine:scroll_mouse()
    return lib.engine_mouse_scroll(self._e)
end

-- ================================================================
-- ÁUDIO
-- ================================================================

-- Inicializa o subsistema de áudio. Chame após Engine.nova().
-- Retorna true em caso de sucesso, false se não houver dispositivo de saída.
function Engine:iniciar_audio()
    return lib.engine_audio_init(self._e) ~= 0
end

-- Processa callbacks internos do áudio (retorno automático de músicas).
-- OBRIGATÓRIO: chame uma vez por frame no loop principal.
function Engine:atualizar_audio()
    lib.engine_audio_update(self._e)
end

-- Libera todos os recursos de áudio. Chame antes de engine:destruir().
function Engine:destruir_audio()
    lib.engine_audio_destroy(self._e)
end

--[[
  Inicia a reprodução de um arquivo de áudio em segundo plano.

  Parâmetros:
    arquivo          : caminho do arquivo MP3 / WAV / OGG / FLAC
    loop             : true = repete indefinidamente, false = toca uma vez
    intensidade      : volume  0.0 (mudo) .. 1.0 (máximo)   [padrão 1.0]
    velocidade       : pitch   0.5 (lento) .. 2.0 (rápido)  [padrão 1.0]
    musica_anterior  : handle de uma faixa PAUSADA a retomar
                       automaticamente quando ESTA terminar.
                       Só tem efeito quando loop=false.
                       Passe nil para sem retorno automático. [padrão nil]

  Retorna:
    handle (número inteiro) que identifica a faixa,
    ou -1 (ENGINE_AUDIO_INVALID) em caso de erro.
]]
function Engine:tocar_musica(arquivo, loop, intensidade, velocidade, musica_anterior)
    local l  = loop            and 1   or 0
    local iv = intensidade     or 1.0
    local vl = velocidade      or 1.0
    local ma = musica_anterior or -1   -- ENGINE_AUDIO_INVALID
    return lib.engine_audio_play(self._e, arquivo, l, iv, vl, ma)
end

-- Pausa uma faixa em reprodução. Pode ser retomada com retomar_musica().
function Engine:interromper_musica(handle)
    if handle and handle ~= -1 then
        lib.engine_audio_pause(self._e, handle)
    end
end

-- Retoma uma faixa que foi pausada com interromper_musica().
function Engine:retomar_musica(handle)
    if handle and handle ~= -1 then
        lib.engine_audio_resume(self._e, handle)
    end
end

-- Para e libera completamente uma faixa. O handle não pode mais ser usado.
function Engine:parar_musica(handle)
    if handle and handle ~= -1 then
        lib.engine_audio_stop(self._e, handle)
    end
end

-- Ajusta o volume de uma faixa em tempo real (0.0 .. 1.0).
function Engine:volume_musica(handle, intensidade)
    if handle and handle ~= -1 then
        lib.engine_audio_volume(self._e, handle, intensidade or 1.0)
    end
end

-- Ajusta o pitch / velocidade de uma faixa em tempo real (0.5 .. 2.0).
function Engine:velocidade_musica(handle, velocidade)
    if handle and handle ~= -1 then
        lib.engine_audio_pitch(self._e, handle, velocidade or 1.0)
    end
end

-- Retorna true se uma faixa (sem loop) terminou de tocar.
function Engine:musica_terminou(handle)
    if not handle or handle == -1 then return true end
    return lib.engine_audio_done(self._e, handle) ~= 0
end

-- Constante: handle inválido (retornado em caso de erro ou faixa não existente).
Engine.AUDIO_INVALIDO = -1

return Engine