# Configurações do Compilador
CXX      = g++
CXXFLAGS = -O2 -fPIC -Wall -Wextra -std=c++17
LDFLAGS  = -shared

# Bibliotecas (Adicionamos as flags para busca de headers se necessário)
# Se for usar DirectX no futuro (via Wine/Mingw) ou Vulkan, adicionaremos aqui.
LIBS     = -lX11 -lGL -lpng -lm -lpthread -ldl

# Arquivos e Diretórios
LIB      = libengine.so
SRC      = src/Engine.cpp
HEADER   = src/Engine.hpp
MINIAUDIO = src/miniaudio.h

# Cores para o terminal (estética)
GREEN    = \033[0;32m
NC       = \033[0m

.PHONY: all run clean setup

all: setup $(LIB)

# Garante que o diretório src existe
setup:
	@mkdir -p src

# Compilação da Shared Library
$(LIB): $(SRC) $(HEADER) $(MINIAUDIO)
	@echo "→ Compilando $(LIB)..."
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LIBS)
	@echo "$(GREEN)✓ $(LIB) compilado com sucesso!$(NC)"

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