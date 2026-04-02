# Sistema de Mapas — TupiEngine

O sistema de mapas é composto por três arquivos com responsabilidades distintas:

| Arquivo | Responsabilidade |
|---|---|
| `mapa.hpp` | Structs, flags e API pública em C++ |
| `mapa.lua` | Módulo Lua para descrever e construir a estrutura do mapa |
| `engine.lua` | Funções de alto nível: carregamento, renderização e colisão |

O Lua descreve o conteúdo do mapa (tiles, camadas, objetos). A engine cuida de carregar o atlas, montar a tabela de colisores e renderizar cada frame.

---

## Fluxo básico

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

---

## Criando um arquivo de mapa

O arquivo de mapa é um script Lua que usa o módulo `mapa.lua` para descrever tiles, camadas e objetos, e deve sempre terminar com `return m:build()`.

### Estrutura mínima

```lua
-- mapa_exemplo.lua
local Mapa = require("mapa")

local m = Mapa.novo(20, 15, 16, 16)   -- 20 colunas, 15 linhas, tiles de 16×16 px
m:atlas("sprites/tileset.png")        -- spritesheet principal

-- ... definição de tiles e objetos ...

return m:build()   -- NÃO remova esta linha
```

### Método 1 — Matriz de blocos (recomendado)

Defina protótipos com `criar_bloco()` e monte o layout com um array 1D. É a forma mais legível para mapas criados à mão.

```lua
local Mapa = require("mapa")
local m = Mapa.novo(10, 8, 16, 16)
m:atlas("sprites/tileset.png")

local _ = 0   -- célula vazia

-- criar_bloco(nome, id, config)
-- config aceita: tiles={col, lin}, flags=Mapa.F.*, colide=1 (atalho para COLISOR)
local G = m:criar_bloco("Grama", 1, { tiles = {0, 0} })
local T = m:criar_bloco("Terra", 2, { tiles = {1, 0} })
local P = m:criar_bloco("Parede", 3, { tiles = {2, 0}, colide = 1 })
local A = m:criar_bloco("Agua",  4, { tiles = {0, 2}, flags = Mapa.F.AGUA })

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

### Método 2 — Camadas com funções de preenchimento

Para controle mais fino, crie camadas explicitamente e posicione tiles com as funções da camada.

```lua
local Mapa = require("mapa")
local m = Mapa.novo(25, 18, 16, 16)
m:atlas("sprites/tileset.png")

-- camada(nome, z_order)  — z_order menor = desenhado primeiro
local chao   = m:camada("chao",   0)
local paredes = m:camada("paredes", 1)
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

---

## Objetos e triggers

Objetos são entidades especiais posicionadas em tiles: baús, NPCs, portas, teleportes. Eles definem uma área de ativação (`raio` em tiles) e propriedades arbitrárias.

```lua
-- objeto(id, tipo, col, lin, raio, props, sprite_opcional)
m:objeto(1, "npc",      10, 8,  2.0, { nome = "Fazendeiro", dialogo = "Bom dia!" })
m:objeto(2, "bau",       3, 3,  1.5, { item = "enxada", quantidade = "1" })
m:objeto(3, "teleporte", 13, 15, 1.0, { destino = "mapa_vila.lua", dest_col = "1", dest_lin = "7" })
```

### Tipos de trigger predefinidos

| String no Lua | Comportamento esperado |
|---|---|
| `"bau"` | Coleta ao pressionar `[E]` |
| `"npc"` | Diálogo ao pressionar `[E]` |
| `"porta"` | Abre/fecha mediante condição |
| `"teleporte"` | Troca de mapa ao pisar |
| `"script"` | Executa lógica definida em Lua |
| `"generico"` | Sem comportamento predefinido |

> Os tipos são apenas strings — a lógica de reação fica no seu `main.lua`. A engine só informa qual objeto está próximo do jogador.

---

## Flags de bloco

Flags controlam o comportamento físico e visual de cada tile. Combine-as com `bor()` (LuaJIT):

```lua
local bit = require("bit")

-- Flags disponíveis em Mapa.F
Mapa.F.NENHUM   -- 0      sem comportamento especial
Mapa.F.COLISOR  -- 0x01   bloqueia movimento (AABB sólido)
Mapa.F.TRIGGER  -- 0x02   dispara evento de proximidade
Mapa.F.AGUA     -- 0x04   tratado como superfície aquática
Mapa.F.ESCADA   -- 0x08   permite escalada
Mapa.F.SOMBRA   -- 0x10   bloqueia o FOV
Mapa.F.ANIMADO  -- 0x20   tile com animação de frames

-- Combinando flags
local flags = bit.bor(Mapa.F.COLISOR, Mapa.F.SOMBRA)
```

No `criar_bloco()`, o atalho `colide = 1` equivale a `flags = Mapa.F.COLISOR` e pode ser usado junto com outras flags:

```lua
-- Equivalentes:
m:criar_bloco("Parede", 1, { tiles = {2, 0}, colide = 1 })
m:criar_bloco("Parede", 1, { tiles = {2, 0}, flags = Mapa.F.COLISOR })
```

> **Atenção — LuaJIT:** os operadores `<<` e `|` do Lua 5.3+ não funcionam no LuaJIT. Use sempre `bit.lshift()` e `bit.bor()` (já importados internamente pelo `mapa.lua`).

---

## API de alto nível — engine.lua

Estas são as funções que você chama no `main.lua`. Não é necessário interagir com o C++ diretamente para o fluxo básico.

| Função | Parâmetros | Retorno | Descrição |
|---|---|---|---|
| `engine:carregar_mapa(caminho)` | Caminho do `.lua` | tabela de mapa | Carrega o mapa, o atlas e pré-computa colisores |
| `engine:desenhar_mapa(mapa)` | Tabela retornada por `carregar_mapa` | — | Renderiza todos os tiles visíveis |
| `engine:colide_mapa(mapa, oid)` | Mapa, object_id | `true` ou `false` | Verifica colisão AABB do objeto com tiles sólidos |

### Exemplo de loop com mapa

```lua
local mapa   = engine:carregar_mapa("mapa_fazenda.lua")
local sprite = engine:carregar_sprite("sprites/player.png")
local jogador = engine:criar_objeto_tile(64, 64, sprite, 0, 0, 16, 16)
engine:hitbox(jogador, 1, 4, 14, 12)

local vel = 2
local dx, dy = 0, 0

while engine:rodando() do
    engine:eventos()
    engine:atualizar()
    engine:limpar()

    -- Mapa sempre antes dos objetos
    engine:desenhar_mapa(mapa)

    dx, dy = 0, 0
    if engine:tecla("d") then dx =  vel end
    if engine:tecla("a") then dx = -vel end
    if engine:tecla("s") then dy =  vel end
    if engine:tecla("w") then dy = -vel end

    engine:mover(jogador, dx, dy)

    -- Desfaz o movimento se bateu em tile sólido
    if engine:colide_mapa(mapa, jogador) then
        engine:mover(jogador, -dx, -dy)
    end

    engine:desenhar()
    engine:apresentar()
    engine:fps(60)
end

engine:destruir()
```

---

## API do módulo mapa.lua

### Mapa

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

### Camada

| Função | Parâmetros | Descrição |
|---|---|---|
| `c:tile(col, lin, sc, sl, flags, sprite?)` | Posição no mapa, posição no atlas, flags | Define um tile individual |
| `c:tile_animado(col, lin, lin_atlas, frames, fps, flags?)` | Posição, linha do atlas, lista de colunas, fps | Tile com animação de frames |
| `c:fill(ci, li, cf, lf, sc, sl, flags?)` | Retângulo (col/lin ini e fim), tile, flags | Preenche uma área retangular |
| `c:borda(larg, alt, sc, sl)` | Dimensões do mapa, tile | Cria borda sólida ao redor do mapa |
| `c:linha_h(ci, cf, lin, sc, sl, flags?)` | Coluna inicial, final, linha, tile, flags | Linha horizontal de tiles |
| `c:linha_v(col, li, lf, sc, sl, flags?)` | Coluna, linha inicial, final, tile, flags | Linha vertical de tiles |

---

## Constantes de tile prontas

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
