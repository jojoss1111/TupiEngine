# 📖 Documentação da Engine 2D

> Engine 2D em Lua feita para iniciantes. Todas as funções estão em português e organizadas por categoria.
> Este guia assume que você sabe o básico de Lua (variáveis, funções, `if`, `while`).

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
21. [Exemplo completo](#exemplo-completo)

---

## Como funciona

A engine é carregada com `require("engine")` e funciona através de um **objeto de jogo** criado com `E.nova()`. A partir daí, todas as funções são chamadas nesse objeto usando `:`.

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)
--                  ^largura ^altura  ^título ^escala da janela
```

O parâmetro `escala` faz a janela ser maior sem aumentar a resolução interna. Com `escala = 2`, uma tela de 320×240 é exibida como uma janela de 640×480 — pixels bem grandes, estilo retrô.

> **Por que usar `:`?**
> O `:` é a forma do Lua de chamar funções que pertencem a um objeto.
> `jogo:limpar()` é a mesma coisa que escrever `jogo.limpar(jogo)`.
> Você só precisa saber que, para usar qualquer função desta engine, é sempre `jogo:nome_da_funcao(...)`.

---

## Loop principal

Todo jogo roda em volta de um **loop**: um `while` que repete dezenas de vezes por segundo. Cada repetição é chamada de **frame**. Dentro desse loop, a ordem das chamadas importa — siga sempre esta sequência:

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)

jogo:fundo(30, 30, 40)  -- define a cor de fundo (cinza-azulado)

while jogo:rodando() do
    jogo:eventos()    -- 1º: lê o teclado, mouse e se a janela foi fechada
    jogo:atualizar()  -- 2º: atualiza câmera, animações, partículas e áudio
    jogo:limpar()     -- 3º: apaga o que foi desenhado no frame anterior

    -- 4º: aqui vai o seu código (mover personagem, checar colisões etc.)

    jogo:desenhar()   -- 5º: desenha todos os objetos da cena
    jogo:apresentar() -- 6º: envia o frame pronto para a tela
    jogo:fps(60)      -- 7º: pausa para não passar de 60 quadros por segundo
end

jogo:destruir()  -- libera a memória e fecha a janela ao sair do loop
```

> **Por que a ordem importa?**
> Se você chamar `jogo:limpar()` depois de `jogo:desenhar()`, a tela vai apagar tudo que acabou de ser desenhado e mostrar apenas a cor de fundo. Sempre limpe *antes* de desenhar.

### Funções do loop

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
| `jogo:tempo()` | Retorna os segundos desde o início do jogo |
| `jogo:delta()` | Retorna a duração do último frame em segundos |

### O que é delta time?

`jogo:delta()` retorna o tempo que o último frame levou para rodar, em segundos. Normalmente é um número muito pequeno, como `0.016` (que equivale a 1/60 de segundo).

**Por que isso é útil?** Se você move um personagem `3` pixels por frame, ele vai se mover mais rápido num computador que roda a 120 fps do que num que roda a 30 fps. Usando delta time, o personagem se move a uma velocidade em *pixels por segundo*, igual em qualquer máquina:

```lua
local velocidade = 100  -- pixels por segundo

-- Sem delta time (velocidade depende do FPS — ruim):
jogo:mover(heroi, 3, 0)

-- Com delta time (velocidade constante — correto):
jogo:mover(heroi, velocidade * jogo:delta(), 0)
```

---

## Janela

```lua
-- Define a cor de fundo em RGB (cada valor de 0 a 255)
jogo:fundo(0,   0,   0  )  -- preto
jogo:fundo(135, 206, 235)  -- azul céu
jogo:fundo(34,  139, 34 )  -- verde floresta

-- Alterna entre janela normal e tela cheia
jogo:tela_cheia()

-- Retorna o tamanho da área de desenho em pixels lógicos
-- (não confundir com o tamanho da janela, que é multiplicado pela escala)
local largura, altura = jogo:tamanho()
print(largura, altura)  --> 320   240
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fundo(r, g, b)` | Vermelho, verde, azul (0–255) | — |
| `jogo:tela_cheia()` | — | — |
| `jogo:tamanho()` | — | `largura, altura` |

---

## Sprites

Um **sprite** é uma imagem PNG carregada na memória da GPU. Quando você carrega um sprite, recebe um número chamado **sprite_id** — é como o "nome" da imagem dentro da engine. Você vai usar esse número em quase tudo: criar objetos, desenhar, animar.

```lua
-- Carrega uma imagem PNG inteira
local sid_heroi = jogo:carregar_sprite("imagens/heroi.png")
local sid_fundo = jogo:carregar_sprite("imagens/fundo.png")

-- Carrega apenas um recorte de uma imagem (spritesheet)
-- Útil quando vários sprites estão numa imagem só
-- parâmetros: arquivo,  x_na_imagem, y_na_imagem, largura, altura
local sid_frame0 = jogo:carregar_regiao("sheet.png",  0, 0, 32, 32)  -- 1º frame
local sid_frame1 = jogo:carregar_regiao("sheet.png", 32, 0, 32, 32)  -- 2º frame
```

> **Atenção:** Se o arquivo não existir, a função retorna `-1`. É uma boa prática verificar:
> ```lua
> local sid = jogo:carregar_sprite("heroi.png")
> if sid == -1 then
>     error("Arquivo 'heroi.png' não encontrado!")
> end
> ```

> **O que é uma spritesheet?**
> É uma imagem única que contém vários sprites organizados em uma grade. Em vez de carregar dezenas de arquivos separados, você carrega uma imagem só e recorta as partes que precisa. Muito mais eficiente!

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:carregar_sprite(caminho)` | Caminho do arquivo PNG | `sprite_id` ou `-1` se falhou |
| `jogo:carregar_regiao(caminho, x, y, w, h)` | Arquivo + posição e tamanho do recorte | `sprite_id` ou `-1` |

---

## Objetos

**Objetos** são as entidades visíveis da cena: o personagem, os inimigos, plataformas, itens. Cada objeto criado recebe um número chamado **object_id**, que você guarda numa variável para controlar o objeto depois.

### Criar e remover

```lua
-- Criar objeto com um sprite inteiro
-- parâmetros: x, y, sprite_id, largura, altura
local heroi   = jogo:criar_objeto(100, 200, sid_heroi,   32, 32)
local inimigo = jogo:criar_objeto(300, 200, sid_inimigo, 32, 32)

-- Criar objeto usando um tile (célula) de uma spritesheet
-- parâmetros: x, y, sprite_id, coluna_do_tile, linha_do_tile, largura_tile, altura_tile
-- Exemplo: tile na coluna 3, linha 0, cada tile tem 16x16 pixels
local moeda  = jogo:criar_objeto_tile(64,  128, sid_tiles, 3, 0, 16, 16)
local pedra  = jogo:criar_objeto_tile(200, 128, sid_tiles, 1, 2, 16, 16)

-- Remover objeto da cena (some da tela e deixa de existir para colisões)
jogo:remover_objeto(inimigo)
```

### Posição e movimento

```lua
-- Mover por deslocamento relativo (quanto mover a partir de onde está)
jogo:mover(heroi,  2,  0)   -- anda 2 pixels para a direita
jogo:mover(heroi, -2,  0)   -- anda 2 pixels para a esquerda
jogo:mover(heroi,  0, -3)   -- sobe 3 pixels (Y cresce para baixo!)
jogo:mover(heroi,  0,  3)   -- desce 3 pixels

-- Posicionar em uma coordenada exata (teleporte)
jogo:posicionar(heroi, 50, 100)

-- Ler a posição atual do objeto
local px, py = jogo:posicao(heroi)
print("Herói está em:", px, py)
```

> **Atenção ao eixo Y!** Na engine, o eixo Y cresce **para baixo**. A posição (0, 0) fica no **canto superior esquerdo** da tela. Para subir, use `dy` negativo; para descer, `dy` positivo.

### Aparência

```lua
-- Trocar o sprite do objeto por um sprite diferente
jogo:objeto_sprite(heroi, sid_heroi_correndo)

-- Trocar qual tile da spritesheet o objeto mostra
-- parâmetros: object_id, coluna, linha
jogo:objeto_tile(heroi, 2, 1)  -- vai para a coluna 2, linha 1

-- Espelhar o sprite (útil para personagens que viram de lado)
-- parâmetros: object_id, espelhar_horizontal, espelhar_vertical
jogo:espelhar(heroi, true,  false)  -- vira para a esquerda
jogo:espelhar(heroi, false, false)  -- volta ao normal

-- Alterar o tamanho do sprite (1.0 = tamanho original)
jogo:escala(heroi, 2.0, 2.0)  -- dobra o tamanho
jogo:escala(heroi, 0.5, 0.5)  -- metade do tamanho
jogo:escala(heroi, 1.5, 1.0)  -- mais largo, mesma altura

-- Rotacionar em graus (sentido horário, pivô no centro)
jogo:rotacao(heroi, 45)   -- 45° inclinado
jogo:rotacao(heroi, 0)    -- sem rotação

-- Transparência: 0.0 = invisível, 1.0 = totalmente opaco
jogo:transparencia(heroi, 0.5)  -- 50% transparente (fantasma)
jogo:transparencia(heroi, 1.0)  -- opaco (normal)

-- Definir em qual camada o objeto é desenhado
-- objetos em camadas maiores aparecem na frente
jogo:camada(heroi,  2, 0)  -- camada 2 (na frente)
jogo:camada(fundo,  0, 0)  -- camada 0 (atrás de tudo)
```

### Hitbox

A **hitbox** é o retângulo invisível usado para detectar colisões. Por padrão ela cobre o sprite inteiro, mas para jogos mais precisos é comum definir uma hitbox menor (especialmente se o sprite tiver bordas transparentes).

```lua
-- parâmetros: object_id, offset_x, offset_y, largura, altura
-- offset_x/y é o deslocamento a partir do canto superior esquerdo do sprite

-- Exemplo: sprite 32x32, hitbox centralizada de 20x28
-- offset (6, 2) empurra o retângulo 6px para dentro na horizontal e 2px para baixo
jogo:hitbox(heroi, 6, 2, 20, 28)

-- Para um personagem de 16x16 com hitbox apertada:
jogo:hitbox(inimigo, 2, 2, 12, 13)
```

### Referência — Objetos

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_objeto(x, y, sid, w, h)` | Posição, sprite, tamanho | `object_id` |
| `jogo:criar_objeto_tile(x, y, sid, tx, ty, tw, th)` | Posição, sprite, coluna/linha do tile, tamanho do tile | `object_id` |
| `jogo:remover_objeto(oid)` | ID do objeto | — |
| `jogo:mover(oid, dx, dy)` | ID, deslocamento X e Y | — |
| `jogo:posicionar(oid, x, y)` | ID, posição exata | — |
| `jogo:posicao(oid)` | ID | `x, y` |
| `jogo:objeto_sprite(oid, sid)` | ID, sprite_id | — |
| `jogo:objeto_tile(oid, coluna, linha)` | ID, coluna, linha | — |
| `jogo:espelhar(oid, horizontal, vertical)` | ID, bool, bool | — |
| `jogo:escala(oid, sx, sy)` | ID, escala X, escala Y | — |
| `jogo:rotacao(oid, graus)` | ID, ângulo em graus | — |
| `jogo:transparencia(oid, alpha)` | ID, valor 0–1 | — |
| `jogo:camada(oid, layer, z)` | ID, índice da camada, ordem Z | — |
| `jogo:hitbox(oid, offset_x, offset_y, w, h)` | ID, offset, tamanho da hitbox | — |

---

## Colisão

Colisão verifica se dois objetos estão se tocando usando suas hitboxes (ou o bounding-box do sprite, se nenhuma hitbox foi definida).

```lua
-- Colisão entre dois objetos
if jogo:colidem(heroi, inimigo) then
    print("Herói foi atingido!")
    -- rebater o herói, tirar vida, etc.
end

-- Colisão com um retângulo fixo (bom para chão, paredes, armadilhas)
-- parâmetros: object_id, x, y, largura, altura  do retângulo
if jogo:colide_ret(heroi, 0, 230, 320, 10) then
    print("Herói tocou o chão!")
end

-- Colisão com um ponto (útil para saber se o mouse está sobre um objeto)
local mx, my = jogo:mouse_pos()
if jogo:colide_ponto(botao, mx, my) then
    print("Mouse em cima do botão!")
end
```

> **Dica de performance:** As funções acima checam um par de objetos por vez. Se você tem dezenas ou centenas de inimigos e projéteis ao mesmo tempo, use a [Grade Espacial](#grade-espacial) — ela é muito mais rápida para muitos objetos.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:colidem(oid1, oid2)` | Dois object_ids | `true` ou `false` |
| `jogo:colide_ret(oid, rx, ry, rw, rh)` | Objeto + retângulo | `true` ou `false` |
| `jogo:colide_ponto(oid, px, py)` | Objeto + ponto | `true` ou `false` |

---

## Câmera

A câmera controla qual parte do mundo está visível na tela. Por padrão ela fica parada em (0, 0), mostrando apenas o canto superior esquerdo do mundo.

```lua
-- Posicionar a câmera em um ponto exato do mundo
jogo:cam_pos(500, 300)

-- Fazer a câmera seguir um objeto suavemente
-- lerp: 0.0 = câmera parada, 1.0 = instantâneo, valores baixos = suave
jogo:cam_seguir(heroi, 0.1)   -- transição bem suave
jogo:cam_seguir(heroi, 0.3)   -- um pouco mais rápida
jogo:cam_seguir(heroi, 1.0)   -- cola imediatamente no herói

-- Zoom: 1.0 = normal, 2.0 = aproximado (tudo maior), 0.5 = afastado (tudo menor)
jogo:cam_zoom(1.5)   -- levemente aproximado
jogo:cam_zoom(0.5)   -- visão de cima, tudo pequeno

-- Efeito de tremor de câmera (impacto, explosão, terremoto)
-- parâmetros: intensidade em pixels, duração em segundos
jogo:cam_tremor(4, 0.2)   -- tremor leve (tiro)
jogo:cam_tremor(10, 0.5)  -- tremor forte (explosão)

-- Converter posição do mundo para posição na tela (e vice-versa)
-- Útil para posicionar UI em cima de objetos do mundo
local sx, sy = jogo:mundo_para_tela(heroi_x, heroi_y)
local wx, wy = jogo:tela_para_mundo(mouse_x, mouse_y)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:cam_pos(x, y)` | Posição no mundo | — |
| `jogo:cam_seguir(oid, lerp)` | Objeto alvo, suavidade 0–1 | — |
| `jogo:cam_zoom(z)` | Fator de zoom | — |
| `jogo:cam_tremor(intensidade, duracao)` | Pixels de tremor, duração | — |
| `jogo:mundo_para_tela(wx, wy)` | Posição no mundo | `sx, sy` na tela |
| `jogo:tela_para_mundo(sx, sy)` | Posição na tela | `wx, wy` no mundo |

---

## Partículas

Partículas são pequenos elementos visuais usados para efeitos: fogo, fumaça, faíscas, chuva de moedas, sangue, etc. Você cria um **emissor** que configura como as partículas se comportam, e depois o aciona quando precisar.

```lua
-- Criar um emissor de partículas (normalmente antes do loop)
local fagulhas = jogo:criar_emissor({
    x = 160, y = 120,  -- posição inicial do emissor no mundo

    -- Velocidade das partículas ao nascer (valores aleatórios dentro do intervalo)
    vel = {-40, 40,    -- vx: entre -40 e +40 (espalha para os lados)
           -80, -20},  -- vy: entre -80 e -20 (sempre sobe)

    -- Gravidade aplicada a cada partícula por segundo
    grav = {0, 120},   -- puxa para baixo (positivo = para baixo)

    -- Tempo de vida de cada partícula, em segundos (valor aleatório)
    vida = {0.3, 1.0},

    -- Tamanho: começa em 5 pixels e vai encolhendo até 0 ao morrer
    tamanho = {5, 0},

    -- Cor em RGBA, valores de 0.0 a 1.0 (não 0 a 255!)
    cor       = {1.0, 0.5, 0.0, 1.0},  -- laranja opaco ao nascer
    cor_final = {1.0, 0.0, 0.0, 0.0},  -- vermelho transparente ao morrer

    rate = 0,    -- partículas por segundo em modo contínuo (0 = só burst)
    max  = 100,  -- máximo de partículas vivas ao mesmo tempo
})

-- Disparar 30 partículas de uma vez (explosão instantânea)
jogo:burst(fagulhas, 30)

-- Mover o ponto de emissão (ex: seguir o herói, como uma tocha)
local px, py = jogo:posicao(heroi)
jogo:emissor_pos(fagulhas, px, py)

-- Remover o emissor quando não precisar mais
jogo:remover_emissor(fagulhas)
```

> `jogo:desenhar()` já inclui as partículas automaticamente — não precisa chamar nada extra para elas aparecerem.

> **Sobre as cores:** As cores das partículas usam valores de `0.0` a `1.0`, não de `0` a `255`. Para converter: divida por 255. Por exemplo, vermelho `(255, 0, 0)` vira `(1.0, 0.0, 0.0)`.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_emissor(cfg)` | Tabela de configuração | `emitter_id` |
| `jogo:burst(eid, n)` | ID do emissor, quantidade de partículas | — |
| `jogo:emissor_pos(eid, x, y)` | ID, nova posição | — |
| `jogo:remover_emissor(eid)` | ID do emissor | — |

---

## Animação

Animações percorrem automaticamente os frames de uma spritesheet. O sistema garante que **apenas uma animação toca por objeto** ao mesmo tempo — chamar `anim_tocar()` em uma nova animação pausa automaticamente a anterior.

### Como uma spritesheet é organizada

Uma spritesheet é uma grade de frames. Cada frame tem uma **coluna** e uma **linha**:

```
Coluna:   0       1       2       3       4
        +-------+-------+-------+-------+-------+
Linha 0 | idle  | idle  | correr| correr| correr|
        +-------+-------+-------+-------+-------+
Linha 1 | atk 0 | atk 1 | atk 2 |  ...  |  ...  |
        +-------+-------+-------+-------+-------+
```

Você passa as **colunas** dos frames que quer animar e a **linha** onde eles estão.

### Criando e usando animações

```lua
local sid_sheet = jogo:carregar_sprite("heroi_sheet.png")
local heroi     = jogo:criar_objeto_tile(100, 100, sid_sheet, 0, 0, 32, 32)

-- Animação de correr: 4 frames nas colunas 0,1,2,3 da linha 0
local anim_correr = jogo:criar_animacao(
    sid_sheet,      -- spritesheet
    32, 32,         -- tamanho de cada tile em pixels
    {0, 1, 2, 3},   -- colunas dos frames
    {0},            -- linha onde estão os frames (linha 0)
    8,              -- velocidade: 8 frames por segundo
    true,           -- loop: repete quando chegar no fim
    heroi           -- objeto que vai ser animado
)

-- Animação de ataque: 3 frames na linha 1, sem loop (toca uma vez)
local anim_atacar = jogo:criar_animacao(
    sid_sheet, 32, 32,
    {0, 1, 2},  -- frames do ataque
    {1},        -- linha 1
    12,         -- mais rápido que o correr
    false,      -- sem loop (one-shot)
    heroi
)

-- Animação idle com frames em linhas diferentes
local anim_idle = jogo:criar_animacao(
    sid_sheet, 32, 32,
    {0, 1},  -- colunas
    {0, 2},  -- coluna 0 na linha 0, coluna 1 na linha 2
    4,
    true, heroi
)
```

### Controlando as animações no loop

```lua
while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    if jogo:tecla("a") or jogo:tecla("d") then
        -- anim_tocar para a animação anterior automaticamente
        jogo:anim_tocar(anim_correr)

    elseif jogo:tecla_press("j") then
        jogo:anim_tocar(anim_atacar)  -- interrompe o correr e ataca

    else
        -- Parar a animação ativa e mostrar um frame fixo (idle)
        -- anim_atual() retorna qual animação está tocando agora
        local ativa = jogo:anim_atual(heroi)
        if ativa then
            jogo:anim_parar(ativa, 0, 0)  -- para no tile (col 0, linha 0)
        end
    end

    -- Detectar quando uma animação one-shot terminou
    if jogo:anim_fim(anim_atacar) then
        jogo:anim_tocar(anim_correr)  -- volta a correr ao terminar o ataque
    end

    jogo:limpar()
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end

-- Liberar da memória quando não precisar mais
jogo:anim_destruir(anim_correr)
jogo:anim_destruir(anim_atacar)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:criar_animacao(sid, tw, th, cols, lins, fps, loop, oid)` | Ver exemplo acima | handle de animação |
| `jogo:anim_tocar(anim)` | Handle da animação | — |
| `jogo:anim_parar(anim, col, lin)` | Handle + frame opcional para exibir | — |
| `jogo:anim_atual(oid)` | object_id | handle da animação ativa, ou `nil` |
| `jogo:anim_fim(anim)` | Handle | `true` se terminou (só para one-shot) |
| `jogo:anim_destruir(anim)` | Handle | — |

---

## Fade

Fade é uma transição suave onde a tela **escurece** (fade out) ou **clareia** (fade in). Muito usado ao trocar de fase, abrir o jogo, ou mostrar uma morte.

```lua
-- Fade out: escurece a tela gradualmente
-- parâmetros: alvo (1=escuro, 0=claro), velocidade, cor R, G, B
jogo:fade(1, 2)               -- escurece para preto em ~0.5 segundo
jogo:fade(1, 1, 0, 0, 80)    -- escurece para azul escuro mais devagar

-- Fade in: clareia a tela
jogo:fade(0, 2)   -- clareia (desfaz o escurecimento)

-- Checar se o fade terminou (útil para saber quando trocar a fase)
if jogo:fade_ok() then
    -- a transição acabou, pode agir aqui
end
```

**Exemplo prático — troca de fase com fade:**

```lua
local fase     = 1
local trocando = false

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    -- Ao pressionar Enter, começa a escurecer
    if not trocando and jogo:tecla_press("return") then
        jogo:fade(1, 3)   -- fade out
        trocando = true
    end

    -- Quando escureceu completamente, troca a fase e clareia
    if trocando and jogo:fade_ok() then
        fase = fase + 1
        carregar_fase(fase)   -- sua função que carrega a próxima fase
        jogo:fade(0, 3)       -- fade in
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
| `jogo:fade_ok()` | — | `true` quando terminou |

---

## Desenho direto

Funções para desenhar formas geométricas e sprites diretamente na tela, sem precisar criar objetos. Útil para UI, debug, efeitos de fundo etc.

### Formas geométricas

```lua
-- Retângulo preenchido: x, y, largura, altura, R, G, B
jogo:ret(10, 10, 100, 50, 255,   0,   0)  -- vermelho
jogo:ret(10, 70, 100, 50,   0, 255,   0)  -- verde
jogo:ret(10,130, 100, 50,   0,   0, 255)  -- azul

-- Contorno de retângulo (borda, sem preenchimento)
-- parâmetros iguais + espessura da borda em pixels
jogo:ret_contorno(10, 10, 100, 50, 255, 255, 255, 1)  -- borda branca fina
jogo:ret_contorno(10, 10, 100, 50, 255, 255,   0, 3)  -- borda amarela grossa

-- Linha entre dois pontos: x0, y0, x1, y1, R, G, B, espessura
jogo:linha(  0,   0, 320, 240, 255, 255, 255, 1)  -- diagonal branca fina
jogo:linha(160,   0, 160, 240,   0, 255,   0, 2)  -- linha verde vertical

-- Círculo: centro_x, centro_y, raio, R, G, B, preenchido (true/false)
jogo:circulo(160, 120, 40,   0, 100, 255, true)   -- círculo azul cheio
jogo:circulo(160, 120, 40, 255,   0,   0, false)   -- só o contorno vermelho

-- Retângulo semitransparente (bom para escurecer partes da tela)
-- parâmetros: x, y, w, h, R, G, B, alpha (0.0 a 1.0)
jogo:overlay(0, 0, 320, 240, 0, 0, 0, 0.5)   -- tela toda escurecida a 50%
jogo:overlay(0, 0, 160, 240, 0, 0, 0, 0.3)   -- metade esquerda escurecida
```

### Desenhando sprites diretamente

```lua
-- Desenhar uma parte de um sprite em (x, y) na tela
-- parâmetros: sprite_id, x_destino, y_destino, x_fonte, y_fonte, largura, altura
jogo:sprite(sid_fundo,  0,  0,   0, 0, 320, 240)  -- fundo inteiro
jogo:sprite(sid_sheet, 50, 50,  32, 0,  32,  32)  -- 2º frame de uma sheet (offset 32)

-- Desenhar com transformações extras (tabela de opções)
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, {
    escala_x = 2.0,   -- dobra a largura
    escala_y = 2.0,   -- dobra a altura
    rotacao  = 45,    -- 45 graus inclinado
    alpha    = 0.8,   -- 80% opaco
    fh       = true,  -- espelhado na horizontal
    fv       = false, -- sem espelhar vertical
})

-- Desenhar com cores invertidas (efeito de dano / flash branco)
jogo:sprite(sid_heroi, 50, 100, 0, 0, 32, 32, { invertido = true })
```

### Tilemap

Permite desenhar um mapa de tiles inteiro de uma vez, sem criar objetos para cada tile. Mais eficiente para mapas grandes.

```lua
-- O mapa é uma tabela linear (lida linha por linha, da esquerda para direita)
-- Cada número é o índice do tile na spritesheet (0 = primeiro tile)
local mapa = {
    1, 1, 1, 1, 1,   -- linha 0: parede
    1, 0, 0, 0, 1,   -- linha 1: parede-vazio-vazio-vazio-parede
    1, 0, 0, 0, 1,   -- linha 2
    1, 1, 1, 1, 1,   -- linha 3: parede
}

-- parâmetros: tabela, n_linhas, n_colunas, sprite_id, largura_tile, altura_tile, offset_x, offset_y
jogo:tilemap(mapa, 4, 5, sid_tiles, 16, 16, 0, 0)

-- Com offset (útil quando a câmera se move)
local cam_x, cam_y = 0, 0
jogo:tilemap(mapa, 4, 5, sid_tiles, 16, 16, -cam_x, -cam_y)
```

---

## Texto e UI

Para mostrar texto, você precisa de uma **fonte bitmap**: uma imagem PNG com todos os caracteres organizados em uma grade.

```lua
-- Texto simples (usa configurações padrão: 16 chars/linha, ASCII offset 32)
-- parâmetros: x, y, string, sprite_id_da_fonte, largura_char, altura_char
jogo:texto(10, 10, "Olá mundo!",       sid_fonte, 8, 8)
jogo:texto(10, 10, "Pontos: " .. pts,  sid_fonte, 8, 8)

-- Com mais controle:
-- parâmetros completos: x, y, texto, sid, fw, fh, chars_por_linha, offset_ascii, espacamento
jogo:texto(10, 10, "Texto aqui", sid_fonte, 8, 8,
    16,  -- chars por linha na imagem da fonte
    32,  -- offset ASCII (32 = espaço, o padrão)
    1)   -- 1 pixel extra entre caracteres

-- Caixa decorada com bordas (usando um tileset 3x3 de cantos/bordas)
-- parâmetros: x, y, largura, altura, sprite_id, largura_tile, altura_tile
jogo:caixa(50, 50, 200, 100, sid_bordas, 8, 8)

-- Caixa de texto com título e conteúdo
jogo:caixa_texto(
    50, 50, 200, 100,          -- posição e tamanho da caixa
    "Item encontrado!",        -- título
    "Você pegou uma espada.",  -- conteúdo
    sid_bordas, 8, 8,          -- tileset da borda
    sid_fonte,  8, 8           -- fonte
)
```

> **Como funciona a fonte bitmap?**
> A fonte é uma imagem com todos os caracteres em uma grade. `chars_por_linha` diz quantos cabem por linha na imagem. `offset_ascii` diz qual é o código ASCII do primeiro caractere na imagem (o padrão `32` corresponde ao espaço, que é o início dos caracteres visíveis no ASCII).

---

## Efeitos visuais

### Chuva e noite

```lua
-- Efeito de chuva
-- Você gerencia as posições das gotas e as passa para a engine a cada frame
local gotas = {}
for i = 1, 50 do
    gotas[i] = {math.random(0, 320), math.random(0, 240)}
end

-- No loop: atualize as posições das gotas e chame:
-- parâmetros: largura_tela, altura_tela, frame, lista_de_gotas, largura_gota, altura_gota
jogo:chuva(320, 240, frame, gotas, 1, 6)

-- Overlay de noite (escurecimento com vinheta)
-- intensidade: 0.0 = dia normal, 1.0 = noite total
jogo:noite(320, 240, 0.0)   -- dia
jogo:noite(320, 240, 0.5)   -- entardecer
jogo:noite(320, 240, 0.85)  -- noite escura
```

---

## Input — Teclado

Existem três tipos de verificação de tecla, para situações diferentes:

```lua
-- jogo:tecla()      → true ENQUANTO a tecla estiver segurada (movimento contínuo)
-- jogo:tecla_press()→ true SÓ no frame em que foi pressionada (ação única, como pulo)
-- jogo:tecla_solta()→ true SÓ no frame em que foi solta

-- Movimento contínuo: enquanto segurar, o herói anda
if jogo:tecla("d") then jogo:mover(heroi,  3, 0) end
if jogo:tecla("a") then jogo:mover(heroi, -3, 0) end
if jogo:tecla("w") then jogo:mover(heroi,  0,-3) end
if jogo:tecla("s") then jogo:mover(heroi,  0, 3) end

-- Ação única: o pulo só acontece uma vez por pressionamento
if jogo:tecla_press("space") then
    pular(heroi)
end

-- Detectar quando a tecla foi solta
if jogo:tecla_solta("lshift") then
    parar_corrida(heroi)
end

-- Combinações de tecla
if jogo:tecla("lctrl") and jogo:tecla_press("s") then
    salvar_jogo()
end
```

**Nomes das teclas:**

| Tecla | Nome a usar |
|---|---|
| Letras | `"a"`, `"b"`, ..., `"z"` |
| Números | `"0"`, `"1"`, ..., `"9"` |
| Espaço | `"space"` |
| Enter | `"return"` |
| Escape | `"escape"` |
| Setas | `"up"`, `"down"`, `"left"`, `"right"` |
| Shift esq./dir. | `"lshift"`, `"rshift"` |
| Ctrl esq./dir. | `"lctrl"`, `"rctrl"` |
| F1 a F12 | `"f1"`, `"f2"`, ..., `"f12"` |
| Backspace | `"backspace"` |
| Tab | `"tab"` |

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:tecla(t)` | Nome da tecla | `true` enquanto segurada |
| `jogo:tecla_press(t)` | Nome da tecla | `true` só no frame inicial |
| `jogo:tecla_solta(t)` | Nome da tecla | `true` só ao soltar |

---

## Input — Mouse

```lua
-- Constantes de botão disponíveis: E.ESQ (0), E.MEIO (1), E.DIR (2)

-- Posição atual do cursor na tela
local mx, my = jogo:mouse_pos()

-- Botão segurado (enquanto mantém pressionado)
if jogo:mouse_segurado(E.ESQ) then
    arrastar_objeto(mx, my)
end

-- Botão pressionado (só no frame do clique — não repete)
if jogo:mouse_press(E.ESQ) then
    print("Clicou em:", mx, my)
    verificar_botoes_ui(mx, my)
end

-- Botão solto (só no frame em que soltou)
if jogo:mouse_solta(E.ESQ) then
    soltar_objeto()
end

-- Scroll do mouse: +1 (rolou para cima), -1 (para baixo), 0 (parado)
local scroll = jogo:mouse_scroll()
if scroll > 0 then zoom = zoom + 0.1 end
if scroll < 0 then zoom = zoom - 0.1 end
jogo:cam_zoom(zoom)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:mouse_pos()` | — | `x, y` do cursor |
| `jogo:mouse_segurado(botao)` | `E.ESQ`, `E.MEIO` ou `E.DIR` | `true` ou `false` |
| `jogo:mouse_press(botao)` | Botão | `true` só no frame do clique |
| `jogo:mouse_solta(botao)` | Botão | `true` só ao soltar |
| `jogo:mouse_scroll()` | — | `-1`, `0` ou `+1` |

---

## Áudio

```lua
-- Inicializar o subsistema de áudio (faça uma vez, logo após E.nova)
local ok = jogo:audio_init()
if not ok then print("Áudio não disponível") end

-- Tocar um arquivo de áudio (.ogg ou .wav)
-- parâmetros: arquivo, loop, volume (0–1), pitch (0.5–2.0)
local musica = jogo:tocar("musica.ogg", true)            -- loop, volume padrão
local pulo   = jogo:tocar("pulo.wav",   false)           -- efeito sem loop
local tiro   = jogo:tocar("tiro.wav",   false, 0.6, 1.2) -- mais baixo e agudo

-- Controlar a reprodução
jogo:pausar(musica)   -- pausa (pode retomar depois)
jogo:retomar(musica)  -- retoma de onde parou
jogo:parar(musica)    -- para e libera o slot (não pode retomar)

-- Ajustar volume e pitch em tempo real
jogo:volume(musica, 0.3)   -- reduz para 30% do volume
jogo:volume(musica, 1.0)   -- volta ao volume normal
jogo:pitch(musica, 0.8)    -- mais grave
jogo:pitch(musica, 1.5)    -- mais agudo

-- Verificar se o som terminou (útil para sons sem loop)
if jogo:audio_fim(pulo) then
    -- o som de pulo acabou
end
```

> **Sobre pitch:** `1.0` é o tom normal. Valores abaixo de `1.0` deixam mais grave e lento; acima de `1.0` deixam mais agudo e rápido.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:audio_init()` | — | `true` se inicializou com sucesso |
| `jogo:tocar(arquivo, loop, vol, pitch)` | Arquivo, opções | `handle` de áudio |
| `jogo:pausar(h)` | Handle | — |
| `jogo:retomar(h)` | Handle | — |
| `jogo:parar(h)` | Handle | — |
| `jogo:volume(h, v)` | Handle, valor 0–1 | — |
| `jogo:pitch(h, p)` | Handle, valor 0.5–2.0 | — |
| `jogo:audio_fim(h)` | Handle | `true` se terminou |

---

## Shaders

Shaders são programas que rodam na GPU e modificam a aparência visual em tempo real — efeitos de tela inteira como escala de cinza, negativo, scanlines de CRT, aberração cromática, e qualquer efeito personalizado que você escrever em GLSL.

### Efeitos prontos

```lua
-- Ativar escala de cinza (preto e branco)
jogo:efeito("cinza", true)
jogo:desenhar()
jogo:efeito("cinza", false)  -- desativa depois de desenhar

-- Negativo (cores invertidas)
jogo:efeito("negativo", true)
jogo:desenhar()
jogo:efeito("negativo", false)

-- Efeito de monitor CRT (scanlines + vinheta + curvatura sutil)
-- passe a resolução da tela para melhor resultado
jogo:efeito("crt", true, 320, 240)
jogo:desenhar()
jogo:efeito("crt", false)

-- Aberração cromática (canais RGB levemente deslocados, como câmera velha)
-- offset: 0.003 = sutil, 0.010 = forte, 0.020 = extremo
jogo:efeito("aberracao", true, 0.005)
jogo:desenhar()
jogo:efeito("aberracao", false)
```

**Efeitos disponíveis:** `"cinza"`, `"negativo"`, `"crt"`, `"aberracao"`

### Shaders personalizados (GLSL)

```lua
-- Criar um shader com código GLSL próprio
-- E.VERT_PADRAO é o vertex shader padrão da engine (use sempre este)
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

-- No loop: ativar o shader antes de desenhar
jogo:shader_usar(meu_shader)

-- Passar variáveis para o shader (uniforms)
-- A função detecta o tipo automaticamente pelo número de valores:
--   1 valor  → float
--   2 valores → vec2
--   4 valores → vec4
jogo:shader_uniform(meu_shader, "u_tint",      1.0, 0.5, 0.0, 1.0)  -- laranja
jogo:shader_uniform(meu_shader, "u_resolucao", 320.0, 240.0)         -- vec2
jogo:shader_uniform(meu_shader, "u_tempo",     jogo:tempo())         -- float

jogo:desenhar()
jogo:shader_nenhum()    -- volta ao modo de desenho padrão

-- Liberar o shader quando não precisar mais
jogo:shader_destruir(meu_shader)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:efeito(nome, ativar, ...)` | Nome do efeito, bool, args opcionais | — |
| `jogo:shader_criar(vert, frag)` | Código GLSL do vertex e fragment shader | `shader_handle` |
| `jogo:shader_usar(sh)` | Handle | — |
| `jogo:shader_nenhum()` | — | — |
| `jogo:shader_uniform(sh, nome, ...)` | Handle, nome da variável, valores | — |
| `jogo:shader_destruir(sh)` | Handle | — |

---

## FBO (Framebuffer)

Um **FBO** (Framebuffer Object) permite desenhar em uma textura em vez da tela. Isso abre possibilidades avançadas: aplicar um shader no jogo inteiro, criar uma tela dentro do jogo (televisão, espelho), fazer efeitos de pós-processamento etc.

**Fluxo de uso:** criar FBO → registrar como sprite → no loop: bind → desenhar → unbind → usar o sprite do FBO como quiser.

```lua
-- Criar um framebuffer do mesmo tamanho da tela
local fbo = jogo:fbo_criar(320, 240)

-- Registrar o FBO como um sprite para poder desenhá-lo depois
local sid_fbo = jogo:fbo_como_sprite(fbo)

-- No loop: redirecionar o desenho para o FBO (em vez da tela)
jogo:fbo_bind(fbo)
    jogo:limpar()     -- limpa o framebuffer
    jogo:desenhar()   -- tudo vai para o FBO, não aparece na tela ainda
jogo:fbo_unbind()     -- volta a desenhar na tela normal

-- Agora sid_fbo contém tudo que foi desenhado
-- Você pode aplicar um shader e exibir na tela:
jogo:efeito("crt", true, 320, 240)
jogo:sprite(sid_fbo, 0, 0,  0, 0, 320, 240)  -- exibe o FBO na tela
jogo:efeito("crt", false)

-- Liberar quando terminar
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

O sistema de FOV (Field of View) implementa **neblina de guerra** com shadowcasting 2D: o jogador só enxerga tiles dentro do seu campo de visão, e paredes bloqueiam a visão.

**Modos disponíveis:**

| Modo | Valor | O que faz |
|---|---|---|
| BASICO | `0` | Tudo escuro fora da visão atual (sem memória) |
| NEBLINA | `1` | Áreas visitadas ficam semi-visíveis (padrão — estilo roguelike) |
| SUAVE | `2` | Transição suave nas bordas da visão |

```lua
-- 1. Criar a sessão de FOV (uma vez, antes do loop)
-- parâmetros: colunas_do_mapa, linhas_do_mapa, raio_de_visão, modo
local fov = jogo:fov_novo(20, 15, 8, 1)
--                        ^cols ^rows ^raio ^modo NEBLINA

-- 2. No loop, recalcular após o jogador se mover
-- Você precisa saber em qual tile o jogador está (coluna e linha, não pixels)
local col_jogador = math.floor(px / 16)  -- supondo tiles de 16px
local lin_jogador = math.floor(py / 16)

jogo:fov_calcular(fov, col_jogador, lin_jogador, function(col, row)
    -- Esta função é chamada pela engine para cada tile
    -- Retorne true se o tile BLOQUEIA a visão (parede, obstáculo)
    -- Retorne false se a visão pode passar por ele
    local indice = row * 20 + col + 1  -- converte col/row para índice da tabela
    return mapa[indice] == PAREDE
end)

-- 3. Desenhar o mundo normalmente
jogo:limpar()
jogo:tilemap(mapa, 15, 20, sid_tiles, 16, 16, 0, 0)
jogo:desenhar()

-- 4. Desenhar a camada de sombra POR CIMA do mundo (e ANTES da UI)
-- parâmetros: fov, largura_tile, altura_tile, offset_x, offset_y, r, g, b
jogo:fov_sombra(fov, 16, 16, 0, 0)          -- sombra preta padrão
jogo:fov_sombra(fov, 16, 16, 0, 0, 0, 0, 40) -- sombra levemente azulada

-- 5. Desenhar HUD e UI por cima da sombra
jogo:texto(10, 10, "HP: 100", sid_fonte, 8, 8)

-- Consultar visibilidade de tiles (útil para lógica de jogo)
if jogo:fov_visivel(fov, 10, 5) then
    -- o tile na coluna 10, linha 5 está visível agora
end

if jogo:fov_explorado(fov, 10, 5) then
    -- o tile já foi visto antes (modo NEBLINA)
end

-- Alterar o raio de visão dinamicamente (tocha apagando, magia etc.)
jogo:fov_raio(fov, 3)   -- visão muito reduzida
jogo:fov_raio(fov, 10)  -- visão ampliada

-- Zerar tudo ao trocar de mapa (apaga memória de tiles explorados)
jogo:fov_reset(fov)
```

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:fov_novo(cols, rows, raio, modo)` | Dimensões do mapa, raio, modo | handle de FOV |
| `jogo:fov_calcular(fov, col, row, fn)` | FOV, posição do jogador (em tiles), função de parede | — |
| `jogo:fov_sombra(fov, tw, th, ox, oy, r, g, b)` | FOV, tamanho do tile, offset, cor da sombra | — |
| `jogo:fov_visivel(fov, col, row)` | FOV, posição do tile | `true` ou `false` |
| `jogo:fov_explorado(fov, col, row)` | FOV, posição do tile | `true` ou `false` |
| `jogo:fov_raio(fov, r)` | FOV, novo raio em tiles | — |
| `jogo:fov_reset(fov)` | FOV | — |

---

## Grade Espacial

A Grade Espacial (Spatial Grid) divide o mundo em células e acelera muito a checagem de colisões quando existem **muitos objetos**. Em vez de checar cada par possível (O(n²)), ela só checa objetos na mesma célula (O(1) aproximado).

> **Quando usar?** Para menos de ~30 objetos, as funções normais de colisão já são rápidas o suficiente. Use a grade quando tiver dezenas de inimigos, projéteis, itens etc. ao mesmo tempo.

```lua
-- 1. Ativar a grade antes do loop
-- cell_size: tamanho de cada célula em pixels (0 = automático, usa 64px)
-- Use um valor próximo ao tamanho dos seus objetos
jogo:sgrid_init(32)   -- células de 32x32 para objetos pequenos
jogo:sgrid_init(64)   -- células de 64x64 (padrão razoável)

-- 2. Se criar muitos objetos de uma vez fora do loop, reconstrua a grade
for i = 1, 200 do
    criar_inimigo()
end
jogo:sgrid_rebuild()   -- atualiza a grade com os novos objetos

-- 3. No loop: usar as colisões eficientes

-- Projetil vs primeiro inimigo que encontrar
local alvo = jogo:sgrid_primeira_colisao(projetil)
if alvo then
    causar_dano(alvo)
    jogo:remover_objeto(projetil)
end

-- Explosão vs todos os objetos em volta
for _, oid in ipairs(jogo:sgrid_todas_colisoes(explosao, 32)) do
    -- oid é o ID de cada objeto que está colidindo com explosao
    causar_dano(oid)
end

-- Verificar se a grade está ativa
if jogo:sgrid_ativo() then
    -- grade funcionando normalmente
end

-- Desativar e liberar quando não precisar mais
jogo:sgrid_destruir()
```

> **Importante:** A grade é atualizada automaticamente quando você usa `jogo:mover()`. Só precisa chamar `sgrid_rebuild()` manualmente ao criar muitos objetos de uma só vez fora do loop principal.

| Função | Parâmetros | Retorno |
|---|---|---|
| `jogo:sgrid_init(cell_size)` | Tamanho da célula em pixels (0 = automático) | — |
| `jogo:sgrid_rebuild()` | — | — |
| `jogo:sgrid_primeira_colisao(oid)` | object_id | ID do objeto colidindo, ou `nil` |
| `jogo:sgrid_todas_colisoes(oid, max)` | object_id, limite máximo | tabela com IDs |
| `jogo:sgrid_ativo()` | — | `true` ou `false` |
| `jogo:sgrid_destruir()` | — | — |

---

## Exemplo completo

Um jogo mínimo funcional com movimento, animação, câmera e áudio:

```lua
local E    = require("engine")
local jogo = E.nova(320, 240, "Meu Jogo", 2)

-- Inicializar sistemas
jogo:audio_init()
jogo:fundo(20, 20, 30)   -- fundo quase preto
jogo:sgrid_init(64)

-- Carregar assets
local sid_sheet = jogo:carregar_sprite("heroi.png")
local musica    = jogo:tocar("musica.ogg", true, 0.5)  -- loop, 50% volume

-- Criar o herói no centro da tela
local heroi = jogo:criar_objeto_tile(160, 180, sid_sheet, 0, 0, 16, 16)
jogo:hitbox(heroi, 2, 2, 12, 13)  -- hitbox levemente menor que o sprite

-- Criar animações (nascem paradas)
local anim_correr = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {0}, 8, true,  heroi)
local anim_cima   = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {1}, 8, true,  heroi)
local anim_baixo  = jogo:criar_animacao(sid_sheet, 16, 16, {0,1,2,3}, {2}, 8, true,  heroi)

-- Câmera segue o herói suavemente
jogo:cam_seguir(heroi, 0.15)

local vel = 80  -- pixels por segundo

while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar()

    local dt      = jogo:delta()
    local movendo = false

    if jogo:tecla("d") then
        jogo:mover(heroi, vel * dt, 0)
        jogo:espelhar(heroi, true, false)   -- vira para a direita
        jogo:anim_tocar(anim_correr)        -- troca automaticamente se outra estiver ativa
        movendo = true
    elseif jogo:tecla("a") then
        jogo:mover(heroi, -vel * dt, 0)
        jogo:espelhar(heroi, false, false)  -- vira para a esquerda
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

    -- Se parou de mover, para a animação ativa e mostra frame idle
    if not movendo then
        local ativa = jogo:anim_atual(heroi)
        if ativa then
            jogo:anim_parar(ativa, 0, 0)  -- frame (col 0, linha 0) como idle
        end
    end

    -- Teclas especiais
    if jogo:tecla_press("f") then jogo:tela_cheia() end
    if jogo:tecla_press("escape") then break end

    jogo:limpar()
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end

jogo:destruir()
```