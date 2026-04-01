CXX      = g++
CXXFLAGS = -O2 -fPIC -Wall -Wextra -std=c++17

BACKEND ?= gl

# =============================================================================
# Backend: OpenGL (padrão — comportamento idêntico ao Makefile original)
# =============================================================================
ifeq ($(BACKEND),gl)
    DEFINES  = -DENGINE_BACKEND_GL
    SRC      = src/Engine.cpp src/fov.cpp
    LIBS     = -lX11 -lGL -lpng -lm -lpthread -ldl
    BACKEND_LABEL = OpenGL/X11
endif

# =============================================================================
# Backend: Direct3D 11 (Windows — MinGW / clang)
# =============================================================================
ifeq ($(BACKEND),dx11)
    DEFINES  = -DENGINE_BACKEND_DX11
    SRC      = src/Engine.cpp src/RendererDX11.cpp src/fov.cpp
    LIBS     = -ld3d11 -ldxgi -ld3dcompiler -lpng -lm -lpthread
    BACKEND_LABEL = DirectX11/Win32
endif

# =============================================================================
# Saída comum — engine
# =============================================================================
LDFLAGS   = -shared
LIB       = libengine.so
HEADER    = src/Engine.hpp
MINIAUDIO = src/miniaudio.h

# =============================================================================
# Sistema de mapas (libmapa.so)
#
# Depende de libengine.so e da LuaJIT para carregar arquivos .lua.
# cJSON.h/.c devem estar em src/ (baixe de github.com/DaveGamble/cJSON).
#
# Carregamento Lua: -llua (LuaJIT instalado no sistema)
# Carregamento JSON: src/cJSON.c (single-header, sem dependência externa)
# =============================================================================
LIB_MAPA       = libmapa.so
SRC_MAPA       = src/mapa.cpp src/cJSON.c
HEADER_MAPA    = src/mapa.hpp
CJSON          = src/cJSON.h src/cJSON.c
LIBS_MAPA      = -llua -lm -L. -lengine

# =============================================================================
# Cores
# =============================================================================
GREEN = \033[0;32m
CYAN  = \033[0;36m
YELLOW= \033[0;33m
NC    = \033[0m

.PHONY: all run clean setup gl dx11 mapa

all: setup $(LIB) $(LIB_MAPA)

setup:
	@mkdir -p src

# -----------------------------------------------------------------------------
# Compilação da engine principal
# -----------------------------------------------------------------------------
$(LIB): $(SRC) $(HEADER) $(MINIAUDIO)
	@echo "$(CYAN)→ Backend : $(BACKEND_LABEL)$(NC)"
	@echo "→ Compilando $(LIB)..."
	$(CXX) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) -o $@ $(SRC) $(LIBS)
	@echo "$(GREEN)✓ $(LIB) compilado com sucesso! [$(BACKEND_LABEL)]$(NC)"

# -----------------------------------------------------------------------------
# Compilação do sistema de mapas
# -----------------------------------------------------------------------------
$(LIB_MAPA): $(SRC_MAPA) $(HEADER_MAPA) $(CJSON) $(LIB)
	@echo "$(YELLOW)→ Compilando $(LIB_MAPA)...$(NC)"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) \
	    -I$(LUAJIT_INC) \
	    -o $@ $(SRC_MAPA) $(LIBS_MAPA)
	@echo "$(GREEN)✓ $(LIB_MAPA) compilado com sucesso!$(NC)"

mapa: $(LIB_MAPA)

# -----------------------------------------------------------------------------
# Download automático do miniaudio
# -----------------------------------------------------------------------------
$(MINIAUDIO):
	@echo "→ Baixando miniaudio.h..."
	curl -fsSL https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h \
	     -o $(MINIAUDIO)
	@echo "$(GREEN)✓ miniaudio.h pronto$(NC)"

# -----------------------------------------------------------------------------
# Download automático do cJSON
# -----------------------------------------------------------------------------
$(CJSON):
	@echo "→ Baixando cJSON..."
	curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h \
	     -o src/cJSON.h
	curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c \
	     -o src/cJSON.c
	@echo "$(GREEN)✓ cJSON pronto$(NC)"

# -----------------------------------------------------------------------------
# Execução
# -----------------------------------------------------------------------------
run: $(LIB) $(LIB_MAPA)
	@echo "→ Iniciando LuaJIT..."
	luajit main.lua

clean:
	rm -f $(LIB) $(LIB_MAPA)
	@echo "✓ Limpeza concluída"

# Atalhos de backend
gl:
	$(MAKE) BACKEND=gl

dx11:
	$(MAKE) BACKEND=dx11