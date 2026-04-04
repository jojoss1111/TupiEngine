#!/usr/bin/env bash
# compile_shaders.sh
#
# Compila os shaders GLSL padrão do backend Vulkan para SPIR-V e gera
# o header C++ "shaders_embedded.hpp" com os arrays embutidos no código.
#
# Isso elimina a dependência de glslang para os shaders padrão da engine.
# O glslang continua sendo usado APENAS para shader_create() em runtime
# (shaders customizados criados pelo jogo).
#
# Dependência: glslc  (vem com o pacote shaderc)
#   Arch Linux : sudo pacman -S shaderc
#   Ubuntu/Deb : sudo apt install glslang-tools
#   Windows    : instale o Vulkan SDK em https://vulkan.lunarg.com
#
# Uso:
#   chmod +x compile_shaders.sh
#   ./compile_shaders.sh
#
# O arquivo gerado (shaders_embedded.hpp) deve ser commitado no repositório
# para que outros contribuidores não precisem rodar este script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_HEADER="$SCRIPT_DIR/shaders_embedded.hpp"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# ---------------------------------------------------------------------------
# Verifica dependência
# ---------------------------------------------------------------------------
if ! command -v glslc &>/dev/null; then
    echo "ERRO: 'glslc' nao encontrado."
    echo "  Arch Linux : sudo pacman -S shaderc"
    echo "  Ubuntu/Deb : sudo apt install glslang-tools"
    echo "  Windows    : instale o Vulkan SDK (https://vulkan.lunarg.com)"
    exit 1
fi

# ---------------------------------------------------------------------------
# quad.vert
# ---------------------------------------------------------------------------
cat > "$TMP_DIR/quad.vert" << 'GLSL'
#version 450
layout(push_constant) uniform PC { mat4 proj; } pc;
layout(location=0) in vec2  inPos;
layout(location=1) in vec2  inUV;
layout(location=2) in vec4  inColor;
layout(location=3) in uint  inTexIdx;
layout(location=0) out vec2      outUV;
layout(location=1) out vec4      outColor;
layout(location=2) out flat uint outTexIdx;
void main() {
    gl_Position = pc.proj * vec4(inPos, 0.0, 1.0);
    outUV      = inUV;
    outColor   = inColor;
    outTexIdx  = inTexIdx;
}
GLSL

# ---------------------------------------------------------------------------
# quad.frag
# ---------------------------------------------------------------------------
cat > "$TMP_DIR/quad.frag" << 'GLSL'
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set=0, binding=0) uniform sampler2D textures[128];
layout(location=0) in vec2      inUV;
layout(location=1) in vec4      inColor;
layout(location=2) in flat uint inTexIdx;
layout(location=0) out vec4 fragColor;
void main() {
    fragColor = texture(textures[nonuniformEXT(inTexIdx)], inUV) * inColor;
}
GLSL

echo "[1/4] Compilando quad.vert -> SPIR-V..."
glslc --target-env=vulkan1.1 -o "$TMP_DIR/quad.vert.spv" "$TMP_DIR/quad.vert"

echo "[2/4] Compilando quad.frag -> SPIR-V..."
glslc --target-env=vulkan1.1 -o "$TMP_DIR/quad.frag.spv" "$TMP_DIR/quad.frag"

# ---------------------------------------------------------------------------
# Converte .spv para array uint32_t C++
# ---------------------------------------------------------------------------
spv_to_array() {
    local spv="$1"
    local name="$2"
    local size
    size=$(wc -c < "$spv")

    echo "static const uint32_t ${name}[] = {"
    xxd -p -c 4 "$spv" | while IFS= read -r line; do
        while [ ${#line} -lt 8 ]; do line="${line}00"; done
        b1="${line:0:2}"; b2="${line:2:2}"
        b3="${line:4:2}"; b4="${line:6:2}"
        printf "    0x%s%s%s%s,\n" "$b4" "$b3" "$b2" "$b1"
    done
    echo "};"
    echo "static const size_t ${name}_size = ${size}u;"
}

echo "[3/4] Gerando $OUT_HEADER..."

{
cat << 'HEADER'
// shaders_embedded.hpp
// GERADO AUTOMATICAMENTE por compile_shaders.sh — nao edite manualmente.
// Execute ./compile_shaders.sh para regenerar apos alterar os shaders.
//
// Contem os shaders padrao do backend Vulkan pre-compilados em SPIR-V.
// Embutir os arrays aqui elimina a dependencia de glslang para o pipeline
// padrao; glslang continua sendo usado apenas por shader_create() em runtime.
#pragma once
#ifndef SHADERS_EMBEDDED_HPP
#define SHADERS_EMBEDDED_HPP
#include <cstdint>
#include <cstddef>

HEADER

spv_to_array "$TMP_DIR/quad.vert.spv" "k_vert_spv_embedded"
echo ""
spv_to_array "$TMP_DIR/quad.frag.spv" "k_frag_spv_embedded"
echo ""
echo "#endif /* SHADERS_EMBEDDED_HPP */"
} > "$OUT_HEADER"

echo "[4/4] Pronto!"
echo ""
echo "Arquivo gerado: $OUT_HEADER"
echo "Commite este arquivo junto com o codigo-fonte para que"
echo "outros contribuidores nao precisem rodar este script."
