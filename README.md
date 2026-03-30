# 🏹 TupiEngine

<p align="center">
  <img src="https://img.shields.io/badge/Backend-C%2B%2B%2017-blue?style=for-the-badge&logo=c%2B%2B" alt="C++17">
  <img src="https://img.shields.io/badge/Scripting-LuaJIT-red?style=for-the-badge&logo=lua" alt="LuaJIT">
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey?style=for-the-badge&logo=linux" alt="Plataformas">
  <img src="https://img.shields.io/badge/Focus-High%20Performance-orange?style=for-the-badge" alt="Performance">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="Licença">
</p>

> *"Engine 100% feita por um desenvolvedor com hiperfoco para ajudar novos programadores em Lua e C."*

A **TupiEngine** é uma engine 2D minimalista e ultra-veloz, projetada para criadores que desejam desenvolver jogos com estética retro (16-bits) sem lidar com o peso e a complexidade das engines comerciais modernas. Ela une o poder bruto e o controle do **C++** com a agilidade e simplicidade do **LuaJIT**, permitindo que você escreva código limpo que roda na velocidade da luz.

---

## 📋 Índice
1. [Principais Funcionalidades](#-principais-funcionalidades)
2. [Por que usar a TupiEngine?](#-por-que-usar-a-tupiengine)
3. [Arquitetura do Sistema](#-arquitetura-do-sistema)
4. [Como Começar](#-como-começar)
    - [Pré-requisitos](#1-pré-requisitos)
    - [Compilação](#2-compilação)
    - [Rodando seu Jogo](#3-rodando-seu-jogo)
5. [Código em Ação](#-código-em-ação-lua)
6. [Estrutura do Projeto](#-estrutura-do-projeto)
7. [Roadmap](#-roadmap)
8. [Como Contribuir](#-como-contribuir)
9. [Licença](#-licença)

---

## ✨ Principais Funcionalidades

- **Desenvolvimento em Lua Puro:** Foco total na lógica do jogo usando LuaJIT, garantindo desenvolvimento super rápido.
- **Hot-Reloading Intuitivo:** Sem tempos longos de compilação. Altere o script e veja o resultado imediatamente.
- **Batch Rendering Otimizado:** Capacidade de desenhar milhares de sprites simultaneamente sem perda de frames graças ao renderizador em lotes (C++ e OpenGL/DX11).
- **Zero Overhead com FFI:** Uso intensivo do *LuaJIT FFI* para chamadas de função diretas ao núcleo em C++.
- **Áudio Integrado de Baixa Latência:** Suporte nativo a formatos MP3, WAV e FLAC através da biblioteca `miniaudio`.
- **Arquitetura Baseada em Dados (POD):** Gerenciamento manual de memória focado na melhor performance, até mesmo em hardware antigo.

---

## 🚀 Por que usar a TupiEngine?

### 🐥 Para Iniciantes (O caminho mais fácil)
* **Amigável:** Escreva toda a lógica do seu jogo em Lua, uma das linguagens mais acessíveis para quem está começando.
* **Abstração Inteligente:** Você não precisa saber o que é um "Buffer de Vértices" ou "Swap Chain". A engine cuida da burocracia do hardware, você cuida da diversão.

### 🦾 Para Veteranos (O controle total)
* **Arquitetura Híbrida:** O núcleo em C++ gerencia a comunicação de baixo nível com o hardware via **X11/OpenGL** (Linux) ou **DirectX 11** (Windows).
* **Performance Extrema:** Estruturas POD (Plain Old Data) asseguram que seus jogos tirem leite de pedra até mesmo nas máquinas mais modestas.

---

## 🛠️ Arquitetura do Sistema

| Componente | Tecnologia | Descrição |
| :--- | :--- | :--- |
| **Core** | C++ 17 | Gerenciamento de janelas, processamento de inputs e ciclo de vida. |
| **Graphics** | OpenGL / DX11 | Renderizador de Batch (lotes) otimizado para cenários 2D pesados. |
| **Audio** | Miniaudio | Subsistema de áudio ágil e com suporte multiplataforma. |
| **Scripting** | LuaJIT | Interface de alto nível baseada em JIT-compilation para o motor C++. |

---

## ⚙️ Como Começar

A engine foi desenhada para ser compilada de forma simples e direta utilizando o utilitário `make`.

### 1. Pré-requisitos
Certifique-se de ter as ferramentas básicas instaladas no seu ambiente de desenvolvimento:

* **No Linux (Ubuntu/Debian):**
  ```bash
  sudo apt-get install g++ make libx11-dev libgl1-mesa-dev libpng-dev luajit
