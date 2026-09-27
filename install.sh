#!/usr/bin/env bash
# Instala as dependências do TV Box e-SUS Otimizado num Armbian/Debian.
#
# Uso:
#   ./install.sh              # só dependências de RUNTIME (o que o
#                              # dispositivo precisa pra rodar o binário
#                              # já compilado)
#   ./install.sh --build      # runtime + dependências de BUILD (cmake,
#                              # g++, libx11-dev, git) — só necessário se
#                              # for compilar no próprio dispositivo
#
# Ver docs/memory/architecture.md e known-issues.md pro porquê de cada
# escolha (em especial: yt-dlp NUNCA via apt, ver seção abaixo).

set -euo pipefail

WITH_BUILD_DEPS=false
for arg in "$@"; do
  case "$arg" in
    --build) WITH_BUILD_DEPS=true ;;
    -h|--help)
      sed -n '2,15p' "$0"
      exit 0
      ;;
    *)
      echo "Opção desconhecida: $arg (use --build ou --help)" >&2
      exit 1
      ;;
  esac
done

log() { printf '\n==> %s\n' "$1"; }

if [ "$(id -u)" = "0" ]; then
  SUDO=""
else
  SUDO="sudo"
fi

log "Atualizando índices do apt"
$SUDO apt update

log "Instalando dependências de runtime (X11, mpv, curl, python3, chromium)"
$SUDO apt install -y \
  xserver-xorg \
  xinit \
  x11-xserver-utils \
  mpv \
  curl \
  python3 \
  python3-pip \
  chromium

if [ "$WITH_BUILD_DEPS" = true ]; then
  # xorg-dev: metapacote oficial que o próprio GLFW recomenda pra
  # compilar no X11 — cobre libx11-dev + libxrandr-dev + libxinerama-dev
  # + libxcursor-dev + libxi-dev + libxext-dev de uma vez. Só libx11-dev
  # sozinho NÃO é suficiente (confirmado: "RandR headers not found" ao
  # compilar sem os outros). libgl1-mesa-dev: headers de OpenGL que o
  # raylib precisa (GRAPHICS_API_OPENGL_33).
  log "Instalando dependências de build (--build): cmake, g++, xorg-dev, libgl1-mesa-dev, git"
  $SUDO apt install -y cmake g++ xorg-dev libgl1-mesa-dev git
fi

# yt-dlp NUNCA via apt: o pacote do Debian trava numa versão antiga que
# simplesmente para de funcionar contra o YouTube (confirmado testando
# de verdade — ver docs/memory/known-issues.md item 5). Instala via pip
# --user, sem venv, sem mexer em pacotes do sistema.
log "Instalando/atualizando yt-dlp via pip (nunca via apt)"
pip3 install --user --upgrade --break-system-packages yt-dlp

LOCAL_BIN="$HOME/.local/bin"
if ! echo "$PATH" | tr ':' '\n' | grep -qx "$LOCAL_BIN"; then
  log "Adicionando $LOCAL_BIN ao PATH (~/.bashrc)"
  if ! grep -qF "$LOCAL_BIN" "$HOME/.bashrc" 2>/dev/null; then
    echo "export PATH=\"$LOCAL_BIN:\$PATH\"" >> "$HOME/.bashrc"
  fi
  export PATH="$LOCAL_BIN:$PATH"
  echo "PATH atualizado nesta sessão; abra um novo terminal (ou rode"
  echo "'source ~/.bashrc') pra isso valer em sessões futuras."
fi

log "Verificação"
echo -n "mpv:      "; mpv --version | head -1
echo -n "yt-dlp:   "; "$LOCAL_BIN/yt-dlp" --version
echo -n "curl:     "; curl --version | head -1
echo -n "chromium: "; (command -v chromium || command -v chromium-browser) >/dev/null 2>&1 \
  && (chromium --version 2>/dev/null || chromium-browser --version 2>/dev/null) \
  || echo "não encontrado"
if [ "$WITH_BUILD_DEPS" = true ]; then
  echo -n "cmake:  "; cmake --version | head -1
  echo -n "g++:    "; g++ --version | head -1
fi

echo
echo "Pronto. Lembre de manter o yt-dlp atualizado de tempos em tempos:"
echo "  yt-dlp -U"
echo
echo "Pra rodar o kiosk (+ painel institucional no Chromium) direto ao"
echo "subir o X (sem gerenciador de janelas):"
echo "  echo 'exec /caminho/para/tvbox-esus-otimizado/exec.sh' > ~/.xinitrc"
echo "  chmod +x ~/.xinitrc"
echo "  startx"
echo
echo "Pra desligar o painel do Chromium e rodar só o vídeo:"
echo "  PANEL_ENABLED=0 startx"
