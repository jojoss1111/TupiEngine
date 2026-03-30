# 🏹 TupiEngine

<p align="center">
  <img src="https://img.shields.io/badge/Backend-C%2B%2B%2017-blue?style=for-the-badge&logo=c%2B%2B" alt="C++17">
  <img src="https://img.shields.io/badge/Scripting-LuaJIT-red?style=for-the-badge&logo=lua" alt="LuaJIT">
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey?style=for-the-badge&logo=linux" alt="Plataformas">
  <img src="https://img.shields.io/badge/Focus-High%20Performance-orange?style=for-the-badge" alt="Performance">
</p>

---

### **"Engine 100% feita por um desenvolvedor com hiperfoco para ajudar novos programadores em Lua e C."**

A **TupiEngine** é uma engine 2D minimalista e ultra-veloz, projetada para quem quer criar jogos com estética retro (16-bits) sem lidar com o peso de engines comerciais. Ela une o poder bruto do **C++** com a agilidade do **LuaJIT**, permitindo que você escreva código simples que roda na velocidade da luz.

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
local Tupi = require("engine")
local jogo = Tupi.nova(320, 180, "Tupi Quest", 3) -- Resolução retro com upscale

while jogo:rodando() do
    jogo:eventos()
    
    -- Lógica simples de cor de fundo
    jogo:limpar()
    jogo:fundo(20, 20, 25) 
    
    -- Desenhar um player (x, y, largura, altura, R, G, B)
    jogo:ret(150, 80, 16, 16, 255, 200, 0)
    
    jogo:desenhar()
    jogo:apresentar()
    jogo:fps(60)
end
```
## 🛠️ Como Compilar e Rodar
A engine foi desenhada para ser compilada de forma simples usando o `make`.
### 1. Pré-requisitos
Certifique-se de ter as ferramentas básicas instaladas:
* **No Linux:** `g++`, `make`, `libx11-dev`, `libgl1-mesa-dev`, `libpng-dev` e `luajit`.
* **No Windows (MinGW/Clang):** Um ambiente que suporte `make` (como MSYS2) e os headers do DirectX 11.
### 2. Compilação (Gerando a Shared Library)
O comando principal gera o arquivo `libengine.so` (ou `.dll`), que é o coração da engine.
**Para Linux (Padrão OpenGL/X11):**
```bash
make BACKEND=gl
```
```Dos
make BACKEND=dx11
```
* **Nota: O Makefile baixará automaticamente o arquivo miniaudio.h caso ele não esteja na pasta src/.
### 3. Como Rodar o seu Jogo
A TupiEngine não gera um executável "fechado", ela é uma biblioteca carregada pelo LuaJIT.
Certifique-se de que o arquivo compilado (libengine.so) está na mesma pasta do seu script principal (ex: main.lua).
Chame o LuaJIT apontando para o seu script:
```
luajit main.lua
```
