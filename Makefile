CC      = gcc
CFLAGS  = -O2 -fPIC -Wall -Wextra
LDFLAGS = -shared
LIBS    = -lX11 -lGL -lpng -lm -lpthread -ldl

LIB     = libengine.so
SRC     = src/Engine.c

.PHONY: all run clean

all: $(LIB)

$(LIB): $(SRC) src/Engine.h src/miniaudio.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	@echo "✓ $(LIB) compilado"

src/miniaudio.h:
	@echo "→ Baixando miniaudio.h..."
	curl -fsSL https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h \
	     -o src/miniaudio.h
	@echo "✓ miniaudio.h baixado"

run: $(LIB)
	luajit main.lua

clean:
	rm -f $(LIB)