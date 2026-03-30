CXX      = g++
CXXFLAGS = -O2 -fPIC -Wall -Wextra -std=c++17

BACKEND ?= gl

# =============================================================================
# Backend: OpenGL (padrão — comportamento idêntico ao Makefile original)
# =============================================================================
ifeq ($(BACKEND),gl)
    DEFINES  = -DENGINE_BACKEND_GL
    SRC      = src/Engine.cpp
    LIBS     = -lX11 -lGL -lpng -lm -lpthread -ldl
    BACKEND_LABEL = OpenGL/X11
endif

# =============================================================================
# Backend: Direct3D 11 (Windows — MinGW / clang)
# =============================================================================
ifeq ($(BACKEND),dx11)
    DEFINES  = -DENGINE_BACKEND_DX11
    SRC      = src/Engine.cpp src/RendererDX11.cpp
    LIBS     = -ld3d11 -ldxgi -ld3dcompiler -lpng -lm -lpthread
    BACKEND_LABEL = DirectX11/Win32
endif

# =============================================================================
# Saída comum
# =============================================================================
LDFLAGS   = -shared
LIB       = libengine.so
HEADER    = src/Engine.hpp
MINIAUDIO = src/miniaudio.h

# Cores
GREEN = \033[0;32m
CYAN  = \033[0;36m
NC    = \033[0m

.PHONY: all run clean setup gl vk dx11

all: setup $(LIB)

setup:
	@mkdir -p src

# Compilação da Shared Library
$(LIB): $(SRC) $(HEADER) $(MINIAUDIO)
	@echo "$(CYAN)→ Backend : $(BACKEND_LABEL)$(NC)"
	@echo "→ Compilando $(LIB)..."
	$(CXX) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) -o $@ $(SRC) $(LIBS)
	@echo "$(GREEN)✓ $(LIB) compilado com sucesso! [$(BACKEND_LABEL)]$(NC)"

# Download automático do miniaudio
$(MINIAUDIO):
	@echo "→ Baixando miniaudio.h..."
	curl -fsSL https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h \
	     -o $(MINIAUDIO)
	@echo "$(GREEN)✓ miniaudio.h pronto$(NC)"

run: $(LIB)
	@echo "→ Iniciando LuaJIT..."
	luajit main.lua

clean:
	rm -f $(LIB)
	@echo "✓ Limpeza concluída"

# Atalhos de backend
gl:
	$(MAKE) BACKEND=gl

dx11:
	$(MAKE) BACKEND=dx11