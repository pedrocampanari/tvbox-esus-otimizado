# Atalho fino sobre o CMake (que já resolve a dependência do Raylib via
# FetchContent). Não é mais um segundo pipeline de compilação: ver
# docs/memory/architecture.md.

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/bin/tvbox_esus_app

.PHONY: all run clean

all: $(TARGET)

$(TARGET):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) obj bin
