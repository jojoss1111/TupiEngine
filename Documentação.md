# TupiEngine — Referência da API

> Documentação de referência para a interface de scripting em Lua da TupiEngine.
> Pressupõe conhecimento básico de Lua: variáveis, funções, `if` e `while`.

---

## Sumário

1. [Inicialização](#inicialização)
2. [Loop Principal](#loop-principal)
3. [Janela](#janela)
4. [Sprites](#sprites)
5. [Objetos](#objetos)
6. [Colisão](#colisão)
7. [Câmera](#câmera)
8. [Partículas](#partículas)
9. [Animação](#animação)
10. [Fade](#fade)
11. [Desenho Direto](#desenho-direto)
12. [Texto e UI](#texto-e-ui)
13. [Efeitos Visuais](#efeitos-visuais)
14. [Input — Teclado](#input--teclado)
15. [Input — Mouse](#input--mouse)
16. [Áudio](#áudio)
17. [Shaders](#shaders)
18. [FBO (Framebuffer)](#fbo-framebuffer)
19. [FOV — Campo de Visão](#fov--campo-de-visão)
20. [Grade Espacial](#grade-espacial)
21. [Sistema de Mapas](#sistema-de-mapas)
22. [Exemplo Completo](#exemplo-completo)

---

## Inicialização

A engine é carregada com `require("engine")`. A função `E.nova()` cria a janela e retorna o objeto principal do jogo. Todas as funções subsequentes são chamadas sobre esse objeto usando a sintaxe de método do Lua (`:`).

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)
--                  ^largura ^altura  ^título  ^escala
```

O parâmetro `escala` multiplica o tamanho da janela sem alterar a resolução interna de renderização. Com `escala = 2`, uma tela de 320×240 é exibida como uma janela de 640×480, preservando a estética de pixel art.

A sintaxe `jogo:funcao()` é equivalente a `jogo.funcao(jogo)`. O uso de `:` é o padrão do Lua para chamadas orientadas a objeto.

---

## Loop Principal

Todo jogo opera em um loop de atualização contínuo. Cada iteração do loop corresponde a um frame. A ordem de chamada das funções dentro do loop é obrigatória.

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)

jogo:fundo(30, 30, 40)

while jogo:rodando() do
    jogo:eventos()    -- 1. processa entrada e eventos de janela
    jogo:atualizar()  -- 2. atualiza câmera, animações, partículas e áudio
    jogo:limpar()     -- 3. limpa o buffer com a cor de fundo

    -- 4. lógica do jogo (movimento, colisões, etc.)

    jogo:desenhar()   -- 5. submete objetos ao renderizador
    jogo:apresentar() -- 6. envia o frame para a tela
    jogo:fps(60)      -- 7. limita a taxa de quadros
end

jogo:destruir()
```

`jogo:limpar()` deve sempre ser chamado antes de `jogo:desenhar()`. Inverter a ordem apaga o conteúdo recém-renderizado antes de exibi-lo.

### Delta Time

`jogo:delta()` retorna a duração do último frame em segundos (tipicamente `~0.016` a 60 fps). Multiplicar a velocidade de movimentação pelo delta time garante que o comportamento seja independente da taxa de quadros:

```lua
local velocidade = 100  -- pixels por segundo

-- Sem delta time (velocidade varia com o FPS):
jogo:mover(heroi, 3, 0)

-- Com delta time (velocidade constante):
jogo:mover(heroi, velocidade * jogo:delta(), 0)
```

### Referência

| Função | Parâmetros | Retorno |
|---|---|---|
| `E.nova(largura, altura, titulo, escala)` | Dimensões, título, fator de escala | objeto do jogo |
| `jogo:rodando()` | — | `true` enquanto a janela estiver aberta |
| `jogo:eventos()` | — | — |
| `jogo:atualizar()` | — | — |
| `jogo:limpar()` | — | — |
| `jogo:desenhar()` | — | — |
| `jogo:apresentar()` | — | — |
| `jogo:fps(alvo)` | Taxa de quadros desejada | — |
| `jogo:destruir()` | — | — |
| `jogo:tempo()` | — | segundos desde o início |
| `jogo:delta()` | — | duração do último frame em segundos |

---

## Janela

```lua
-- Define a cor de fundo em RGB (0–255)
jogo:fundo(0,   0,   0  )  -- preto
jogo:fundo(135, 206, 235)  -- azul

-- Alterna entre modo janela e tela cheia
jogo:tela_cheia()

-- Retorna as dimensões lógicas da área de renderização
local largura, altura = jogo:tamanho()
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fundo(r, g, b)` | Componentes RGB (0–255) | — |
| `jogo:tela_cheia()` | — | — |
| `jogo:tamanho()` | — | `largura, altura` |

---

## Sprites

Um sprite é uma textura PNG carregada na GPU. A função de carregamento retorna um identificador numérico (`sprite_id`) utilizado em todas as operações posteriores.

```lua
-- Carrega uma imagem PNG completa
local sid_heroi = jogo:carregar_sprite("imagens/heroi.png")

-- Carrega uma região de uma spritesheet
-- parâmetros: arquivo, x, y, largura, altura (em pixels na imagem fonte)
local sid_frame0 = jogo:carregar_regiao("sheet.png",  0, 0, 32, 32)
local sid_frame1 = jogo:carregar_regiao("sheet.png", 32, 0, 32, 32)
```

Em caso de falha no carregamento, as funções retornam `-1`. Recomenda-se verificar o retorno:

```lua
local sid = jogo:carregar_sprite("heroi.png")
if sid == -1 then
    error("Arquivo 'heroi.png' não encontrado.")
end
```

Uma spritesheet é uma imagem única contendo múltiplos sprites organizados em grade. Carregar uma única imagem e recortar regiões é mais eficiente do que carregar múltiplos arquivos separados.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:carregar_sprite(caminho)` | Caminho do arquivo PNG | `sprite_id` ou `-1` |
| `jogo:carregar_regiao(caminho, x, y, w, h)` | Arquivo e recorte | `sprite_id` ou `-1` |

---

## Objetos

Objetos são as entidades visíveis da cena. Cada objeto criado recebe um identificador numérico (`object_id`) utilizado para controle posterior.

### Criação e Remoção

```lua
-- Objeto com sprite completo
local heroi   = jogo:criar_objeto(100, 200, sid_heroi,   32, 32)

-- Objeto usando um tile de uma spritesheet
-- parâmetros extras: coluna e linha do tile, largura e altura do tile
local moeda   = jogo:criar_objeto_tile(64,  128, sid_tiles, 3, 0, 16, 16)

-- Remoção da cena
jogo:remover_objeto(inimigo)
```

### Posição e Movimento

O eixo Y cresce para baixo. A origem (0, 0) está no canto superior esquerdo da tela.

```lua
-- Deslocamento relativo à posição atual
jogo:mover(heroi,  2,  0)   --  2px para a direita
jogo:mover(heroi, -2,  0)   --  2px para a esquerda
jogo:mover(heroi,  0, -3)   --  3px para cima
jogo:mover(heroi,  0,  3)   --  3px para baixo

-- Posicionamento absoluto
jogo:posicionar(heroi, 50, 100)

-- Leitura da posição atual
local px, py = jogo:posicao(heroi)
```

### Aparência

```lua
-- Troca o sprite associado ao objeto
jogo:objeto_sprite(heroi, sid_heroi_correndo)

-- Altera qual tile da spritesheet o objeto exibe
jogo:objeto_tile(heroi, 2, 1)

-- Espelhamento horizontal e vertical
jogo:espelhar(heroi, true,  false)  -- espelha horizontalmente
jogo:espelhar(heroi, false, false)  -- restaura orientação original

-- Escala (1.0 = tamanho original)
jogo:escala(heroi, 2.0, 2.0)

-- Rotação em graus, sentido horário, pivô no centro
jogo:rotacao(heroi, 45)

-- Transparência (0.0 = invisível, 1.0 = opaco)
jogo:transparencia(heroi, 0.5)

-- Ordem de renderização (valores maiores aparecem à frente)
jogo:camada(heroi,  2, 0)
jogo:camada(fundo,  0, 0)
```

### Hitbox

A hitbox é o retângulo usado para detecção de colisão. Por padrão, cobre todo o sprite. É recomendável definir uma hitbox menor quando o sprite contém bordas transparentes.

```lua
-- parâmetros: object_id, offset_x, offset_y, largura, altura
-- offset é relativo ao canto superior esquerdo do sprite
jogo:hitbox(heroi, 6, 2, 20, 28)
```

### Referência

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_objeto(x, y, sid, w, h)` | Posição, sprite, dimensões | `object_id` |
| `jogo:criar_objeto_tile(x, y, sid, tx, ty, tw, th)` | Posição, sprite, tile e dimensões | `object_id` |
| `jogo:remover_objeto(oid)` | ID do objeto | — |
| `jogo:mover(oid, dx, dy)` | ID, deslocamento | — |
| `jogo:posicionar(oid, x, y)` | ID, coordenadas absolutas | — |
| `jogo:posicao(oid)` | ID | `x, y` |
| `jogo:objeto_sprite(oid, sid)` | ID, sprite_id | — |
| `jogo:objeto_tile(oid, col, lin)` | ID, coluna, linha | — |
| `jogo:espelhar(oid, h, v)` | ID, bool, bool | — |
| `jogo:escala(oid, sx, sy)` | ID, fatores de escala | — |
| `jogo:rotacao(oid, graus)` | ID, ângulo | — |
| `jogo:transparencia(oid, alpha)` | ID, valor 0–1 | — |
| `jogo:camada(oid, layer, z)` | ID, camada, profundidade | — |
| `jogo:hitbox(oid, ox, oy, w, h)` | ID, offset, dimensões | — |

---

## Colisão

A detecção de colisão opera sobre as hitboxes dos objetos (ou sobre o bounding box do sprite quando nenhuma hitbox foi definida).

```lua
-- Colisão entre dois objetos
if jogo:colidem(heroi, inimigo) then
    -- tratar colisão
end

-- Colisão com retângulo fixo (plataformas, paredes estáticas)
if jogo:colide_ret(heroi, 0, 230, 320, 10) then
    -- herói tocou o chão
end

-- Colisão com ponto (útil para verificar cursor sobre objeto)
local mx, my = jogo:mouse_pos()
if jogo:colide_ponto(botao, mx, my) then
    -- cursor está sobre o botão
end
```

Para cenas com dezenas ou centenas de objetos, consulte a seção [Grade Espacial](#grade-espacial) para uma alternativa de maior desempenho.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:colidem(oid1, oid2)` | Dois object_ids | `true` ou `false` |
| `jogo:colide_ret(oid, rx, ry, rw, rh)` | Objeto + retângulo | `true` ou `false` |
| `jogo:colide_ponto(oid, px, py)` | Objeto + ponto | `true` ou `false` |

---

## Câmera

A câmera define a região do mundo visível na tela. Por padrão, permanece em (0, 0).

```lua
-- Posicionamento absoluto no espaço do mundo
jogo:cam_pos(500, 300)

-- Interpolação suave em direção a um objeto
-- lerp: 0.0 = parada, 1.0 = instantânea
jogo:cam_seguir(heroi, 0.1)

-- Fator de zoom (1.0 = normal)
jogo:cam_zoom(1.5)

-- Efeito de tremor
-- parâmetros: intensidade em pixels, duração em segundos
jogo:cam_tremor(4, 0.2)

-- Conversão de coordenadas
local sx, sy = jogo:mundo_para_tela(heroi_x, heroi_y)
local wx, wy = jogo:tela_para_mundo(mouse_x, mouse_y)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:cam_pos(x, y)` | Posição no mundo | — |
| `jogo:cam_seguir(oid, lerp)` | Objeto alvo, suavidade 0–1 | — |
| `jogo:cam_zoom(z)` | Fator de zoom | — |
| `jogo:cam_tremor(intensidade, duracao)` | Pixels, segundos | — |
| `jogo:mundo_para_tela(wx, wy)` | Posição no mundo | `sx, sy` |
| `jogo:tela_para_mundo(sx, sy)` | Posição na tela | `wx, wy` |

---

## Partículas

O sistema de partículas permite criar efeitos visuais como fogo, fumaça e explosões através de emissores configuráveis.

```lua
-- Criação de um emissor (antes do loop)
local fagulhas = jogo:criar_emissor({
    x = 160, y = 120,

    -- Intervalo de velocidade inicial (vx_min, vx_max, vy_min, vy_max)
    vel  = {-40, 40, -80, -20},

    -- Aceleração gravitacional (gx, gy) em pixels/s²
    grav = {0, 120},

    -- Tempo de vida em segundos (mínimo, máximo)
    vida = {0.3, 1.0},

    -- Tamanho inicial e final em pixels
    tamanho = {5, 0},

    -- Cor inicial e final em RGBA normalizado (0.0–1.0)
    cor       = {1.0, 0.5, 0.0, 1.0},
    cor_final = {1.0, 0.0, 0.0, 0.0},

    rate = 0,    -- emissão contínua em partículas/segundo (0 = somente burst)
    max  = 100,  -- limite de partículas simultâneas
})

-- Emissão instantânea de N partículas
jogo:burst(fagulhas, 30)

-- Reposicionar o emissor
local px, py = jogo:posicao(heroi)
jogo:emissor_pos(fagulhas, px, py)

-- Remoção do emissor
jogo:remover_emissor(fagulhas)
```

As partículas são renderizadas automaticamente por `jogo:desenhar()`. As cores utilizam valores normalizados no intervalo `0.0–1.0` (divida valores RGB por 255 para converter).

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_emissor(cfg)` | Tabela de configuração | `emitter_id` |
| `jogo:burst(eid, n)` | ID do emissor, quantidade | — |
| `jogo:emissor_pos(eid, x, y)` | ID, nova posição | — |
| `jogo:remover_emissor(eid)` | ID do emissor | — |

---

## Animação

O sistema de animação percorre automaticamente frames de uma spritesheet. Apenas uma animação pode estar ativa por objeto simultaneamente — chamar `anim_tocar()` com uma nova animação pausa a anterior de forma automática.

Os frames da spritesheet são endereçados por coluna e linha:

```
Coluna:   0       1       2       3
        +-------+-------+-------+-------+
Linha 0 | idle  | idle  | correr| correr|
        +-------+-------+-------+-------+
Linha 1 | cima  | cima  | baixo | baixo |
        +-------+-------+-------+-------+
```

```lua
-- Criação de animações (antes do loop)
-- parâmetros: sprite_id, largura_tile, altura_tile, colunas, linhas, fps, loop, object_id
local anim_correr = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {0}, 8, true,  heroi)
local anim_atacar = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {3}, 12, false, heroi)

-- Dentro do loop
if jogo:tecla("d") then
    jogo:anim_tocar(anim_correr)
end

if jogo:tecla_press("j") then
    jogo:anim_tocar(anim_atacar)
end

-- Retornar ao frame idle quando parado
if not movendo then
    local ativa = jogo:anim_atual(heroi)
    if ativa then
        jogo:anim_parar(ativa, 0, 0)
    end
end

-- Detectar término de animação one-shot
if jogo:anim_fim(anim_atacar) then
    jogo:anim_tocar(anim_correr)
end

-- Liberação de memória
jogo:anim_destruir(anim_correr)
jogo:anim_destruir(anim_atacar)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_animacao(sid, tw, th, cols, lins, fps, loop, oid)` | Configuração | handle de animação |
| `jogo:anim_tocar(anim)` | Handle | — |
| `jogo:anim_parar(anim, col, lin)` | Handle + frame de pausa | — |
| `jogo:anim_atual(oid)` | object_id | handle ativo ou `nil` |
| `jogo:anim_fim(anim)` | Handle | `true` se concluída (one-shot) |
| `jogo:anim_destruir(anim)` | Handle | — |

---

## Fade

O sistema de fade aplica uma sobreposição que escurece ou clareia a tela gradualmente, útil para transições entre cenas.

```lua
-- Fade out (escurecimento): alvo=1, velocidade, cor opcional (padrão: preto)
jogo:fade(1, 2)
jogo:fade(1, 1, 0, 0, 80)  -- escurecimento com tom azulado

-- Fade in (clareamento)
jogo:fade(0, 2)

-- Verificar conclusão da transição
if jogo:fade_ok() then
    -- transição concluída
end
```

Exemplo de troca de cena com fade:

```lua
local trocando = false

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    if not trocando and jogo:tecla_press("return") then
        jogo:fade(1, 3)
        trocando = true
    end

    if trocando and jogo:fade_ok() then
        carregar_proxima_cena()
        jogo:fade(0, 3)
        trocando = false
    end

    jogo:limpar()
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fade(alvo, vel, r, g, b)` | `0`=claro / `1`=escuro; velocidade; cor (opcional) | — |
| `jogo:fade_ok()` | — | `true` quando concluído |

---

## Desenho Direto

Funções para renderizar formas geométricas e sprites diretamente na tela, sem criação de objetos. Indicadas para UI, debug e efeitos de fundo.

### Formas Geométricas

```lua
-- Retângulo preenchido: x, y, largura, altura, R, G, B
jogo:ret(10, 10, 100, 50, 255, 0, 0)

-- Contorno de retângulo: mesmos parâmetros + espessura em pixels
jogo:ret_contorno(10, 10, 100, 50, 255, 255, 255, 1)

-- Linha: x0, y0, x1, y1, R, G, B, espessura
jogo:linha(0, 0, 320, 240, 255, 255, 255, 1)

-- Círculo: cx, cy, raio, R, G, B, preenchido (bool)
jogo:circulo(160, 120, 40, 0, 100, 255, true)

-- Sobreposição semitransparente: x, y, w, h, R, G, B, alpha (0.0–1.0)
jogo:overlay(0, 0, 320, 240, 0, 0, 0, 0.5)
```

### Sprites

```lua
-- Renderizar região de um sprite: sprite_id, x_dest, y_dest, x_src, y_src, w, h
jogo:sprite(sid_fundo,  0,  0,  0, 0, 320, 240)
jogo:sprite(sid_sheet, 50, 50, 32, 0,  32,  32)

-- Com transformações adicionais
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, {
    escala_x = 2.0,
    escala_y = 2.0,
    rotacao  = 45,
    alpha    = 0.8,
    fh       = true,
    fv       = false,
})

-- Efeito de inversão de cores (flash de dano)
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, { invertido = true })
```

### Tilemap

Renderiza um mapa de tiles completo em uma única chamada, sem criação de objetos individuais.

```lua
-- Mapa como tabela linear, lida linha por linha
local mapa = {
    1, 1, 1, 1, 1,
    1, 0, 0, 0, 1,
    1, 0, 0, 0, 1,
    1, 1, 1, 1, 1,
}

-- parâmetros: tabela, n_linhas, n_colunas, sprite_id, tw, th, offset_x, offset_y
jogo:tilemap(mapa, 4, 5, sid_tiles, 16, 16, 0, 0)
```

---

## Texto e UI

O renderizador de texto utiliza fontes bitmap: imagens PNG com todos os caracteres organizados em grade.

```lua
-- Texto simples
-- parâmetros: x, y, string, sprite_id_fonte, largura_char, altura_char
jogo:texto(10, 10, "Pontos: " .. pts, sid_fonte, 8, 8)

-- Parâmetros completos: adiciona chars_por_linha, offset_ascii e espaçamento
jogo:texto(10, 10, "Texto", sid_fonte, 8, 8, 16, 32, 1)

-- Caixa decorada com tileset de bordas 3×3
jogo:caixa(50, 50, 200, 100, sid_bordas, 8, 8)

-- Caixa de diálogo com título e conteúdo
jogo:caixa_texto(
    50, 50, 200, 100,
    "Item encontrado!",
    "Você pegou uma espada.",
    sid_bordas, 8, 8,
    sid_fonte,  8, 8
)
```

`chars_por_linha` indica quantos caracteres cabem por linha na imagem da fonte. `offset_ascii` indica o código ASCII do primeiro caractere da imagem (o valor padrão `32` corresponde ao caractere de espaço).

---

## Efeitos Visuais

### Chuva

```lua
-- Inicializar posições das gotas
local gotas = {}
for i = 1, 50 do
    gotas[i] = {math.random(0, 320), math.random(0, 240)}
end

-- No loop: atualizar posições e renderizar
-- parâmetros: largura_tela, altura_tela, frame, gotas, largura_gota, altura_gota
jogo:chuva(320, 240, frame, gotas, 1, 6)
```

### Overlay de Noite

```lua
-- intensidade: 0.0 = dia, 1.0 = noite total
jogo:noite(320, 240, 0.0)
jogo:noite(320, 240, 0.85)
```

---

## Input — Teclado

A engine oferece três modos de leitura de teclas:

- `jogo:tecla()` — retorna `true` enquanto a tecla estiver pressionada (movimento contínuo).
- `jogo:tecla_press()` — retorna `true` apenas no frame em que a tecla foi pressionada (ação única).
- `jogo:tecla_solta()` — retorna `true` apenas no frame em que a tecla foi liberada.

```lua
-- Movimento contínuo
if jogo:tecla("d") then jogo:mover(heroi,  3, 0) end
if jogo:tecla("a") then jogo:mover(heroi, -3, 0) end

-- Ação única por pressionamento
if jogo:tecla_press("space") then pular(heroi) end

-- Detecção de liberação
if jogo:tecla_solta("lshift") then parar_corrida(heroi) end

-- Combinação de teclas
if jogo:tecla("lctrl") and jogo:tecla_press("s") then
    salvar_jogo()
end
```

**Nomes das teclas:**

| Tecla | Identificador |
|---|---|
| Letras | `"a"` a `"z"` |
| Números | `"0"` a `"9"` |
| Espaço | `"space"` |
| Enter | `"return"` |
| Escape | `"escape"` |
| Setas | `"up"`, `"down"`, `"left"`, `"right"` |
| Shift | `"lshift"`, `"rshift"` |
| Ctrl | `"lctrl"`, `"rctrl"` |
| F1–F12 | `"f1"` a `"f12"` |
| Backspace | `"backspace"` |
| Tab | `"tab"` |

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:tecla(t)` | Identificador da tecla | `true` enquanto pressionada |
| `jogo:tecla_press(t)` | Identificador da tecla | `true` no frame inicial |
| `jogo:tecla_solta(t)` | Identificador da tecla | `true` ao liberar |

---

## Input — Mouse

```lua
-- Constantes de botão: E.ESQ (0), E.MEIO (1), E.DIR (2)

-- Posição do cursor
local mx, my = jogo:mouse_pos()

-- Botão mantido pressionado
if jogo:mouse_segurado(E.ESQ) then
    arrastar_objeto(mx, my)
end

-- Botão pressionado (único por clique)
if jogo:mouse_press(E.ESQ) then
    verificar_ui(mx, my)
end

-- Botão liberado
if jogo:mouse_solta(E.ESQ) then
    soltar_objeto()
end

-- Scroll: +1 (acima), -1 (abaixo), 0 (parado)
local scroll = jogo:mouse_scroll()
if scroll ~= 0 then
    zoom = zoom + scroll * 0.1
    jogo:cam_zoom(zoom)
end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:mouse_pos()` | — | `x, y` |
| `jogo:mouse_segurado(botao)` | Constante de botão | `true` ou `false` |
| `jogo:mouse_press(botao)` | Constante de botão | `true` no frame do clique |
| `jogo:mouse_solta(botao)` | Constante de botão | `true` ao liberar |
| `jogo:mouse_scroll()` | — | `-1`, `0` ou `+1` |

---

## Áudio

```lua
-- Inicializar o subsistema (uma vez, após E.nova)
local ok = jogo:audio_init()
if not ok then print("Subsistema de áudio indisponível.") end

-- Reproduzir arquivo (OGG ou WAV)
-- parâmetros: arquivo, loop, volume (0–1), pitch (0.5–2.0)
local musica = jogo:tocar("musica.ogg", true)
local efeito = jogo:tocar("pulo.wav",   false, 0.6, 1.2)

-- Controle de reprodução
jogo:pausar(musica)
jogo:retomar(musica)
jogo:parar(musica)

-- Ajuste em tempo real
jogo:volume(musica, 0.3)
jogo:pitch(musica, 0.8)   -- abaixo de 1.0: mais grave e lento
jogo:pitch(musica, 1.5)   -- acima de 1.0: mais agudo e rápido

-- Verificar término (sons sem loop)
if jogo:audio_fim(efeito) then
    -- reprodução concluída
end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:audio_init()` | — | `true` se bem-sucedido |
| `jogo:tocar(arquivo, loop, vol, pitch)` | Configuração de reprodução | handle de áudio |
| `jogo:pausar(h)` | Handle | — |
| `jogo:retomar(h)` | Handle | — |
| `jogo:parar(h)` | Handle | — |
| `jogo:volume(h, v)` | Handle, valor 0–1 | — |
| `jogo:pitch(h, p)` | Handle, valor 0.5–2.0 | — |
| `jogo:audio_fim(h)` | Handle | `true` se concluído |

---

## Shaders

Shaders são programas executados na GPU que permitem aplicar efeitos de pós-processamento a tela inteira.

### Efeitos Predefinidos

```lua
-- Escala de cinza
jogo:efeito("cinza", true)
jogo:desenhar()
jogo:efeito("cinza", false)

-- Inversão de cores
jogo:efeito("negativo", true)
jogo:desenhar()
jogo:efeito("negativo", false)

-- Simulação de monitor CRT (scanlines, vinheta, curvatura)
jogo:efeito("crt", true, 320, 240)
jogo:desenhar()
jogo:efeito("crt", false)

-- Aberração cromática (deslocamento entre canais RGB)
-- offset: 0.003 = sutil, 0.020 = extremo
jogo:efeito("aberracao", true, 0.005)
jogo:desenhar()
jogo:efeito("aberracao", false)
```

Efeitos disponíveis: `"cinza"`, `"negativo"`, `"crt"`, `"aberracao"`.

### Shaders Personalizados (GLSL)

```lua
-- Criar shader com código GLSL
-- E.VERT_PADRAO é o vertex shader padrão da engine
local meu_shader = jogo:shader_criar(E.VERT_PADRAO, [[
    uniform sampler2D u_tex;
    uniform vec4      u_tint;
    varying vec2 v_uv;
    varying vec4 v_color;
    void main() {
        vec4 c = texture2D(u_tex, v_uv) * v_color;
        gl_FragColor = vec4(c.rgb * u_tint.rgb, c.a * u_tint.a);
    }
]])

-- Ativar no loop
jogo:shader_usar(meu_shader)

-- Passar uniforms (tipo inferido pelo número de valores: 1=float, 2=vec2, 4=vec4)
jogo:shader_uniform(meu_shader, "u_tint",      1.0, 0.5, 0.0, 1.0)
jogo:shader_uniform(meu_shader, "u_resolucao", 320.0, 240.0)
jogo:shader_uniform(meu_shader, "u_tempo",     jogo:tempo())

jogo:desenhar()
jogo:shader_nenhum()

-- Liberação
jogo:shader_destruir(meu_shader)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:efeito(nome, ativar, ...)` | Nome, bool, args opcionais | — |
| `jogo:shader_criar(vert, frag)` | Código GLSL | `shader_handle` |
| `jogo:shader_usar(sh)` | Handle | — |
| `jogo:shader_nenhum()` | — | — |
| `jogo:shader_uniform(sh, nome, ...)` | Handle, nome, valores | — |
| `jogo:shader_destruir(sh)` | Handle | — |

---

## FBO (Framebuffer)

Um FBO (Framebuffer Object) redireciona a renderização para uma textura em vez da tela, permitindo pós-processamento, efeitos compostos e renderização offscreen.

Fluxo de uso: criar FBO → registrar como sprite → no loop: bind → renderizar → unbind → utilizar o sprite resultante.

```lua
-- Criação
local fbo     = jogo:fbo_criar(320, 240)
local sid_fbo = jogo:fbo_como_sprite(fbo)

-- No loop
jogo:fbo_bind(fbo)
    jogo:limpar()
    jogo:desenhar()
jogo:fbo_unbind()

-- Aplicar efeito e exibir na tela
jogo:efeito("crt", true, 320, 240)
jogo:sprite(sid_fbo, 0, 0, 0, 0, 320, 240)
jogo:efeito("crt", false)

-- Liberação
jogo:fbo_destruir(fbo)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fbo_criar(w, h)` | Largura, altura | `fbo_handle` |
| `jogo:fbo_bind(fh)` | Handle | — |
| `jogo:fbo_unbind()` | — | — |
| `jogo:fbo_como_sprite(fh)` | Handle | `sprite_id` |
| `jogo:fbo_destruir(fh)` | Handle | — |

---

## FOV — Campo de Visão

O sistema de FOV implementa neblina de guerra com shadowcasting 2D: tiles fora do campo de visão do jogador são ocultados, e obstáculos bloqueiam a linha de visão.

**Modos disponíveis:**

| Modo | Valor | Comportamento |
|---|---|---|
| `BASICO` | `0` | Sem memória: tudo escurecido fora da visão atual |
| `NEBLINA` | `1` | Áreas visitadas permanecem semi-visíveis |
| `SUAVE` | `2` | Transição suavizada nas bordas da visão |

```lua
-- Criar sessão de FOV
-- parâmetros: colunas, linhas, raio de visão (em tiles), modo
local fov = jogo:fov_novo(20, 15, 8, 1)

-- Recalcular após movimento do jogador (coordenadas em tiles, não pixels)
local col = math.floor(px / 16)
local lin = math.floor(py / 16)

jogo:fov_calcular(fov, col, lin, function(col, row)
    -- retorne true se o tile bloqueia a visão
    local idx = row * 20 + col + 1
    return mapa[idx] == PAREDE
end)

-- Renderização
jogo:limpar()
jogo:tilemap(mapa, 15, 20, sid_tiles, 16, 16, 0, 0)
jogo:desenhar()
jogo:fov_sombra(fov, 16, 16, 0, 0)        -- sombra preta
jogo:fov_sombra(fov, 16, 16, 0, 0, 0, 0, 40)  -- sombra com tom azulado

-- Consultas de estado
if jogo:fov_visivel(fov, 10, 5) then ... end
if jogo:fov_explorado(fov, 10, 5) then ... end

-- Alteração dinâmica do raio
jogo:fov_raio(fov, 3)

-- Reset ao trocar de mapa
jogo:fov_reset(fov)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fov_novo(cols, rows, raio, modo)` | Dimensões, raio, modo | handle de FOV |
| `jogo:fov_calcular(fov, col, row, fn)` | FOV, posição em tiles, função de bloqueio | — |
| `jogo:fov_sombra(fov, tw, th, ox, oy, r, g, b)` | FOV, tile size, offset, cor | — |
| `jogo:fov_visivel(fov, col, row)` | FOV, tile | `true` ou `false` |
| `jogo:fov_explorado(fov, col, row)` | FOV, tile | `true` ou `false` |
| `jogo:fov_raio(fov, r)` | FOV, raio | — |
| `jogo:fov_reset(fov)` | FOV | — |

---

## Grade Espacial

A Grade Espacial (Spatial Grid) divide o mundo em células para acelerar detecção de colisão em cenas com muitos objetos. Em vez de verificar todos os pares possíveis (O(n²)), verifica apenas objetos na mesma célula (O(1) amortizado).

Recomendada para cenas com mais de ~30 objetos ativos simultaneamente.

```lua
-- Ativar antes do loop
-- cell_size em pixels; 0 usa o padrão de 64px
jogo:sgrid_init(64)

-- Reconstruir ao criar muitos objetos fora do loop
for i = 1, 200 do criar_inimigo() end
jogo:sgrid_rebuild()

-- Colisão com o primeiro objeto encontrado
local alvo = jogo:sgrid_primeira_colisao(projetil)
if alvo then
    causar_dano(alvo)
    jogo:remover_objeto(projetil)
end

-- Colisão com todos os objetos em uma área
for _, oid in ipairs(jogo:sgrid_todas_colisoes(explosao, 32)) do
    causar_dano(oid)
end

-- Verificar estado
if jogo:sgrid_ativo() then ... end

-- Liberação
jogo:sgrid_destruir()
```

A grade é atualizada automaticamente a cada chamada de `jogo:mover()`. `sgrid_rebuild()` é necessário apenas ao criar objetos em lote fora do loop principal.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:sgrid_init(cell_size)` | Tamanho da célula (0 = automático) | — |
| `jogo:sgrid_rebuild()` | — | — |
| `jogo:sgrid_primeira_colisao(oid)` | object_id | ID do objeto ou `nil` |
| `jogo:sgrid_todas_colisoes(oid, max)` | object_id, limite | tabela de IDs |
| `jogo:sgrid_ativo()` | — | `true` ou `false` |
| `jogo:sgrid_destruir()` | — | — |

---

## Sistema de Mapas

O sistema de mapas é composto por três arquivos com responsabilidades distintas:

| Arquivo | Responsabilidade |
|---|---|
| `mapa.hpp` | Structs, flags e API pública em C++ |
| `mapa.lua` | Módulo Lua para descrever e construir a estrutura do mapa |
| `engine.lua` | Funções de alto nível: carregamento, renderização e colisão |

O Lua descreve o conteúdo do mapa (tiles, camadas, objetos). A engine cuida de carregar o atlas, montar a tabela de colisores e renderizar cada frame.

### Fluxo básico

```lua
-- Antes do loop: carrega o arquivo de mapa
local mapa = engine:carregar_mapa("mapa_fazenda.lua")

-- Dentro do loop, antes de engine:desenhar()
engine:desenhar_mapa(mapa)

-- Verifica colisão do jogador com tiles sólidos
if engine:colide_mapa(mapa, jogador) then
    engine:mover(jogador, -dx, -dy)   -- desfaz o movimento
end
```

`engine:carregar_mapa()` lê o arquivo Lua, carrega o atlas na GPU e pré-computa a tabela de colisores — tudo em uma chamada. A partir daí, `desenhar_mapa()` e `colide_mapa()` são chamadas baratas por frame.

### Criando um arquivo de mapa

O arquivo de mapa é um script Lua que usa o módulo `mapa.lua` para descrever tiles, camadas e objetos, e deve sempre terminar com `return m:build()`.

**Estrutura mínima:**

```lua
-- mapa_exemplo.lua
local Mapa = require("mapa")

local m = Mapa.novo(20, 15, 16, 16)   -- 20 colunas, 15 linhas, tiles de 16×16 px
m:atlas("sprites/tileset.png")        -- spritesheet principal

-- ... definição de tiles e objetos ...

return m:build()   -- NÃO remova esta linha
```

**Método 1 — Matriz de blocos (recomendado)**

Defina protótipos com `criar_bloco()` e monte o layout com um array 1D. É a forma mais legível para mapas criados à mão.

```lua
local Mapa = require("mapa")
local m = Mapa.novo(10, 8, 16, 16)
m:atlas("sprites/tileset.png")

local _ = 0   -- célula vazia

-- criar_bloco(nome, id, config)
-- config aceita: tiles={col, lin}, flags=Mapa.F.*, colide=1 (atalho para COLISOR)
local G = m:criar_bloco("Grama",  1, { tiles = {0, 0} })
local T = m:criar_bloco("Terra",  2, { tiles = {1, 0} })
local P = m:criar_bloco("Parede", 3, { tiles = {2, 0}, colide = 1 })
local A = m:criar_bloco("Agua",   4, { tiles = {0, 2}, flags = Mapa.F.AGUA })

m:carregar_matriz({
    P, P, P, P, P, P, P, P, P, P,
    P, G, G, T, T, G, G, G, G, P,
    P, G, G, T, T, G, A, A, G, P,
    P, G, G, G, G, G, A, A, G, P,
    P, G, G, G, G, G, G, G, G, P,
    P, G, G, G, G, G, G, G, G, P,
    P, G, G, G, G, G, G, G, G, P,
    P, P, P, P, P, P, P, P, P, P,
})

return m:build()
```

O array é lido linha por linha, da esquerda para a direita. O elemento `0` (ou `nil`) é ignorado — a célula fica vazia. A largura do mapa é usada automaticamente para converter o índice 1D em `(col, lin)`.

**Método 2 — Camadas com funções de preenchimento**

Para controle mais fino, crie camadas explicitamente e posicione tiles com as funções da camada.

```lua
local Mapa = require("mapa")
local m = Mapa.novo(25, 18, 16, 16)
m:atlas("sprites/tileset.png")

-- camada(nome, z_order)  — z_order menor = desenhado primeiro
local chao    = m:camada("chao",      0)
local paredes = m:camada("paredes",   1)
local deco    = m:camada("decoracao", 2)

-- Preenche toda a área de chão com o tile (col=0, lin=0) do atlas
chao:fill(0, 0, 24, 17, 0, 0)

-- Cria uma borda sólida com o tile (col=3, lin=1)
paredes:borda(25, 18, 3, 1)

-- Tile individual: col, lin no mapa, col, lin no atlas, flags
paredes:tile(5, 3, 1, 2, Mapa.F.COLISOR)

-- Linha horizontal de tiles
paredes:linha_h(5, 11, 8, 0, 1, Mapa.F.COLISOR)

-- Linha vertical de tiles
paredes:linha_v(12, 2, 7, 0, 1, Mapa.F.COLISOR)

-- Tile com animação de frames (desloca coluna no atlas, linha fixa)
chao:tile_animado(10, 5, 2, {0, 1, 2, 3}, 8)
--                col lin lin_atlas frames  fps

-- Decoração sem colisão
deco:tile(6, 4, 4, 0)

return m:build()
```

Os dois métodos podem ser combinados: use `criar_bloco` + `carregar_matriz` para o chão e paredes, e camadas manuais para detalhes ou animações.

### Objetos e triggers

Objetos são entidades especiais posicionadas em tiles: baús, NPCs, portas, teleportes. Eles definem uma área de ativação (`raio` em tiles) e propriedades arbitrárias.

```lua
-- objeto(id, tipo, col, lin, raio, props, sprite_opcional)
m:objeto(1, "npc",       10, 8,  2.0, { nome = "Fazendeiro", dialogo = "Bom dia!" })
m:objeto(2, "bau",        3, 3,  1.5, { item = "enxada", quantidade = "1" })
m:objeto(3, "teleporte", 13, 15, 1.0, { destino = "mapa_vila.lua", dest_col = "1", dest_lin = "7" })
```

| String no Lua | Comportamento esperado |
|---|---|
| `"bau"` | Coleta ao pressionar `[E]` |
| `"npc"` | Diálogo ao pressionar `[E]` |
| `"porta"` | Abre/fecha mediante condição |
| `"teleporte"` | Troca de mapa ao pisar |
| `"script"` | Executa lógica definida em Lua |
| `"generico"` | Sem comportamento predefinido |

> Os tipos são apenas strings — a lógica de reação fica no seu `main.lua`. A engine só informa qual objeto está próximo do jogador.

### Flags de bloco

Flags controlam o comportamento físico e visual de cada tile. Combine-as com `bor()` (LuaJIT):

```lua
-- Flags disponíveis em Mapa.F
Mapa.F.NENHUM   -- 0      sem comportamento especial
Mapa.F.COLISOR  -- 0x01   bloqueia movimento (AABB sólido)
Mapa.F.TRIGGER  -- 0x02   dispara evento de proximidade
Mapa.F.AGUA     -- 0x04   tratado como superfície aquática
Mapa.F.ESCADA   -- 0x08   permite escalada
Mapa.F.SOMBRA   -- 0x10   bloqueia o FOV
Mapa.F.ANIMADO  -- 0x20   tile com animação de frames

-- Combinando flags
local bit   = require("bit")
local flags = bit.bor(Mapa.F.COLISOR, Mapa.F.SOMBRA)
```

No `criar_bloco()`, o atalho `colide = 1` equivale a `flags = Mapa.F.COLISOR` e pode ser usado junto com outras flags:

```lua
-- Equivalentes:
m:criar_bloco("Parede", 1, { tiles = {2, 0}, colide = 1 })
m:criar_bloco("Parede", 1, { tiles = {2, 0}, flags = Mapa.F.COLISOR })
```

> **Atenção — LuaJIT:** os operadores `<<` e `|` do Lua 5.3+ não funcionam no LuaJIT. Use sempre `bit.lshift()` e `bit.bor()` (já importados internamente pelo `mapa.lua`).

### API de alto nível — engine.lua

| Função | Parâmetros | Retorno | Descrição |
|---|---|---|---|
| `engine:carregar_mapa(caminho)` | Caminho do `.lua` | tabela de mapa | Carrega o mapa, o atlas e pré-computa colisores |
| `engine:desenhar_mapa(mapa)` | Tabela retornada por `carregar_mapa` | — | Renderiza todos os tiles visíveis |
| `engine:colide_mapa(mapa, oid)` | Mapa, object_id | `true` ou `false` | Verifica colisão AABB do objeto com tiles sólidos |

**Exemplo de loop com mapa:**

```lua
local mapa    = engine:carregar_mapa("mapa_fazenda.lua")
local sprite  = engine:carregar_sprite("sprites/player.png")
local jogador = engine:criar_objeto_tile(64, 64, sprite, 0, 0, 16, 16)
engine:hitbox(jogador, 1, 4, 14, 12)

local vel = 2
local dx, dy = 0, 0

while engine:rodando() do
    engine:eventos()
    engine:atualizar()
    engine:limpar()

    engine:desenhar_mapa(mapa)   -- sempre antes de engine:desenhar()

    dx, dy = 0, 0
    if engine:tecla("d") then dx =  vel end
    if engine:tecla("a") then dx = -vel end
    if engine:tecla("s") then dy =  vel end
    if engine:tecla("w") then dy = -vel end

    engine:mover(jogador, dx, dy)

    if engine:colide_mapa(mapa, jogador) then
        engine:mover(jogador, -dx, -dy)
    end

    engine:desenhar()
    engine:apresentar()
    engine:fps(60)
end

engine:destruir()
```

### API do módulo mapa.lua

**Mapa:**

| Função | Parâmetros | Descrição |
|---|---|---|
| `Mapa.novo(larg, alt, tw, th)` | Colunas, linhas, px por tile | Cria um mapa vazio |
| `m:atlas(caminho)` | Arquivo PNG | Define o spritesheet principal |
| `m:camada(nome, z_order)` | String, número | Cria e retorna uma camada |
| `m:criar_bloco(nome, id, config)` | Nome, id inteiro, config | Registra um protótipo de bloco; retorna `id` |
| `m:carregar_matriz(layout)` | Array 1D de IDs | Preenche camadas a partir de um layout visual |
| `m:objeto(id, tipo, col, lin, raio, props, sprite?)` | Ver seção acima | Registra um objeto/trigger |
| `m:build()` | — | Serializa e retorna a tabela final para a engine |

**Campos de `config` em `criar_bloco`:**

| Campo | Tipo | Descrição |
|---|---|---|
| `tiles` | `{col, lin}` | Posição do tile no atlas |
| `flags` | `Mapa.F.*` | Bitmask de comportamento (padrão `0`) |
| `colide` | `1` ou `0` | Atalho para `Mapa.F.COLISOR` |
| `camada` | número | Índice da camada alvo (padrão `0`) |
| `sprite` | string | Spritesheet alternativa para este bloco |

**Camada:**

| Função | Parâmetros | Descrição |
|---|---|---|
| `c:tile(col, lin, sc, sl, flags, sprite?)` | Posição no mapa, posição no atlas, flags | Define um tile individual |
| `c:tile_animado(col, lin, lin_atlas, frames, fps, flags?)` | Posição, linha do atlas, lista de colunas, fps | Tile com animação de frames |
| `c:fill(ci, li, cf, lf, sc, sl, flags?)` | Retângulo (col/lin ini e fim), tile, flags | Preenche uma área retangular |
| `c:borda(larg, alt, sc, sl)` | Dimensões do mapa, tile | Cria borda sólida ao redor do mapa |
| `c:linha_h(ci, cf, lin, sc, sl, flags?)` | Coluna inicial, final, linha, tile, flags | Linha horizontal de tiles |
| `c:linha_v(col, li, lf, sc, sl, flags?)` | Coluna, linha inicial, final, tile, flags | Linha vertical de tiles |

### Constantes de tile prontas

O módulo exporta `Mapa.TILES` com definições prontas para tilesets padrão. Ajuste os valores de `col` e `lin` conforme o seu spritesheet.

```lua
Mapa.TILES.GRAMA         -- {col=0, lin=0, flags=NENHUM}
Mapa.TILES.TERRA         -- {col=1, lin=0, flags=NENHUM}
Mapa.TILES.PAREDE        -- {col=0, lin=1, flags=COLISOR|SOMBRA}
Mapa.TILES.AGUA          -- {col=0, lin=2, flags=AGUA}
Mapa.TILES.LAVA          -- {col=1, lin=2, flags=AGUA|COLISOR}
Mapa.TILES.ESCADA_BAIXO  -- {col=0, lin=3, flags=ESCADA}
Mapa.TILES.PORTA         -- {col=0, lin=4, flags=TRIGGER}
Mapa.TILES.BAU           -- {col=1, lin=4, flags=TRIGGER}
```

Use com `Mapa.tile_de()` para extrair os parâmetros:

```lua
-- tile_de(constante, flags_extra?) → sprite_col, sprite_lin, flags
local sc, sl, fl = Mapa.tile_de(Mapa.TILES.PAREDE)
chao:tile(3, 2, sc, sl, fl)
```

---

## Exemplo Completo

Jogo funcional mínimo com movimento, animação, câmera e áudio:

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)

jogo:audio_init()
jogo:fundo(20, 20, 30)
jogo:sgrid_init(64)

local sid_sheet = jogo:carregar_sprite("heroi.png")
local musica    = jogo:tocar("musica.ogg", true, 0.5)

local heroi       = jogo:criar_objeto_tile(160, 180, sid_sheet, 0, 0, 16, 16)
jogo:hitbox(heroi, 2, 2, 12, 13)

local anim_correr = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {0}, 8, true,  heroi)
local anim_cima   = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {1}, 8, true,  heroi)
local anim_baixo  = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {2}, 8, true,  heroi)

jogo:cam_seguir(heroi, 0.15)

local vel = 80

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    local dt      = jogo:delta()
    local movendo = false

    if jogo:tecla("d") then
        jogo:mover(heroi, vel * dt, 0)
        jogo:espelhar(heroi, true, false)
        jogo:anim_tocar(anim_correr)
        movendo = true
    elseif jogo:tecla("a") then
        jogo:mover(heroi, -vel * dt, 0)
        jogo:espelhar(heroi, false, false)
        jogo:anim_tocar(anim_correr)
        movendo = true
    elseif jogo:tecla("w") then
        jogo:mover(heroi, 0, -vel * dt)
        jogo:anim_tocar(anim_cima)
        movendo = true
    elseif jogo:tecla("s") then
        jogo:mover(heroi, 0,  vel * dt)
        jogo:anim_tocar(anim_baixo)
        movendo = true
    end

    if not movendo then
        local ativa = jogo:anim_atual(heroi)
        if ativa then
            jogo:anim_parar(ativa, 0, 0)
        end
    end

    if jogo:tecla_press("f")      then jogo:tela_cheia() end
    if jogo:tecla_press("escape") then break end

    jogo:limpar()
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end

jogo:destruir()
```
