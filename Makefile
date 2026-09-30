# Atalho fino sobre o CMake (que já resolve a dependência do Raylib via
# FetchContent). Não é mais um segundo pipeline de compilação: ver
# docs/memory/architecture.md.

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/bin/tvbox_esus_app

.PHONY: all run clean

# `all` SEMPRE chama o build do CMake (incremental — só recompila o que
# mudou). Antes o alvo era o próprio binário sem dependências: depois de
# um `git pull`, `make` via o binário antigo "em dia" e não recompilava
# nada (o dispositivo ficava rodando código velho sem avisar).
all:
	@test -f $(BUILD_DIR)/CMakeCache.txt || cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) obj bin
