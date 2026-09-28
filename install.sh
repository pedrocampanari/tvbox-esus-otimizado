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
  unclutter-xfixes \
  mpv \
  curl \
  python3 \
  python3-pip \
  chromium \
  ir-keytable \
  triggerhappy \
  alsa-utils

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

# Kiosk fechado não pode mostrar NENHUMA mensagem/barra do próprio
# Chromium (aviso de tradução automática, aviso de flag de linha de
# comando não suportada por causa do --no-sandbox em exec.sh, etc). Só
# flag de linha de comando não é confiável pra isso (confirmado: a
# barra de tradução apareceu mesmo com --disable-translate). A forma
# suportada de verdade é política de enterprise via JSON — funciona
# tanto no binário `chromium` (Debian) quanto `chromium-browser`
# (derivados), então escreve nos dois diretórios possíveis.
log "Configurando políticas do Chromium (desliga tradutor + aviso de flag não suportada)"
for policy_dir in /etc/chromium/policies/managed /etc/chromium-browser/policies/managed; do
  $SUDO mkdir -p "$policy_dir"
  $SUDO tee "$policy_dir/tvbox-esus-kiosk.json" > /dev/null <<'JSON'
{
  "TranslateEnabled": false,
  "CommandLineFlagSecurityWarningsEnabled": false
}
JSON
done

# Controle remoto IR que vem com o hardware (receptor gpio_ir_recv, já
# reconhecido pelo kernel/device-tree do RK3229 — confirmado via `dmesg`
# e `ir-keytable -t` num dispositivo real). O que falta não é hardware
# nem decodificação (o protocolo `necx` já decodifica os scancodes
# perfeitamente), é o KEYMAP: sem ele o kernel só emite EV_MSC(scancode),
# nunca EV_KEY, então nenhuma tecla chega em lugar nenhum. Scancodes
# abaixo capturados e confirmados ao vivo (`ir-keytable -t`) num
# controle real desta unidade — ver docs/memory/known-issues.md.
log "Gravando keymap do controle remoto IR (POWER/VOL+/VOL-/MUTE)"
$SUDO mkdir -p /etc/rc_keymaps
$SUDO tee /etc/rc_keymaps/rc-rk322x-tvbox.toml > /dev/null <<'TOML'
[[protocols]]
name = "rc-rk322x-tvbox"
protocol = "nec"
variant = "necx"
[protocols.scancodes]
0x50540 = "KEY_POWER"
0x5054c = "KEY_VOLUMEUP"
0x50541 = "KEY_VOLUMEDOWN"
0x50518 = "KEY_MUTE"
TOML

# Regra própria em vez de confiar no casamento automático do
# /etc/rc_maps.cfg do pacote ir-keytable: mesmo padrão de decisão que já
# usamos pro --gpu-context=x11egl do mpv (determinístico > autodetecção).
# Dispara toda vez que a interface rc0 aparece (boot ou hot-plug) e
# aplica o keymap acima explicitamente, sem depender do driver reportar
# o "Default keymap" certo nem do formato do rc_maps.cfg da distro.
log "Instalando regra udev para carregar o keymap ao detectar o receptor IR"
$SUDO tee /etc/udev/rules.d/99-tvbox-ir-remote.rules > /dev/null <<'UDEV'
ACTION=="add", SUBSYSTEM=="rc", KERNEL=="rc[0-9]*", RUN+="/usr/bin/ir-keytable -w /etc/rc_keymaps/rc-rk322x-tvbox.toml -s $kernel"
UDEV
$SUDO udevadm control --reload-rules
$SUDO udevadm trigger --subsystem-match=rc

# triggerhappy: daemon minúsculo (não precisa de X, não precisa de
# desktop) que converte eventos de tecla (agora gerados pelo keymap
# acima) em comandos reais. Alternativa mais pesada seria escutar
# /dev/input diretamente dentro do app C++, mas o controle remoto não é
# parte da UI do kiosk (não navega slides) — é controle de energia/som
# do aparelho, então fica fora do binário principal.
log "Configurando triggerhappy (POWER = poweroff; VOL+/VOL-/MUTE = ALSA)"
$SUDO tee /usr/local/bin/tvbox-volume > /dev/null <<'SH'
#!/usr/bin/env bash
# Ajusta o primeiro mixer ALSA disponível. Detecta o nome do controle em
# vez de assumir "Master" porque a saída de áudio deste hardware é via
# HDMI (snd_soc_hdmi_codec) e o nome do controle simples varia por board
# — alguns nem expõem controle de volume por software (TV controla o
# volume nesse caso), então falha em silêncio (log, não erro) se não
# houver nenhum.
set -euo pipefail
control="$(amixer scontrols 2>/dev/null | head -n1 | sed -E "s/^Simple mixer control '([^']+)'.*/\1/")"
if [ -z "$control" ]; then
  logger -t tvbox-volume "nenhum mixer ALSA encontrado (provável saída HDMI sem volume por software) — ignorando"
  exit 0
fi
case "$1" in
  up)     amixer -q sset "$control" 5%+ ;;
  down)   amixer -q sset "$control" 5%- ;;
  toggle) amixer -q sset "$control" toggle ;;
esac
SH
$SUDO chmod +x /usr/local/bin/tvbox-volume

$SUDO mkdir -p /etc/triggerhappy/triggers.d
$SUDO tee /etc/triggerhappy/triggers.d/tvbox-remote.conf > /dev/null <<'THD'
KEY_POWER      1  /usr/sbin/poweroff
KEY_VOLUMEUP   1  /usr/local/bin/tvbox-volume up
KEY_VOLUMEDOWN 1  /usr/local/bin/tvbox-volume down
KEY_MUTE       1  /usr/local/bin/tvbox-volume toggle
THD
$SUDO systemctl enable --now triggerhappy

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
echo -n "controle remoto (rc0): "
if [ -e /sys/class/rc/rc0 ]; then echo "detectado"; else echo "NÃO detectado (ver docs/memory/known-issues.md)"; fi
echo -n "triggerhappy:          "; systemctl is-active triggerhappy 2>/dev/null || echo "inativo"

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
