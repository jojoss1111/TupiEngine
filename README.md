# 🏹 TupiEngine

<p align="center">
  <img src="https://img.shields.io/badge/Backend-C%2B%2B%2017-blue?style=for-the-badge&logo=c%2B%2B" alt="C++17">
  <img src="https://img.shields.io/badge/Scripting-LuaJIT-red?style=for-the-badge&logo=lua" alt="LuaJIT">
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey?style=for-the-badge&logo=linux" alt="Plataformas">
  <img src="https://img.shields.io/badge/Focus-High%20Performance-orange?style=for-the-badge" alt="Performance">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="Licença">
</p>

---

### **"Engine 100% feita por um desenvolvedor com hiperfoco para ajudar novos programadores em Lua e C."**

A **TupiEngine** é uma engine 2D minimalista e ultra-veloz, projetada para quem quer criar jogos com estética retro (16-bits) sem lidar com o peso de engines comerciais. Ela une o poder bruto do **C++** com a agilidade do **LuaJIT**, permitindo que você escreva código simples que roda na velocidade da luz.

---

## 📋 Índice
1. [Por que usar a TupiEngine?](#-por-que-usar-a-tupiengine)
2. [Arquitetura do Sistema](#-arquitetura-do-sistema)
3. [Código em Ação (Lua)](#-código-em-ação-lua)
4. [Como Compilar e Rodar](#-como-compilar-e-rodar)
5. [Estrutura do Projeto](#-estrutura-do-projeto)
6. [Roadmap](#-roadmap)
7. [Como Contribuir](#-como-contribuir)

---

## 🚀 Por que usar a TupiEngine?

### 🐥 Para Iniciantes (O caminho fácil)
* **Lua Puro:** Escreva a lógica do seu jogo em Lua, uma das linguagens mais fáceis do mundo.
* **Sem Compilação:** Altere o seu script e veja o resultado na hora. Nada de esperar minutos para ver um quadrado se mexer.
* **Abstração:** Você não precisa saber o que é um "Buffer de Vértices" ou "Swap Chain". A engine cuida disso, você cuida da diversão.

### 🦾 Para Veteranos (O controle total)
* **Arquitetura Híbrida:** O núcleo em C++ gerencia o hardware via **X11/OpenGL** (Linux) ou **DirectX 11** (Windows).
* **Zero Overhead:** Usamos **LuaJIT FFI** para chamar funções C diretamente, garantindo que o scripting não seja um gargalo.
* **Foco em Dados:** Estruturas POD (Plain Old Data) e gerenciamento de memória manual para performance máxima em hardware modesto.

---

## 🛠️ Arquitetura do Sistema

| Componente | Tecnologia | Descrição |
| :--- | :--- | :--- |
| **Core** | C++ 17 | Gerenciamento de janelas, inputs e ciclo de vida. |
| **Graphics** | OpenGL / DX11 | Renderizador de Batch (lotes) para milhares de sprites. |
| **Audio** | Miniaudio | Suporte a MP3, WAV e FLAC com baixa latência. |
| **Scripting** | LuaJIT | Interface de alto nível para desenvolvimento rápido. |

---

## 🎮 Código em Ação (Lua)

Esqueça códigos complexos. Na TupiEngine, um loop de jogo profissional parece com isso:

```lua
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
```
## 🛠️ Como Compilar e Rodar

A engine foi desenhada para ser compilada de forma simples usando o `g++` e o comando `make`.

### 1. Pré-requisitos
Certifique-se de ter as ferramentas básicas instaladas no seu ambiente de desenvolvimento:
* **No Linux (Ubuntu/Debian):** `g++`, `make`, `libx11-dev`, `libgl1-mesa-dev`, `libpng-dev` e `luajit`.
* **No Windows (MinGW/Clang):** Um ambiente que suporte `make` (como MSYS2) e os headers do DirectX 11.

### 2. Compilação (Gerando a Shared Library)
O comando principal gera o arquivo `libengine.so` (ou `.dll`), que é o coração da engine.

**Para Linux (Padrão OpenGL/X11):**
```
make BACKEND=gl
```
**Para Windows (Padrão DirectX11/Win32):**
```
make BACKEND=dx11
```
**Nota: O Makefile baixará automaticamente o arquivo miniaudio.h usando o comando curl caso ele não esteja na pasta src/.**

### 3. Como Rodar o seu Jogo:
A TupiEngine não gera um executável "fechado", ela é uma biblioteca dinâmica carregada diretamente pelo LuaJIT.
Certifique-se de que o arquivo compilado (libengine.so ou engine.dll) está na mesma pasta do seu script principal (ex: main.lua).
Chame o LuaJIT apontando para o seu script:
```
luajit main.lua
```
## 📁 Estrutura do Projeto
```
TupiEngine/
├── src/           # Código-fonte do Core em C++ (Backend)
├── main.lua       # Script de entrada do jogo
├── Makefile       # Arquivo de configuração de build
└── README.md      # Documentação da engine
```
## 🤝 Como Contribuir
Pode contrubuir apenas me enviando oque você achou, novas coisas que posso colocar, erro que você encontrou 
e se possivel compartilhar com um amigo ou fazer um jogo e falar que fez na tupiengine

## Links para a documentação completa 
* [Como funcionam as funções de Sistema](./documentação.md#sistema)
