# TupiEngine

[![Backend](https://img.shields.io/badge/Backend-C%2B%2B%2017-blue?style=for-the-badge&logo=c%2B%2B)](https://isocpp.org/)
[![Scripting](https://img.shields.io/badge/Scripting-LuaJIT-red?style=for-the-badge&logo=lua)](https://luajit.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey?style=for-the-badge&logo=linux)](https://github.com)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

TupiEngine é uma engine 2D minimalista de alto desempenho, desenvolvida para facilitar a criação de jogos com estética retrô (16 bits). O núcleo é implementado em C++17 e expõe uma interface de scripting via LuaJIT, permitindo desenvolvimento rápido sem sacrificar performance.

---

## Índice

1. [Visão Geral](#visão-geral)
2. [Arquitetura](#arquitetura)
3. [Exemplo de Uso](#exemplo-de-uso)
4. [Compilação](#compilação)
5. [Estrutura do Projeto](#estrutura-do-projeto)
6. [Roadmap](#roadmap)
7. [Contribuição](#contribuição)

---

## Visão Geral

A engine foi projetada com dois perfis de usuário em mente.

**Desenvolvedores iniciantes:** toda a lógica do jogo pode ser escrita em Lua, uma linguagem de sintaxe simples e amplamente documentada. Não é necessário compilar o projeto a cada alteração — basta editar o script e executar novamente. Conceitos de baixo nível como buffers de vértices e swap chains são gerenciados internamente pela engine.

**Desenvolvedores experientes:** o núcleo em C++ acessa o hardware diretamente via X11 e OpenGL no Linux, ou via Win32 e DirectX 11 no Windows. O scripting usa LuaJIT FFI para chamadas diretas a funções C, eliminando overhead de binding. As estruturas de dados seguem o modelo POD (Plain Old Data) com gerenciamento manual de memória, adequado para hardware com recursos limitados.

---

## Arquitetura

| Componente  | Tecnologia        | Responsabilidade                                              |
|:------------|:------------------|:--------------------------------------------------------------|
| Core        | C++17             | Gerenciamento de janela, eventos de entrada e ciclo de vida.  |
| Graphics    | OpenGL / DX11     | Renderização em batch para milhares de sprites por frame.     |
| Audio       | miniaudio         | Reprodução de MP3, WAV e FLAC com baixa latência.             |
| Scripting   | LuaJIT            | Interface de alto nível para desenvolvimento de jogos.        |

---

## Exemplo de Uso

O trecho abaixo demonstra um loop de jogo funcional com movimento de entidade e controle de framerate:

```lua
-- main.lua
package.path = package.path .. ";./?.lua;./src/?.lua;./obj/?.lua"

local E        = require("engine")
local SCREEN_W = 256
local SCREEN_H = 244

local function main()
    local v = E.nova(SCREEN_W, SCREEN_H, "Meu Jogo", 2)
    if not v then
        print("Falha ao inicializar a engine.")
        return
    end

    v:fundo(0, 0, 0)

    local x         = SCREEN_W / 2
    local y         = SCREEN_H / 2
    local velocidade = 4
    local tamanho_q  = 16

    while v:rodando() do
        v:eventos()
        v:limpar()
        v:atualizar(0)

        if     v:tecla("w") then x = x - velocidade
        elseif v:tecla("s") then x = x + velocidade
        elseif v:tecla("d") then y = y + velocidade
        elseif v:tecla("a") then y = y - velocidade
        end

        v:rect(x, y, tamanho_q, tamanho_q, 200, 10, 50)
        v:apresentar()
        v:fps(60)
    end

    v:destruir()
end

main()
```

---

## Compilação

### Pré-requisitos

**Linux (Ubuntu/Debian):** `g++`, `make`, `libx11-dev`, `libgl1-mesa-dev`, `libpng-dev`, `luajit`.

**Windows (MSYS2/MinGW ou Clang):** ambiente com suporte a `make` e headers do DirectX 11.

### Gerando a biblioteca

O processo de build gera `libengine.so` (Linux) ou `engine.dll` (Windows), que é a biblioteca dinâmica carregada pelo runtime do LuaJIT.

**Linux — backend OpenGL/X11:**
```sh
make BACKEND=gl
```

**Windows — backend DirectX 11/Win32:**
```sh
make BACKEND=dx11
```

> O Makefile faz o download automático de `miniaudio.h` via `curl` caso o arquivo não esteja presente em `src/`.

### Executando um jogo

A TupiEngine não gera um executável independente. O jogo é executado diretamente pelo interpretador LuaJIT, que carrega a biblioteca dinâmica em tempo de execução.

Certifique-se de que `libengine.so` (ou `engine.dll`) está no mesmo diretório do script principal e execute:

```sh
luajit main.lua
```

---

## Estrutura do Projeto

```
TupiEngine/
├── src/        # Código-fonte do núcleo em C++
├── main.lua    # Ponto de entrada do jogo
├── Makefile    # Configuração de build
└── README.md   # Este documento
```

---

## Contribuição

Contribuições são bem-vindas nas seguintes formas:

- Relatos de bugs encontrados durante o uso.
- Sugestões de funcionalidades ou melhorias de API.
- Compartilhamento de jogos desenvolvidos com a engine.

Abra uma issue ou entre em contato diretamente para discutir qualquer um dos pontos acima.
