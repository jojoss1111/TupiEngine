# 📖 Documentação da Engine 2D

> Engine 2D em Lua pensada para iniciantes. Todas as funções estão em português e organizadas por categoria.

---

## Sumário

1. [Como funciona](#como-funciona)
2. [Loop principal](#loop-principal)
3. [Janela](#janela)
4. [Sprites](#sprites)
5. [Objetos](#objetos)
6. [Colisão](#colisão)
7. [Câmera](#câmera)
8. [Partículas](#partículas)
9. [Animação](#animação)
10. [Fade](#fade)
11. [Desenho direto](#desenho-direto)
12. [Texto e UI](#texto-e-ui)
13. [Efeitos visuais](#efeitos-visuais)
14. [Input — Teclado](#input--teclado)
15. [Input — Mouse](#input--mouse)
16. [Áudio](#áudio)
17. [Shaders](#shaders)
18. [FBO (Framebuffer)](#fbo-framebuffer)
19. [FOV — Campo de Visão](#fov--campo-de-visão)
20. [Grade Espacial](#grade-espacial)

---

## Como funciona

A engine é carregada com `require` e funciona através de um **objeto de jogo** criado com `E.nova()`. A partir daí, todas as funções são chamadas nesse objeto usando `:`.

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)
```

> **Dica:** O `:` é o modo Lua de chamar funções que pertencem a um objeto. `jogo:limpar()` é o mesmo que `jogo.limpar(jogo)`.

---

## Loop principal

Todo jogo funciona em volta de um loop que roda dezenas de vezes por segundo. Cada repetição desse loop é chamada de **frame**. A ordem das chamadas importa.

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)

jogo:fundo(30, 30, 40)  -- cor de fundo cinza-azulado

while jogo:rodando() do
    jogo:eventos()    -- 1. processa teclado, mouse e janela
    jogo:atualizar()  -- 2. atualiza câmera, animações, áudio
    jogo:limpar()     -- 3. apaga o frame anterior
    -- 4. seu código de desenho fica aqui
    jogo:desenhar()   -- 5. desenha todos os objetos da cena
    jogo:apresentar() -- 6. exibe o frame na tela
    jogo:fps(60)      -- 7. limita a 60 quadros por segundo
end

jogo:destruir()  -- libera recursos ao fechar
```

### Referência das funções do loop

| Função | O que faz |
|---|---|
| `E.nova(largura, altura, titulo, escala)` | Cria a janela e retorna o objeto do jogo |
| `jogo:rodando()` | Retorna `true` enquanto a janela estiver aberta |
| `jogo:eventos()` | Processa entradas (teclado, mouse, fechar janela) |
| `jogo:atualizar()` | Atualiza câmera, animações, partículas e áudio |
| `jogo:limpar()` | Apaga a tela com a cor de fundo |
| `jogo:desenhar()` | Desenha todos os objetos, partículas e fade |
| `jogo:apresentar()` | Envia o frame para a tela |
| `jogo:fps(alvo)` | Limita os quadros por segundo (padrão: 60) |
| `jogo:destruir()` | Fecha a janela e libera a memória |
| `jogo:tempo()` | Segundos desde o início do jogo |
| `jogo:delta()` | Duração do último frame em segundos |

> **O que é delta time?**
> É o tempo que o último frame levou para rodar. Usar `delta` para calcular velocidade faz o jogo se mover igual em qualquer computador, rápido ou lento.
> ```lua
> -- Velocidade de 100 pixels por segundo, independente do FPS
> local velocidade = 100
> local x = x + velocidade * jogo:delta()
> ```

---

## Janela

```lua
-- Cor de fundo em RGB (0 a 255)
jogo:fundo(0, 0, 0)        -- preto
jogo:fundo(135, 206, 235)  -- azul céu

-- Alternar tela cheia
jogo:tela_cheia()

-- Saber o tamanho da área de desenho
local largura, altura = jogo:tamanho()
print(largura, altura)  -- ex: 320   240
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fundo(r, g, b)` | Vermelho, verde, azul (0–255) | — |
| `jogo:tela_cheia()` | — | — |
| `jogo:tamanho()` | — | `largura, altura` |

---

## Sprites

Um **sprite** é uma imagem carregada na memória. Cada sprite recebe um **sprite_id** (número) que você usa para criar objetos, desenhar etc.

```lua
-- Carrega uma imagem inteira
local sid_heroi  = jogo:carregar_sprite("imagens/heroi.png")
local sid_fundo  = jogo:carregar_sprite("imagens/fundo.png")

-- Carrega apenas uma parte de uma imagem (recorte de spritesheet)
-- parâmetros: arquivo, x, y, largura, altura  (em pixels, na imagem)
local sid_frame1 = jogo:carregar_regiao("sheet.png",  0, 0, 32, 32)
local sid_frame2 = jogo:carregar_regiao("sheet.png", 32, 0, 32, 32)
```

> **Retorna -1** se o arquivo não existir. Bom verificar:
> ```lua
> local sid = jogo:carregar_sprite("heroi.png")
> if sid == -1 then error("Não achei heroi.png!") end
> ```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:carregar_sprite(caminho)` | Caminho do arquivo PNG | `sprite_id` |
| `jogo:carregar_regiao(caminho, x, y, w, h)` | Arquivo + região em pixels | `sprite_id` |

---

## Objetos

**Objetos** são as entidades da cena: personagens, inimigos, plataformas, itens. Cada objeto criado recebe um **object_id** (número) que você guarda para controlar depois.

### Criar e remover

```lua
-- Criar objeto com sprite inteiro
-- parâmetros: x, y, sprite_id, largura, altura
local heroi   = jogo:criar_objeto(100, 200, sid_heroi, 32, 32)
local inimigo = jogo:criar_objeto(300, 200, sid_inimigo, 32, 32)

-- Criar objeto usando um tile de uma spritesheet
-- parâmetros: x, y, sprite_id, coluna_tile, linha_tile, largura_tile, altura_tile
local arvore = jogo:criar_objeto_tile(64, 128, sid_tiles, 3, 0, 16, 16)

-- Remover
jogo:remover_objeto(inimigo)
```

### Posição e movimento

```lua
-- Mover por deslocamento relativo (dx, dy)
jogo:mover(heroi, 2, 0)    -- anda 2 pixels para a direita
jogo:mover(heroi, 0, -3)   -- sobe 3 pixels

-- Posicionar em coordenada exata
jogo:posicionar(heroi, 50, 100)

-- Ler a posição atual
local px, py = jogo:posicao(heroi)
print("Herói em:", px, py)
```

### Aparência

```lua
-- Trocar o sprite do objeto
jogo:objeto_sprite(heroi, sid_heroi_correndo)

-- Trocar o tile exibido (coluna, linha na spritesheet)
jogo:objeto_tile(heroi, 2, 1)

-- Espelhar (horizontal, vertical)
jogo:espelhar(heroi, true, false)   -- espelha na horizontal
jogo:espelhar(heroi, false, false)  -- volta ao normal

-- Escala: 1.0 = normal, 2.0 = dobro do tamanho
jogo:escala(heroi, 2.0, 2.0)
jogo:escala(heroi, 0.5, 0.5)  -- metade do tamanho

-- Rotação em graus (sentido horário)
jogo:rotacao(heroi, 45)
jogo:rotacao(heroi, 0)   -- sem rotação

-- Transparência: 0 = invisível, 1 = opaco
jogo:transparencia(heroi, 0.5)   -- 50% transparente
jogo:transparencia(heroi, 1.0)   -- opaco

-- Camada de profundidade (maior = na frente)
jogo:camada(heroi, 2, 0)
jogo:camada(fundo, 0, 0)
```

### Hitbox

A hitbox é a área de colisão do objeto. Por padrão ela cobre o sprite inteiro, mas você pode ajustar para ser menor, o que deixa as colisões mais precisas.

```lua
-- Definir hitbox com offset e tamanho próprios
-- parâmetros: object_id, offset_x, offset_y, largura, altura
-- Exemplo: sprite de 32x32, hitbox centralizada de 20x28
jogo:hitbox(heroi, 6, 4, 20, 28)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_objeto(x, y, sid, w, h)` | Posição, sprite, tamanho | `object_id` |
| `jogo:criar_objeto_tile(x, y, sid, tx, ty, tw, th)` | Posição, sprite, tile, tamanho tile | `object_id` |
| `jogo:remover_objeto(oid)` | ID do objeto | — |
| `jogo:mover(oid, dx, dy)` | ID, deslocamento | — |
| `jogo:posicionar(oid, x, y)` | ID, posição | — |
| `jogo:posicao(oid)` | ID | `x, y` |
| `jogo:objeto_sprite(oid, sid)` | ID, sprite_id | — |
| `jogo:objeto_tile(oid, col, lin)` | ID, coluna, linha | — |
| `jogo:espelhar(oid, h, v)` | ID, bool, bool | — |
| `jogo:escala(oid, sx, sy)` | ID, escala X, escala Y | — |
| `jogo:rotacao(oid, graus)` | ID, ângulo | — |
| `jogo:transparencia(oid, a)` | ID, valor 0–1 | — |
| `jogo:camada(oid, layer, z)` | ID, camada, ordem Z | — |
| `jogo:hitbox(oid, ox, oy, w, h)` | ID, offset X/Y, tamanho | — |

---

## Colisão

Colisão verifica se dois objetos estão se tocando, usando suas hitboxes.

```lua
-- Colisão entre dois objetos
if jogo:colidem(heroi, inimigo) then
    print("Herói foi atingido!")
end

-- Colisão com um retângulo (útil para chão, paredes fixas)
-- parâmetros: object_id, x, y, largura, altura do retângulo
if jogo:colide_ret(heroi, 0, 230, 320, 10) then
    print("Herói tocou o chão")
end

-- Verifica se um ponto está dentro do objeto (útil para clique do mouse)
local mx, my = jogo:mouse_pos()
if jogo:colide_ponto(botao, mx, my) then
    print("Mouse em cima do botão!")
end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:colidem(oid1, oid2)` | Dois object_ids | `true` ou `false` |
| `jogo:colide_ret(oid, rx, ry, rw, rh)` | Objeto + retângulo | `true` ou `false` |
| `jogo:colide_ponto(oid, px, py)` | Objeto + ponto | `true` ou `false` |

> **Quando usar a Grade Espacial?**
> As funções acima checam um par de objetos por vez. Se você tem centenas de inimigos e projéteis, use a [Grade Espacial](#grade-espacial) para checar colisões de forma muito mais eficiente.

---

## Câmera

A câmera controla o que está visível na tela. Por padrão ela fica parada em (0, 0).

```lua
-- Posicionar a câmera em um ponto do mundo
jogo:cam_pos(500, 300)

-- Câmera segue um objeto suavemente
-- suavidade: 0.0 = não se move, 1.0 = instantâneo
jogo:cam_seguir(heroi, 0.1)   -- suave
jogo:cam_seguir(heroi, 1.0)   -- cola no herói

-- Zoom: 1.0 = normal, 2.0 = aproximado, 0.5 = afastado
jogo:cam_zoom(1.5)

-- Efeito de tremor (impacto, explosão)
-- parâmetros: intensidade (pixels), duração (segundos)
jogo:cam_tremor(6, 0.3)

-- Converter coordenadas
local sx, sy = jogo:mundo_para_tela(wx, wy)  -- mundo → tela
local wx, wy = jogo:tela_para_mundo(sx, sy)  -- tela → mundo
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:cam_pos(x, y)` | Posição no mundo | — |
| `jogo:cam_seguir(oid, lerp)` | Objeto, suavidade 0–1 | — |
| `jogo:cam_zoom(z)` | Fator de zoom | — |
| `jogo:cam_tremor(intensidade, duracao)` | Pixels, segundos | — |
| `jogo:mundo_para_tela(wx, wy)` | Posição no mundo | `sx, sy` |
| `jogo:tela_para_mundo(sx, sy)` | Posição na tela | `wx, wy` |

---

## Partículas

Partículas são pequenos elementos visuais usados para efeitos como fogo, fumaça, faíscas, sangue etc.

```lua
-- Criar um emissor de partículas
local fagulhas = jogo:criar_emissor({
    x = 160, y = 120,             -- posição inicial

    vel  = {-40, 40, -80, -20},   -- velocidade: {vx_min, vx_max, vy_min, vy_max}
    grav = {0, 120},              -- gravidade: {ax, ay}  (positivo = para baixo)

    vida    = {0.3, 1.0},         -- duração de cada partícula (min, max) em segundos
    tamanho = {5, 0},             -- tamanho: começa em 5 e vai a 0 ao morrer

    cor       = {1.0, 0.5, 0.0, 1.0},  -- cor inicial RGBA (0.0 a 1.0)
    cor_final = {1.0, 0.0, 0.0, 0.0},  -- cor final (some ao morrer)

    rate = 0,    -- partículas por segundo (0 = só burst manual)
    max  = 100,  -- máximo de partículas ao mesmo tempo
})

-- Disparar 30 partículas de uma vez (explosão)
jogo:burst(fagulhas, 30)

-- Mover o ponto de emissão (ex: seguir o herói)
local px, py = jogo:posicao(heroi)
jogo:emissor_pos(fagulhas, px, py)

-- Remover o emissor quando não precisar mais
jogo:remover_emissor(fagulhas)
```

> `jogo:desenhar()` já inclui as partículas automaticamente — não precisa chamar nada extra.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_emissor(cfg)` | Tabela de configuração | `emitter_id` |
| `jogo:burst(eid, n)` | ID, quantidade | — |
| `jogo:emissor_pos(eid, x, y)` | ID, posição | — |
| `jogo:remover_emissor(eid)` | ID | — |

---

## Animação

Animações percorrem frames de uma spritesheet automaticamente.

```lua
-- Spritesheet com 4 frames de corrida na linha 0
local sid_sheet = jogo:carregar_sprite("heroi_sheet.png")
local heroi     = jogo:criar_objeto_tile(100, 100, sid_sheet, 0, 0, 32, 32)

-- Criar animação
-- parâmetros: sprite_id, largura_tile, altura_tile, colunas, linhas, fps, loop, object_id
local anim_correr = jogo:criar_animacao(
    sid_sheet,       -- spritesheet
    32, 32,          -- tamanho de cada tile
    {0, 1, 2, 3},    -- colunas dos frames (4 frames)
    {0},             -- linha (todos na linha 0)
    8,               -- 8 frames por segundo
    true,            -- loop
    heroi            -- objeto que será animado
)

-- Animação de ataque (one-shot, sem loop)
local anim_atacar = jogo:criar_animacao(
    sid_sheet, 32, 32,
    {4, 5, 6},   -- frames do ataque
    {1},         -- na linha 1
    12,          -- mais rápido
    false,       -- sem loop (toca uma vez)
    heroi
)

-- No loop do jogo:
if jogo:tecla("a") or jogo:tecla("d") then
    jogo:anim_tocar(anim_correr)
end

if jogo:tecla_press("j") then
    jogo:anim_tocar(anim_atacar)
end

-- Verificar se animação one-shot terminou
if jogo:anim_fim(anim_atacar) then
    jogo:anim_tocar(anim_correr)  -- volta a correr
end

-- Parar e exibir frame fixo
jogo:anim_parar(anim_correr, 0, 0)  -- para no frame (col 0, linha 0)

-- Liberar quando não precisar mais
jogo:anim_destruir(anim_correr)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_animacao(sid, tw, th, cols, lins, fps, loop, oid)` | Ver exemplo | handle de animação |
| `jogo:anim_tocar(anim)` | Handle | — |
| `jogo:anim_parar(anim, col, lin)` | Handle, frame opcional | — |
| `jogo:anim_fim(anim)` | Handle | `true` ou `false` |
| `jogo:anim_destruir(anim)` | Handle | — |

---

## Fade

Fade é uma transição onde a tela escurece ou clareia gradualmente. Muito usado para trocar de fase ou cena.

```lua
-- Escurecer a tela (fade out)
-- parâmetros: alvo (1=preto), velocidade, r, g, b da cor
jogo:fade(1, 2)              -- escurece em 0.5 segundo (velocidade 2)
jogo:fade(1, 1, 0, 0, 80)   -- fade para azul escuro

-- Clarear a tela (fade in)
jogo:fade(0, 2)   -- clareia

-- Checar se o fade terminou
if jogo:fade_ok() then
    -- pode fazer a troca de fase aqui
end
```

**Exemplo de troca de fase com fade:**
```lua
local fase       = 1
local trocando   = false

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    if not trocando and jogo:tecla_press("enter") then
        jogo:fade(1, 3)   -- começa a escurecer
        trocando = true
    end

    if trocando and jogo:fade_ok() then
        fase = fase + 1
        carregar_fase(fase)  -- sua função de carregar fase
        jogo:fade(0, 3)      -- clareia na nova fase
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
| `jogo:fade(alvo, vel, r, g, b)` | 0=claro, 1=escuro; velocidade; cor | — |
| `jogo:fade_ok()` | — | `true` quando terminou |

---

## Desenho direto

Funções para desenhar formas geométricas e sprites diretamente na tela, sem criar objetos.

### Formas

```lua
-- Retângulo preenchido: x, y, largura, altura, R, G, B
jogo:ret(10, 10, 100, 50, 255, 0, 0)   -- retângulo vermelho

-- Contorno de retângulo: mesmos parâmetros + espessura
jogo:ret_contorno(10, 10, 100, 50, 255, 255, 255, 2)  -- borda branca de 2px

-- Linha: x0, y0, x1, y1, R, G, B, espessura
jogo:linha(0, 0, 320, 240, 0, 255, 0, 1)  -- diagonal verde

-- Círculo: centro x, centro y, raio, R, G, B, preenchido
jogo:circulo(160, 120, 40, 0, 100, 255, true)   -- círculo azul preenchido
jogo:circulo(160, 120, 40, 255, 0, 0, false)    -- contorno vermelho

-- Retângulo semitransparente (overlay): x, y, w, h, R, G, B, alpha (0..1)
jogo:overlay(0, 0, 320, 240, 0, 0, 0, 0.5)   -- tela escurecida a 50%
```

### Sprite direto

```lua
-- Desenhar parte de um sprite em (x, y)
-- parâmetros: sprite_id, x_destino, y_destino, x_fonte, y_fonte, largura, altura
jogo:sprite(sid_fundo, 0, 0,  0, 0, 320, 240)   -- fundo inteiro
jogo:sprite(sid_sheet, 50, 50, 32, 0, 32, 32)   -- frame específico do sheet

-- Desenhar com opções extras
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, {
    escala_x = 2.0,    -- dobrar largura
    escala_y = 2.0,    -- dobrar altura
    rotacao  = 45,     -- 45 graus
    alpha    = 0.8,    -- 80% opaco
    fh       = true,   -- espelhar horizontal
    fv       = false,  -- sem espelhar vertical
})

-- Desenhar com cores invertidas (efeito de dano)
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, { invertido = true })
```

### Tilemap

```lua
-- Desenhar um mapa de tiles de uma vez
-- O mapa é uma tabela linear, linha por linha, com o índice do tile
local mapa = {
    1, 1, 1, 1, 1,
    1, 0, 0, 0, 1,
    1, 0, 0, 0, 1,
    1, 1, 1, 1, 1,
}

-- parâmetros: tabela, linhas, colunas, sprite_id, tw, th, offset_x, offset_y
jogo:tilemap(mapa, 4, 5, sid_tiles, 16, 16, 0, 0)
```

---

## Texto e UI

```lua
-- Texto com fonte bitmap
-- parâmetros: x, y, string, sprite_id_da_fonte, fw, fh, chars_por_linha, offset_ascii, espacamento
jogo:texto(10, 10, "Olá mundo!", sid_fonte, 8, 8)
jogo:texto(10, 10, "Pontos: " .. pontos, sid_fonte, 8, 8, 16, 32, 1)

-- Caixa decorada com tileset de bordas 3x3
-- parâmetros: x, y, largura, altura, sprite_id, tw, th
jogo:caixa(50, 50, 200, 100, sid_bordas, 8, 8)

-- Caixa de texto com título e conteúdo
jogo:caixa_texto(50, 50, 200, 100,
    "Título",
    "Texto do conteúdo aqui.",
    sid_bordas, 8, 8,       -- tileset da caixa
    sid_fonte,  8, 8)       -- fonte
```

> **Como funciona a fonte bitmap?**
> A fonte é uma imagem PNG com todos os caracteres organizados em uma grade. `chars_por_linha` diz quantos caracteres cabem por linha na imagem, e `offset_ascii` diz qual caractere começa (32 = espaço, o padrão ASCII).

---

## Efeitos visuais

```lua
-- Chuva: precisa de uma lista de gotas {x, y}
local gotas = {}
for i = 1, 50 do
    gotas[i] = {math.random(0, 320), math.random(0, 240)}
end
-- Atualizar posições das gotas no seu código...
-- parâmetros: largura, altura, frame_animação, gotas, largura_gota, altura_gota
jogo:chuva(320, 240, frame, gotas, 1, 6)

-- Overlay de noite
-- intensidade: 0 = dia claro, 1 = noite total
jogo:noite(320, 240, 0.7)  -- noite escura
```

---

## Input — Teclado

```lua
-- true ENQUANTO a tecla estiver segurada
if jogo:tecla("d") then
    jogo:mover(heroi, 3, 0)
end

-- true SOMENTE no frame em que foi pressionada (sem repetição)
if jogo:tecla_press("space") then
    pular(heroi)
end

-- true SOMENTE no frame em que foi solta
if jogo:tecla_solta("shift") then
    parar_corrida()
end
```

**Nomes comuns de teclas:**

| Tecla | Nome |
|---|---|
| Letras | `"a"`, `"b"`, ..., `"z"` |
| Números | `"0"` até `"9"` |
| Espaço | `"space"` |
| Enter | `"return"` |
| Escape | `"escape"` |
| Setas | `"up"`, `"down"`, `"left"`, `"right"` |
| Shift | `"lshift"`, `"rshift"` |
| Ctrl | `"lctrl"`, `"rctrl"` |
| F1–F12 | `"f1"`, `"f2"`, ... |

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:tecla(t)` | Nome da tecla | `true` enquanto segurada |
| `jogo:tecla_press(t)` | Nome da tecla | `true` só no frame inicial |
| `jogo:tecla_solta(t)` | Nome da tecla | `true` só ao soltar |

---

## Input — Mouse

```lua
-- Constantes de botão: jogo.ESQ, jogo.MEIO, jogo.DIR
-- (também pode usar 0, 1, 2 diretamente)

-- Posição do cursor
local mx, my = jogo:mouse_pos()

-- Botão segurado
if jogo:mouse_segurado(E.ESQ) then
    print("Botão esquerdo pressionado")
end

-- Botão pressionado (só no frame do clique)
if jogo:mouse_press(E.ESQ) then
    print("Clicou em:", mx, my)
end

-- Botão solto
if jogo:mouse_solta(E.ESQ) then
    soltar_objeto()
end

-- Scroll do mouse: +1 (cima), -1 (baixo), 0 (parado)
local scroll = jogo:mouse_scroll()
if scroll > 0 then cam_zoom = cam_zoom + 0.1 end
if scroll < 0 then cam_zoom = cam_zoom - 0.1 end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:mouse_pos()` | — | `x, y` |
| `jogo:mouse_segurado(b)` | Botão | `true` ou `false` |
| `jogo:mouse_press(b)` | Botão | `true` ou `false` |
| `jogo:mouse_solta(b)` | Botão | `true` ou `false` |
| `jogo:mouse_scroll()` | — | `-1`, `0` ou `+1` |

---

## Áudio

```lua
-- Inicializar áudio (faça logo após E.nova)
jogo:audio_init()

-- Tocar um arquivo (ogg, wav)
-- loop=false, volume=1.0 (0 a 1), pitch=1.0 (0.5 a 2.0)
local musica = jogo:tocar("musica.ogg", true)          -- música em loop
local som    = jogo:tocar("pulo.wav")                  -- efeito sonoro
local tiro   = jogo:tocar("tiro.wav", false, 0.6, 1.2) -- mais baixo e agudo

-- Controlar
jogo:pausar(musica)    -- pausa
jogo:retomar(musica)   -- retoma
jogo:parar(musica)     -- para e libera

-- Ajustar em tempo real
jogo:volume(musica, 0.3)   -- 30% do volume
jogo:pitch(musica, 0.8)    -- mais grave

-- Checar se terminou (útil para sons sem loop)
if jogo:audio_fim(som) then
    print("Som terminou")
end
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:audio_init()` | — | `true` se funcionou |
| `jogo:tocar(arquivo, loop, vol, pitch)` | Arquivo, opções | `handle` |
| `jogo:pausar(h)` | Handle | — |
| `jogo:retomar(h)` | Handle | — |
| `jogo:parar(h)` | Handle | — |
| `jogo:volume(h, v)` | Handle, 0–1 | — |
| `jogo:pitch(h, p)` | Handle, 0.5–2.0 | — |
| `jogo:audio_fim(h)` | Handle | `true` ou `false` |

---

## Shaders

Shaders são programas que rodam na GPU e modificam a aparência dos pixels em tempo real. A engine já vem com alguns prontos, e você pode criar os seus em GLSL.

### Efeitos prontos

```lua
-- Ativar efeito de escala de cinza
jogo:efeito("cinza", true)

-- Ativar dentro do loop e desativar depois de desenhar
jogo:efeito("negativo", true)
jogo:desenhar()
jogo:efeito("negativo", false)

-- Efeito de monitor CRT (scanlines + vinheta)
-- passe largura e altura da tela para melhor resultado
jogo:efeito("crt", true, 320, 240)

-- Aberração cromática (desvio RGB, como câmera ruim)
-- offset: quanto as cores se separam (0.003 = sutil, 0.01 = forte)
jogo:efeito("aberracao", true, 0.005)

-- Desativar qualquer efeito
jogo:efeito("cinza", false)
```

**Efeitos disponíveis:** `"cinza"`, `"negativo"`, `"crt"`, `"aberracao"`

### Shaders personalizados

```lua
-- Criar shader com código GLSL próprio
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

-- Usar no loop
jogo:shader_usar(meu_shader)

-- Definir uniforms (variáveis passadas para o shader)
-- detecta automaticamente: 1 valor=float, 2=vec2, 4=vec4
jogo:shader_uniform(meu_shader, "u_tint",      1.0, 0.5, 0.0, 1.0)  -- laranja
jogo:shader_uniform(meu_shader, "u_resolucao", 320, 240)             -- vec2
jogo:shader_uniform(meu_shader, "u_tempo",     jogo:tempo())         -- float

jogo:desenhar()
jogo:shader_nenhum()    -- volta ao modo padrão

-- Liberar quando não precisar mais
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

Um FBO permite desenhar em uma textura ao invés da tela. Útil para aplicar shaders no jogo inteiro ou criar efeitos avançados como reflexos, minijogos em telas dentro do jogo etc.

```lua
-- Criar framebuffer do tamanho da tela
local fbo = jogo:fbo_criar(320, 240)

-- Registrar como sprite para usar depois
local sid_fbo = jogo:fbo_como_sprite(fbo)

-- No loop: desenhar no FBO ao invés da tela
jogo:fbo_bind(fbo)
    jogo:limpar()
    jogo:desenhar()  -- tudo vai para o FBO
jogo:fbo_unbind()

-- Agora sid_fbo contém o frame renderizado
-- Aplique efeito e exiba na tela
jogo:efeito("crt", true, 320, 240)
jogo:sprite(sid_fbo, 0, 0,  0, 0, 320, 240)
jogo:efeito("crt", false)

-- Liberar quando não precisar
jogo:fbo_destruir(fbo)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fbo_criar(w, h)` | Tamanho | `fbo_handle` |
| `jogo:fbo_bind(fh)` | Handle | — |
| `jogo:fbo_unbind()` | — | — |
| `jogo:fbo_como_sprite(fh)` | Handle | `sprite_id` |
| `jogo:fbo_destruir(fh)` | Handle | — |

---

## FOV — Campo de Visão

Sistema de **Field of View** (FOV) com shadowcasting 2D: cria neblina de guerra onde o jogador só enxerga o que está no seu campo de visão.

**Modos disponíveis:**

| Modo | Valor | Comportamento |
|---|---|---|
| BASICO | `0` | Tudo escuro fora da visão atual |
| NEBLINA | `1` | Áreas visitadas ficam semi-visíveis (padrão) |
| SUAVE | `2` | Transição suave nas bordas da visão |

```lua
-- 1. Criar sessão de FOV
-- parâmetros: colunas_do_mapa, linhas_do_mapa, raio_de_visao, modo
local fov = jogo:fov_novo(20, 15, 8, 1)   -- mapa 20x15, raio 8 tiles, modo NEBLINA

-- 2. No loop, recalcular após mover o jogador
-- fov_calcular precisa de uma função que diga quais tiles bloqueiam a visão
local col_jogador = 5   -- coluna do tile onde o jogador está
local lin_jogador = 7   -- linha do tile

jogo:fov_calcular(fov, col_jogador, lin_jogador, function(col, row)
    -- retorne true se o tile nessa posição bloqueia a visão (parede, etc.)
    return mapa[row * 20 + col + 1] == PAREDE
end)

-- 3. Desenhar o mundo normalmente
jogo:limpar()
jogo:tilemap(mapa, 15, 20, sid_tiles, 16, 16, 0, 0)
jogo:desenhar()

-- 4. Desenhar a camada de sombra APÓS o mundo e ANTES da UI
-- parâmetros: fov, tw, th, offset_x, offset_y, r, g, b (cor da sombra)
jogo:fov_sombra(fov, 16, 16, 0, 0)

-- 5. Desenhar HUD/UI por cima
jogo:texto(10, 10, "HP: 100", sid_fonte, 8, 8)

-- Consultas úteis
if jogo:fov_visivel(fov, 10, 5) then
    -- tile (10,5) está visível agora
end

if jogo:fov_explorado(fov, 10, 5) then
    -- tile (10,5) já foi visto antes (modo NEBLINA)
end

-- Alterar raio de visão dinamicamente
jogo:fov_raio(fov, 5)   -- visão reduzida (tocha apagando...)
jogo:fov_raio(fov, 10)  -- visão ampliada

-- Zerar ao trocar de mapa
jogo:fov_reset(fov)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fov_novo(cols, rows, raio, modo)` | Dimensões do mapa, raio, modo | handle FOV |
| `jogo:fov_calcular(fov, col, row, fn)` | FOV, posição jogador, função parede | — |
| `jogo:fov_sombra(fov, tw, th, ox, oy, r, g, b)` | FOV, tamanho tile, offset, cor | — |
| `jogo:fov_visivel(fov, col, row)` | FOV, tile | `true` ou `false` |
| `jogo:fov_explorado(fov, col, row)` | FOV, tile | `true` ou `false` |
| `jogo:fov_raio(fov, r)` | FOV, novo raio | — |
| `jogo:fov_reset(fov)` | FOV | — |

---

## Grade Espacial

A Grade Espacial (Spatial Grid) divide o mundo em células e acelera drasticamente a checagem de colisões quando existem muitos objetos.

> **Quando usar?**
> Use a grade quando tiver **muitos objetos** (inimigos, projéteis, itens). Para menos de ~30 objetos, as funções normais de colisão já são suficientes.

```lua
-- 1. Ativar a grade antes do loop
-- cell_size: tamanho de cada célula em pixels (0 = automático)
jogo:sgrid_init(64)

-- 2. Se criar muitos objetos de uma vez, reconstruir a grade
for i = 1, 100 do
    criar_inimigo()
end
jogo:sgrid_rebuild()

-- 3. No loop: checar colisões eficientemente

-- Projetil vs primeiro inimigo que encontrar
local alvo = jogo:sgrid_primeira_colisao(projetil)
if alvo then
    causar_dano(alvo)
    jogo:remover_objeto(projetil)
end

-- Explosão vs todos os inimigos em volta
for _, oid in ipairs(jogo:sgrid_todas_colisoes(explosao)) do
    causar_dano(oid)
end

-- Verificar se a grade está ativa
if jogo:sgrid_ativo() then
    -- grade funcionando
end

-- Desativar quando não precisar mais
jogo:sgrid_destruir()
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:sgrid_init(cell_size)` | Tamanho da célula (0=auto) | — |
| `jogo:sgrid_rebuild()` | — | — |
| `jogo:sgrid_primeira_colisao(oid)` | object_id | `object_id` ou `nil` |
| `jogo:sgrid_todas_colisoes(oid, max)` | object_id, limite | tabela de IDs |
| `jogo:sgrid_ativo()` | — | `true` ou `false` |
| `jogo:sgrid_destruir()` | — | — |

---

## Exemplo completo

Um jogo mínimo com movimento, sprite, animação e colisão:

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Exemplo", 2)

jogo:audio_init()
jogo:fundo(20, 20, 30)
jogo:sgrid_init(64)

-- Assets
local sid_sheet = jogo:carregar_sprite("heroi.png")
local musica    = jogo:tocar("musica.ogg", true, 0.5)

-- Herói
local heroi = jogo:criar_objeto_tile(160, 180, sid_sheet, 0, 0, 16, 16)
jogo:hitbox(heroi, 2, 2, 12, 14)

-- Animação
local anim = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {0}, 8, true, heroi)

-- Câmera
jogo:cam_seguir(heroi, 0.15)

local vel = 80  -- pixels por segundo

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    local dt = jogo:delta()
    local movendo = false

    if jogo:tecla("d") then jogo:mover(heroi, vel*dt,     0); movendo=true end
    if jogo:tecla("a") then jogo:mover(heroi, -vel*dt,    0); movendo=true end
    if jogo:tecla("s") then jogo:mover(heroi, 0,      vel*dt); movendo=true end
    if jogo:tecla("w") then jogo:mover(heroi, 0,     -vel*dt); movendo=true end

    if movendo then
        jogo:anim_tocar(anim)
    else
        jogo:anim_parar(anim, 0, 0)
    end

    if jogo:tecla_press("f") then jogo:tela_cheia() end
    if jogo:tecla_press("escape") then break end

    jogo:limpar()
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end

jogo:destruir()
```