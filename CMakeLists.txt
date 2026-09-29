cmake_minimum_required(VERSION 3.14)
project(tvbox_esus_app LANGUAGES CXX)

# Configura o padrão do C++ para C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Baixa o Raylib automaticamente do GitHub (não empacotado no Armbian;
# ver docs/memory/architecture.md sobre o fluxo de build recomendado:
# compilar num host de desenvolvimento e apenas copiar o binário final
# para o dispositivo).
include(FetchContent)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)

# Garante que as opções do Raylib não poluam o cache global do CMake
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

# Força OpenGL ES 2.0 em vez do padrão do Raylib pra PLATFORM_DESKTOP
# (OpenGL 3.3 desktop). O alvo real é RK3229 (GPU Mali-400), que só fala
# OpenGL ES — pedir GL 3.3 nele faz o GLFW/Raylib crashar (segfault) na
# criação do contexto gráfico, confirmado testando de verdade num
# RK322x real (ver docs/memory/known-issues.md). O app só usa desenho 2D
# básico (retângulos, texto, textura) — não depende de nenhum recurso
# exclusivo do GL 3.3 — então ES 2.0 funciona igual em qualquer GPU,
# incluindo as desktop (AMD/Intel/NVIDIA) usadas em desenvolvimento.
set(OPENGL_VERSION "ES 2.0" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(raylib)

find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
  apps/tvbox_esus_app.cpp
  src/scraper.cpp
  src/player.cpp
  src/ui.cpp
  src/procexec.cpp
  src/campaign_store.cpp
  src/native_window.cpp
  src/remote_control.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE include
  # Headers do GLFW vendorizado pelo raylib (src/native_window.cpp usa
  # glfwGetX11Window/glfwGetPlatform pra obter o ID de janela X11
  # nativo — ver include/native_window.h sobre o porquê). Só o header:
  # os símbolos já estão dentro de libraylib.a (raylib embute os
  # objetos do GLFW), então não precisamos linkar o alvo `glfw` também.
  ${raylib_SOURCE_DIR}/src/external/glfw/include
)

# X11: janela FILHA da janela do Raylib onde o mpv desenha o vídeo
# (embedding via --wid) — ver include/player.h sobre por que é filha,
# não uma segunda janela top-level. Threads: thread de scraping em
# segundo plano. Nenhuma lib de vídeo/HTTP é linkada — curl/mpv/yt-dlp
# são invocados como processos externos (ver docs/memory/architecture.md).
target_link_libraries(${PROJECT_NAME} PRIVATE raylib X11::X11 Threads::Threads)

# Define o diretório de saída do executável
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
