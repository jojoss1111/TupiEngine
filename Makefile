# =============================================================================
# Makefile — Engine 2D  |  Backends: vk · gl · dx11
#
# Uso:
#   make                    → detecta SO, compila backend padrão (vk em ambos)
#   make BACKEND=gl         → força OpenGL  (Linux)
#   make BACKEND=dx11       → força DX11    (Windows/MinGW)
#   make BACKEND=vk         → força Vulkan  (Linux ou Windows)
#   make run                → compila + executa com LuaJIT
#   make clean              → remove artefatos
#   make info               → mostra configuração detectada sem compilar
#
# Detecção automática de sistema operacional:
#   - Linux  → libengine.so   (ELF shared)
#   - Windows (MinGW/MSYS2) → engine.dll  (PE shared)
#
# Detecção automática de CPU para flags SIMD (SSE4.1 / AVX2):
#   Linux  : lê /proc/cpuinfo
#   Windows: interroga o compilador com -march=native (MinGW ≥ 8)
# =============================================================================

# -----------------------------------------------------------------------------
# 1. Detectar sistema operacional
# -----------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    HOST_OS := windows
else
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Linux)
        HOST_OS := linux
    else ifeq ($(UNAME_S),Darwin)
        HOST_OS := macos
    else
        HOST_OS := linux   # fallback conservador
    endif
endif

# -----------------------------------------------------------------------------
# 2. Toolchain por SO
# -----------------------------------------------------------------------------
ifeq ($(HOST_OS),windows)
    CXX       ?= x86_64-w64-mingw32-g++
    AR        ?= x86_64-w64-mingw32-ar
    WINDRES   ?= x86_64-w64-mingw32-windres
    LIB_EXT    = dll
    LDFLAGS    = -shared -Wl,--out-implib,libengine.a
    # MinGW: não precisa de -fPIC para DLL, mas não prejudica
    FPIC       =
    EXE_EXT    = .exe
else
    CXX       ?= g++
    AR        ?= ar
    LIB_EXT    = so
    LDFLAGS    = -shared
    FPIC       = -fPIC
    EXE_EXT    =
endif

# -----------------------------------------------------------------------------
# 3. Detectar suporte a SSE4.1 e AVX2
#
#   Linux  : grep dos flags em /proc/cpuinfo
#   Windows: pergunta ao compilador qual o máximo suportado
# -----------------------------------------------------------------------------
ifeq ($(HOST_OS),linux)
    CPU_FLAGS := $(shell grep -m1 '^flags' /proc/cpuinfo 2>/dev/null || echo "")
    HAS_AVX2  := $(findstring avx2,  $(CPU_FLAGS))
    HAS_SSE41 := $(findstring sse4_1,$(CPU_FLAGS))
else
    # No Windows pedimos ao compilador para autodetectar
    # (-march=native dumpa os flags reais da CPU corrente)
    CPU_FLAGS := $(shell $(CXX) -march=native -dM -E - < /dev/null 2>/dev/null \
                         | grep -E '__AVX2__|__SSE4_1__' || echo "")
    HAS_AVX2  := $(findstring __AVX2__,  $(CPU_FLAGS))
    HAS_SSE41 := $(findstring __SSE4_1__,$(CPU_FLAGS))
endif

# Escolhe o melhor nível SIMD disponível:
#   AVX2  → vetores de 256 bits, ideal para loops de vértices
#   SSE41 → vetores de 128 bits (usado no push_quad SIMD)
#   nenhum → sem flag extra (o código cai no fallback escalar)
ifneq ($(HAS_AVX2),)
    SIMD_FLAGS = -mavx2 -mfma
    SIMD_LABEL = AVX2+FMA
else ifneq ($(HAS_SSE41),)
    SIMD_FLAGS = -msse4.1
    SIMD_LABEL = SSE4.1
else
    SIMD_FLAGS =
    SIMD_LABEL = escalar (sem SIMD)
endif

# -----------------------------------------------------------------------------
# 4. Flags de compilação base
# -----------------------------------------------------------------------------
OPT_FLAGS  = -O3 -funroll-loops -fomit-frame-pointer
WARN_FLAGS = -Wall -Wextra -Wno-unused-parameter
STD_FLAG   = -std=c++17
DEBUG_FLAGS=          # sobrescrito com  make DEBUG=1

ifdef DEBUG
    OPT_FLAGS   = -O0 -g3 -fsanitize=address,undefined
    LDFLAGS    += -fsanitize=address,undefined
    DEBUG_FLAGS = -DDEBUG_BUILD
    SIMD_FLAGS  =       # ASAN + SIMD pode gerar false positives
    SIMD_LABEL  = desativado (DEBUG)
endif

CXXFLAGS = $(STD_FLAG) $(OPT_FLAGS) $(FPIC) $(WARN_FLAGS) \
           $(SIMD_FLAGS) $(DEBUG_FLAGS)

# -----------------------------------------------------------------------------
# 5. Backend padrão = vk  (pode ser sobrescrito: make BACKEND=gl)
# -----------------------------------------------------------------------------
BACKEND ?= vk

# =============================================================================
# 6. Configurações por backend
# =============================================================================

# ------------------------------------  VULKAN  --------------------------------
ifeq ($(BACKEND),vk)
    DEFINES       = -DENGINE_BACKEND_VK
    SRC           = src/Engine_renderer.cpp \
                    src/Renderizador/RendererVK.cpp \
                    src/Engine_events.cpp \
                    src/FAR/fov.cpp

    ifeq ($(HOST_OS),linux)
        # Localiza o SDK Vulkan: prefere a variável de ambiente VULKAN_SDK,
        # cai para o pacote de sistema (vulkan-devel / libvulkan-dev)
        VULKAN_SDK   ?= /usr
        VK_INC        = -I$(VULKAN_SDK)/include
        VK_LIB        = -L$(VULKAN_SDK)/lib -lvulkan

        # glslang: tenta pkg-config primeiro, depois linkagem direta
        GLSLANG_LIBS := $(shell pkg-config --libs glslang 2>/dev/null || \
                                echo "-lglslang -lSPIRV -lglslang-default-resource-limits")
        GLSLANG_INC  := $(shell pkg-config --cflags glslang 2>/dev/null || echo "")

        # SDL3: tenta pkg-config, cai para -lSDL3 direto
        SDL3_LIBS    := $(shell pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")
        SDL3_INC     := $(shell pkg-config --cflags sdl3 2>/dev/null || echo "")
        LIBS          = $(VK_LIB) \
                        $(GLSLANG_LIBS) \
                        $(SDL3_LIBS) \
                        -lpng -lm -lpthread -ldl
        CXXFLAGS     += $(VK_INC) $(GLSLANG_INC) $(SDL3_INC)

        BACKEND_LABEL = Vulkan/SDL3 [Linux]
    endif

    ifeq ($(HOST_OS),windows)
        # No Windows o SDK Vulkan instala em VULKAN_SDK (variável de ambiente)
        # Fallback: diretório padrão do LunarG SDK
        VULKAN_SDK   ?= C:/VulkanSDK/$(shell ls "C:/VulkanSDK" 2>/dev/null \
                                        | sort -V | tail -1)
        VK_INC        = -I"$(VULKAN_SDK)/Include"
        VK_LIB        = -L"$(VULKAN_SDK)/Lib" -lvulkan-1

        # SDL3: espera MSYS2/pacman  ou  SDL3_DIR definido
        # pacman -S mingw-w64-x86_64-SDL3
        SDL3_DIR     ?= C:/msys64/mingw64
        SDL3_INC      = -I"$(SDL3_DIR)/include"
        SDL3_LIB      = -L"$(SDL3_DIR)/lib" -lSDL3

        # glslang no MSYS2: pacman -S mingw-w64-x86_64-glslang
        GLSLANG_LIBS  = -lglslang -lSPIRV -lglslang-default-resource-limits

        LIBS          = $(VK_LIB) \
                        $(GLSLANG_LIBS) \
                        $(SDL3_LIB) \
                        -lpng -lm -lpthread
        CXXFLAGS     += $(VK_INC) $(SDL3_INC)

        BACKEND_LABEL = Vulkan/SDL3 [Windows]
    endif

    # shaders_embedded.hpp — SPIR-V pré-compilado (gerado por compile_shaders.sh/.bat)
    SHADERS_EMBEDDED = src/Renderizador/shaders_embedded.hpp
    ifeq $(HOST_OS),windows)
        SHADERS_SCRIPT = compile_shaders.bat
    else
        SHADERS_SCRIPT = compile_shaders.sh
    endif
    CXXFLAGS        += -Isrc/Renderizador
endif

# ------------------------------------  OPENGL  --------------------------------
ifeq ($(BACKEND),gl)
    DEFINES       = -DENGINE_BACKEND_GL
    SRC           = src/Engine_renderer.cpp \
                    src/Renderizador/RendererGL.cpp \
                    src/Engine_events.cpp \
                    src/FAR/fov.cpp

    ifeq ($(HOST_OS),linux)
        LIBS          = -lX11 -lGL -lpng -lm -lpthread -ldl
        BACKEND_LABEL = OpenGL/X11 [Linux]
    endif

    ifeq ($(HOST_OS),windows)
        LIBS          = -lopengl32 -lgdi32 -lpng -lm -lpthread
        BACKEND_LABEL = OpenGL/Win32 [Windows]
    endif
endif

# ------------------------------------  DX11  ----------------------------------
ifeq ($(BACKEND),dx11)
    ifeq ($(HOST_OS),linux)
        $(error Backend dx11 não é suportado em Linux. Use BACKEND=vk ou BACKEND=gl)
    endif

    DEFINES       = -DENGINE_BACKEND_DX11
    SRC           = src/Engine_renderer.cpp \
                    src/Engine_events.cpp \
                    src/Renderizador/RendererDX11.cpp \
                    src/FAR/fov.cpp
    LIBS          = -ld3d11 -ldxgi -ld3dcompiler -lpng -lm -lpthread
    BACKEND_LABEL = DirectX11/Win32 [Windows]
endif

# =============================================================================
# 7. Artefatos de saída
# =============================================================================
LIB        = libengine.$(LIB_EXT)
HEADER     = src/Engine.hpp
MINIAUDIO  = src/miniaudio.h

# vk_mem_alloc.h — single-header VMA (AMD GPUOpen, MIT).
# Colocado em src/Renderizador/ para que o #include "vk_mem_alloc.h"
# em RendererVK.hpp o encontre sem -I extra.
VMA_HEADER = src/Renderizador/vk_mem_alloc.h
VMA_URL    = https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h

LIB_MAPA   = libmapa.$(LIB_EXT)
SRC_MAPA   = src/FAR/mapa.cpp
HEADER_MAPA= src/FAR/mapa.hpp

ifeq ($(HOST_OS),windows)
    LIBS_MAPA = -llua -lm -L. -lengine
else
    LIBS_MAPA = -llua -lm -L. -lengine -Wl,-rpath,.
endif

# =============================================================================
# 8. Cores ANSI (só funcionam em terminais compatíveis; no MinGW podem ser
#    suprimidas com  make NO_COLOR=1)
# =============================================================================
ifndef NO_COLOR
    GREEN  = \033[0;32m
    CYAN   = \033[0;36m
    YELLOW = \033[0;33m
    BOLD   = \033[1m
    NC     = \033[0m
else
    GREEN = BLUE = CYAN = YELLOW = BOLD = NC =
endif

# =============================================================================
# 9. Regras
# =============================================================================
.PHONY: all run clean setup gl dx11 vk info mapa deps shaders

all: info setup $(LIB) $(LIB_MAPA)

# --- Resumo de configuração (impresso antes de compilar) --------------------
info:
	@echo ""
	@printf "%b\n" "$(BOLD)╔══════════════════════════════════════════════╗$(NC)"
	@printf "%b\n" "$(BOLD)║           Engine 2D — Configuração           ║$(NC)"
	@printf "%b\n" "$(BOLD)╚══════════════════════════════════════════════╝$(NC)"
	@echo "  Sistema   : $(HOST_OS)"
	@echo "  Backend   : $(BACKEND_LABEL)"
	@echo "  SIMD      : $(SIMD_LABEL)"
	@echo "  Otimização: $(OPT_FLAGS)"
	@echo "  Saída     : $(LIB)"
	@echo ""

setup:
	@mkdir -p src src/Renderizador src/FAR

# --- Compilação dos shaders padrão Vulkan → SPIR-V --------------------------
# Linux  : executa compile_shaders.sh  (requer glslc via shaderc)
# Windows: executa compile_shaders.bat (requer glslc via Vulkan SDK LunarG)
# O arquivo gerado deve ser commitado; outros usuários não precisam do glslc.
$(SHADERS_EMBEDDED): $(SHADERS_SCRIPT)
	@echo "→ Compilando shaders Vulkan..."
ifeq ($(HOST_OS),windows)
	@$(SHADERS_SCRIPT)
else
	@chmod +x $(SHADERS_SCRIPT)
	@bash $(SHADERS_SCRIPT)
endif
	@printf "%b\n" "$(GREEN)✓ shaders_embedded.hpp gerado$(NC)"

shaders: $(SHADERS_EMBEDDED)

# --- Compilação principal ---------------------------------------------------
# Para o backend Vulkan, garante que vk_mem_alloc.h existe antes de compilar.
ifeq ($(BACKEND),vk)
$(LIB): $(SRC) $(HEADER) $(MINIAUDIO) $(VMA_HEADER) $(SHADERS_EMBEDDED)
else
$(LIB): $(SRC) $(HEADER) $(MINIAUDIO)
endif
	@printf "%b\n" "$(CYAN)→ Compilando $(LIB) [$(BACKEND_LABEL)]...$(NC)"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) -o $@ $(SRC) $(LIBS)
	@printf "%b\n" "$(GREEN)✓ $(LIB) compilado com sucesso!$(NC)"
	@echo "  Flags SIMD ativos: $(SIMD_FLAGS)"

# --- Sistema de mapas -------------------------------------------------------
$(LIB_MAPA): $(SRC_MAPA) $(HEADER_MAPA) $(LIB)
	@printf "%b\n" "$(YELLOW)→ Compilando $(LIB_MAPA)...$(NC)"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) \
		-o $@ $(SRC_MAPA) $(LIBS_MAPA)
	@printf "%b\n" "$(GREEN)✓ $(LIB_MAPA) compilado com sucesso!$(NC)"

mapa: $(LIB_MAPA)

# --- Download automático do miniaudio ---------------------------------------
$(MINIAUDIO):
	@echo "→ Baixando miniaudio.h..."
	curl -fsSL \
	  https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h \
	  -o $(MINIAUDIO)
	@printf "%b\n" "$(GREEN)✓ miniaudio.h pronto$(NC)"

# --- Download automático do vk_mem_alloc.h ----------------------------------
# Repositório oficial: GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator (MIT)
$(VMA_HEADER):
	@echo "→ Baixando vk_mem_alloc.h..."
	@mkdir -p src/Renderizador
	curl -fsSL $(VMA_URL) -o $(VMA_HEADER)
	@printf "%b\n" "$(GREEN)✓ vk_mem_alloc.h pronto$(NC)"

# --- Execução ---------------------------------------------------------------
run: $(LIB) $(LIB_MAPA)
	@echo "→ Iniciando LuaJIT..."
ifeq ($(HOST_OS),windows)
	luajit$(EXE_EXT) main.lua
else
	luajit main.lua
endif

# --- Limpeza ----------------------------------------------------------------
clean:
	rm -f $(LIB) $(LIB_MAPA)
ifeq ($(HOST_OS),windows)
	rm -f libengine.a       # import library MinGW
endif
	rm -f vk_pipeline_cache.bin
ifeq ($(BACKEND),vk)
	@echo "  (shaders_embedded.hpp mantido — commite-o no repositório)"
endif
	@printf "%b\n" "$(GREEN)✓ Limpeza concluída$(NC)"

# --- Atalho para baixar dependências sem compilar ---------------------------
deps: $(MINIAUDIO) $(VMA_HEADER) $(SHADERS_EMBEDDED)
	@printf "%b\n" "$(GREEN)✓ Todas as dependências prontas$(NC)"

# --- Atalhos de backend -----------------------------------------------------
vk:
	$(MAKE) BACKEND=vk

gl:
	$(MAKE) BACKEND=gl

dx11:
	$(MAKE) BACKEND=dx11