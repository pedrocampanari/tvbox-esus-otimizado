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

FetchContent_MakeAvailable(raylib)

find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
  apps/tvbox_esus_app.cpp
  src/scraper.cpp
  src/player.cpp
  src/ui.cpp
  src/procexec.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE include)

# X11: janela dedicada onde o mpv desenha o vídeo (embedding via --wid),
# sem depender de internals do Raylib/GLFW. Threads: thread de scraping
# em segundo plano. Nenhuma lib de vídeo/HTTP é linkada — curl/mpv/yt-dlp
# são invocados como processos externos (ver docs/memory/architecture.md).
target_link_libraries(${PROJECT_NAME} PRIVATE raylib X11::X11 Threads::Threads)

# Define o diretório de saída do executável
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
