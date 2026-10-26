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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(id -u)" = "0" ]; then
  SUDO=""
else
  SUDO="sudo"
fi

log "Atualizando índices do apt"
$SUDO apt update

# ffmpeg: o cache de vídeos (include/video_cache.h) baixa vídeo H.264 e
# áudio separados via yt-dlp, que precisa do ffmpeg pra juntar os dois
# num .mp4 (só cópia de stream, sem recodificar — qualidade intacta).
# As libs pesadas (libavcodec etc.) já vêm com o mpv; o pacote em si é
# pequeno.
log "Instalando dependências de runtime (X11, mpv, ffmpeg, curl, python3, chromium)"
$SUDO apt install -y \
  xserver-xorg \
  xinit \
  x11-xserver-utils \
  unclutter-xfixes \
  mpv \
  ffmpeg \
  quickjs \
  curl \
  python3 \
  python3-pip \
  chromium \
  ir-keytable \
  triggerhappy \
  alsa-utils \
  wpasupplicant \
  iw \
  netplan.io \
  isc-dhcp-client

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
#
# yt-dlp-ejs + quickjs: o YouTube passou a exigir um runtime JavaScript
# pra decifrar os formatos ("No supported JavaScript runtime could be
# found ... some formats may be missing"). deno (o padrão do yt-dlp) não
# tem build pra armv7, e nodejs puxaria ~80MB (libicu + libnode);
# quickjs é 1,2MB e, medido no RK3229, extrai no mesmo tempo (~9s). Só
# o pacote yt-dlp-ejs (Python puro) — o extra "yt-dlp[default]" falha
# no armv7 (brotli sem wheel). Ver docs/memory/known-issues.md item -9.
log "Instalando/atualizando yt-dlp + yt-dlp-ejs via pip (nunca via apt)"
pip3 install --user --upgrade --break-system-packages yt-dlp yt-dlp-ejs

# Config de sistema: vale pro cache de vídeos E pro ytdl_hook do mpv.
log "Configurando yt-dlp pra usar quickjs como runtime JavaScript"
$SUDO tee /etc/yt-dlp.conf > /dev/null <<'YTDLP'
# TV Box e-SUS: runtime JS pro YouTube (install.sh; ver docs/memory/known-issues.md item -9)
--js-runtimes quickjs
YTDLP

# yt-dlp desatualizado para de funcionar contra o YouTube (item 5 de
# known-issues). `yt-dlp -U` não serve pra instalação via pip — um timer
# semanal atualiza pelo pip, de madrugada (downloads do cache só
# acontecem logo após o boot, então não há disputa).
log "Instalando timer semanal de atualização do yt-dlp"
YTDLP_HOME="$HOME"
$SUDO tee /etc/systemd/system/tvbox-ytdlp-update.service > /dev/null <<UNIT
[Unit]
Description=TV Box e-SUS: atualiza yt-dlp e yt-dlp-ejs via pip
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
User=$(id -un)
Environment=HOME=$YTDLP_HOME
Nice=19
ExecStart=/usr/bin/pip3 install --user --upgrade --break-system-packages yt-dlp yt-dlp-ejs
UNIT
$SUDO tee /etc/systemd/system/tvbox-ytdlp-update.timer > /dev/null <<'UNIT'
[Unit]
Description=TV Box e-SUS: atualização semanal do yt-dlp

[Timer]
OnCalendar=Sun *-*-* 03:30:00
RandomizedDelaySec=30min
Persistent=true

[Install]
WantedBy=timers.target
UNIT
$SUDO systemctl daemon-reload
$SUDO systemctl enable --now tvbox-ytdlp-update.timer

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
# VOL+/VOL-/MUTE re-capturados botão a botão em 2026-09-30: o mapeamento
# de 2026-09-27 estava deslocado (VOL- somava, VOL+ não fazia nada).
# O keymap fica versionado em config/rc-rk322x-tvbox.toml (adicionar um
# botão novo = editar uma linha lá e rodar este script de novo).
log "Gravando keymap do controle remoto IR (config/rc-rk322x-tvbox.toml)"
$SUDO mkdir -p /etc/rc_keymaps
$SUDO install -m 644 "$SCRIPT_DIR/config/rc-rk322x-tvbox.toml" /etc/rc_keymaps/rc-rk322x-tvbox.toml

# Regra própria em vez de confiar no casamento automático do
# /etc/rc_maps.cfg do pacote ir-keytable: mesmo padrão de decisão que já
# usamos pro --gpu-context=x11egl do mpv (determinístico > autodetecção).
# Dispara toda vez que a interface rc0 aparece (boot ou hot-plug) e
# aplica o keymap acima explicitamente, sem depender do driver reportar
# o "Default keymap" certo nem do formato do rc_maps.cfg da distro.
# `-c` limpa o keymap atual antes do `-w`: sem ele, reaplicar com o
# aparelho ligado SOMA os códigos novos aos antigos (um mapeamento errado
# anterior continuaria valendo até o próximo boot).
log "Instalando regra udev para carregar o keymap ao detectar o receptor IR"
$SUDO tee /etc/udev/rules.d/99-tvbox-ir-remote.rules > /dev/null <<'UDEV'
ACTION=="add", SUBSYSTEM=="rc", KERNEL=="rc[0-9]*", RUN+="/usr/bin/ir-keytable -c -w /etc/rc_keymaps/rc-rk322x-tvbox.toml -s $kernel"
UDEV
$SUDO udevadm control --reload-rules
# --action=add é obrigatório aqui: sem essa flag o udevadm trigger manda
# ACTION=change por padrão, que a regra acima (ACTION=="add") ignora —
# nesse caso o keymap só seria aplicado no PRÓXIMO boot (quando o kernel
# de fato cria rc0 do zero), nunca imediatamente após instalar a regra.
$SUDO udevadm trigger --action=add --subsystem-match=rc

# O Xorg pega o gpio_ir_recv como teclado ("libinput keyboard catchall",
# visto no Xorg.0.log do RK3229 em 2026-10-26): sem isto, cada botão do
# controle vira também uma tecla na janela em foco — o 1/2/3 do manual
# seriam digitados no painel e-SUS e o HOME rolaria a página. Ninguém
# precisa do controle via X: o app e o triggerhappy leem /dev/input
# direto. Vale a partir do próximo startx (reboot).
log "Fazendo o Xorg ignorar o receptor IR (o app lê /dev/input direto)"
$SUDO mkdir -p /etc/X11/xorg.conf.d
$SUDO tee /etc/X11/xorg.conf.d/90-tvbox-ignore-ir.conf > /dev/null <<'XORGIR'
Section "InputClass"
    Identifier "tvbox: controle IR fora do X"
    MatchProduct "gpio_ir_recv"
    Option "Ignore" "on"
EndSection
XORGIR

# triggerhappy: daemon minúsculo (não precisa de X, não precisa de
# desktop) que converte KEY_POWER segurado em reboot. VOL+/VOL-/MUTE/
# HOME/1/2/3 NÃO ficam aqui: o próprio binário C++ lê /dev/input diretamente (ver
# include/remote_control.h) pra poder desenhar o indicador no rodapé, e
# ele mesmo chama o `amixer` — deixar o triggerhappy reagindo a essas
# teclas TAMBÉM duplicaria o ajuste. POWER continua aqui de propósito:
# o reboot é a ferramenta de recuperação justamente quando o app trava,
# então não pode depender dele (o app só desenha a contagem regressiva).
#
# POWER segurado (manual da BTV11: "segurar ~5 s para reiniciar"): o
# toque (valor 1) arma um reboot para daqui a POWER_HOLD_SECONDS; a
# soltura (valor 0) desarma. Toque curto não faz nada — antes, um toque
# acidental desligava o aparelho (`poweroff`) e só voltava tirando da
# tomada. Depende do receptor emitir a soltura só quando o controle para
# de repetir o código (confirmado em `ir-keytable -t`: repetição correta).
# O systemd-logind também escuta KEY_POWER de qualquer /dev/input (o
# receptor IR incluso) e, no padrão HandlePowerKey=poweroff, DESLIGA o
# aparelho num toque curto — passando por cima do triggerhappy. Visto no
# RK3229 em 2026-10-26 ("Power key pressed short. Powering off..."). Só
# o triggerhappy decide o que o POWER faz.
log "Desligando a reação do systemd-logind à tecla POWER"
$SUDO mkdir -p /etc/systemd/logind.conf.d
$SUDO tee /etc/systemd/logind.conf.d/tvbox-power-key.conf > /dev/null <<'LOGIND'
[Login]
HandlePowerKey=ignore
HandlePowerKeyLongPress=ignore
LOGIND
# reload (SIGHUP) relê a configuração sem derrubar as sessões abertas.
$SUDO systemctl reload systemd-logind 2>/dev/null || $SUDO kill -HUP "$(pidof systemd-logind)" 2>/dev/null || true

log "Configurando triggerhappy (POWER segurado 5s = reboot)"
$SUDO tee /usr/local/sbin/tvbox-power-hold > /dev/null <<'POWERHOLD'
#!/bin/sh
# Chamado pelo triggerhappy: `press` arma o reboot, `release` desarma.
# Manter POWER_HOLD_SECONDS igual a kPowerRebootHoldSeconds
# (include/config.h), que só alimenta a contagem na tela.
POWER_HOLD_SECONDS=5
PIDFILE=/run/tvbox-power-hold.pid
case "$1" in
  press)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
      exit 0
    fi
    (
      sleep "$POWER_HOLD_SECONDS"
      rm -f "$PIDFILE"
      logger -t tvbox-power "POWER segurado ${POWER_HOLD_SECONDS}s: reiniciando"
      sync
      /usr/sbin/reboot
    ) > /dev/null 2>&1 &
    echo $! > "$PIDFILE"
    ;;
  release)
    if [ -f "$PIDFILE" ]; then
      kill "$(cat "$PIDFILE")" 2>/dev/null
      rm -f "$PIDFILE"
    fi
    ;;
esac
POWERHOLD
$SUDO chmod 755 /usr/local/sbin/tvbox-power-hold

$SUDO mkdir -p /etc/triggerhappy/triggers.d
$SUDO tee /etc/triggerhappy/triggers.d/tvbox-remote.conf > /dev/null <<'THD'
KEY_POWER 1 /usr/local/sbin/tvbox-power-hold press
KEY_POWER 0 /usr/local/sbin/tvbox-power-hold release
THD

# O unit do Debian roda o thd com `--user nobody`, que não pode reiniciar
# o sistema nem gravar em /run. Override sem o --user, derivado do
# ExecStart instalado (não chuta caminho/flags de outra versão do pacote).
thd_exec="$(systemctl cat triggerhappy 2>/dev/null | sed -n 's/^ExecStart=//p' | tail -n 1 || true)"
case "$thd_exec" in
  *"--user nobody"*)
    $SUDO mkdir -p /etc/systemd/system/triggerhappy.service.d
    printf '[Service]\nExecStart=\nExecStart=%s\n' "${thd_exec/ --user nobody/}" \
      | $SUDO tee /etc/systemd/system/triggerhappy.service.d/tvbox-root.conf > /dev/null
    $SUDO systemctl daemon-reload
    ;;
esac
$SUDO systemctl enable triggerhappy
$SUDO systemctl restart triggerhappy

# Áudio pelo HDMI com volume por software. Diagnóstico no dispositivo
# real (2026-09-29): NENHUMA placa ALSA (analog/SPDIF/HDMI) expõe
# controle de mixer — `amixer scontrols` vazio —, então VOL+/VOL-/MUTE
# do controle remoto nunca tinham o que ajustar; e a placa padrão era a
# analógica (card 0), não o HDMI da TV. Este arquivo:
#   - `dmix`: vários processos tocando juntos no HDMI (Chromium +
#     eventuais outros), que sozinho só aceita um stream;
#   - `softvol`: cria o controle "Master" (0-100%) que o app ajusta;
#   - `pcm.!default`/`ctl.!default` apontando pro HDMI.
# Arquivo separado em /etc/alsa/conf.d em vez de editar /etc/asound.conf
# (que pertence ao pacote armbian-bsp-cli e seria sobrescrito num
# upgrade). Reversível: apagar o arquivo.
log "Configurando áudio HDMI com volume por software (softvol + dmix)"
$SUDO mkdir -p /etc/alsa/conf.d
$SUDO tee /etc/alsa/conf.d/99-tvbox-hdmi-softvol.conf > /dev/null <<'ALSA'
# TV Box e-SUS: saída padrão = HDMI, com volume por software ("Master").
# Ver install.sh e docs/memory/known-issues.md item -8.
pcm.tvbox_hdmi_dmix {
    type dmix
    ipc_key 3229
    ipc_perm 0666
    slave {
        pcm "hw:HDMI,0"
        rate 48000
    }
}
pcm.tvbox_softvol {
    type softvol
    slave.pcm "tvbox_hdmi_dmix"
    control {
        name "Master"
        card HDMI
    }
    min_dB -51.0
    max_dB 0.0
}
pcm.!default {
    type plug
    slave.pcm "tvbox_softvol"
}
ctl.!default {
    type hw
    card HDMI
}
ALSA
# Abre o PCM padrão uma vez pra o controle "Master" passar a existir.
timeout 3 aplay -q -D default -f S16_LE -r 48000 -c 2 -s 4800 /dev/zero 2>/dev/null || true

# WiFi onboard (chip SSV6051, driver ssv6051) é conhecidamente quebrado
# nesta placa sob Armbian: escritas de registro via SDIO são confirmadas
# pelo barramento mas nunca chegam no chip de verdade (bug documentado
# na comunidade, não é algo que resolvemos ou vamos resolver por
# software — ver docs/memory/known-issues.md item -7 para o
# investigação completa e a fonte). Caminho adotado: dongle USB WiFi
# externo (chipset Realtek RTL8188EUS/RTL8192EU recomendado — suporte
# nativo no kernel, plug-and-play). Blacklist do driver quebrado evita
# ~4s de tentativas de calibração fadadas ao fracasso a cada boot e o
# ruído de WARNs no dmesg (que também podia confundir diagnóstico
# futuro deste projeto). Reversível: apagar o arquivo abaixo se algum
# dia quiser tentar o chip onboard de novo (ex.: driver corrigido
# upstream).
log "Desabilitando driver WiFi onboard quebrado (ssv6051) — ver known-issues item -7"
$SUDO tee /etc/modprobe.d/blacklist-ssv6051-wifi.conf > /dev/null <<'MODPROBE'
# SSV6051 (WiFi onboard do RK3229 TV box): escritas de registro via SDIO
# nao persistem no chip sob Armbian (bug documentado, nao ha fix
# conhecido). Ver docs/memory/known-issues.md item -7. Usamos dongle USB
# WiFi externo em vez disso.
blacklist ssv6051
MODPROBE

# wpasupplicant + isc-dhcp-client (instalados acima) bastam pra conectar
# um dongle USB WiFi na mao, sem precisar de NetworkManager (daemon mais
# pesado, desnecessario aqui — a conexao e configurada uma vez e fica
# fixa, nao precisa de gerenciamento continuo/roaming). Ver README para
# o passo a passo de conexão.
log "Verificando adaptador USB WiFi conectado"
if command -v lsusb >/dev/null 2>&1 && lsusb 2>/dev/null | grep -qiE 'realtek|ralink|atheros|mediatek|wireless'; then
  echo "Adaptador USB WiFi detectado:"
  lsusb | grep -iE 'realtek|ralink|atheros|mediatek|wireless'
else
  echo "Nenhum adaptador USB WiFi detectado ainda (ok se ainda nao conectou o dongle)."
fi

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

# Kiosk: "ligou na tomada, aparece o app" (requisito do projeto). Até
# 2026-09-30 nada disso existia — depois de um boot o dispositivo ficava
# no login do console e alguém tinha que rodar `startx` na mão.
#   1. autologin no tty1 (override do getty@tty1);
#   2. ~/.xinitrc -> exec.sh deste diretório;
#   3. ~/.profile: login no tty1 sem X rodando -> `exec startx`. Se o X
#      cair, o shell sai, o getty loga de novo e o X sobe outra vez.
#      Logins por SSH ou em outro tty não são afetados.
# `-nocursor` esconde o cursor no próprio Xorg (o unclutter do exec.sh
# vira só redundância).
KIOSK_USER="$(id -un)"
log "Configurando início automático do kiosk (autologin tty1 + startx) para '$KIOSK_USER'"
$SUDO mkdir -p /etc/systemd/system/getty@tty1.service.d
$SUDO tee /etc/systemd/system/getty@tty1.service.d/tvbox-autologin.conf > /dev/null <<GETTY
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $KIOSK_USER --noclear %I \$TERM
GETTY
$SUDO systemctl daemon-reload

printf 'exec %s/exec.sh\n' "$SCRIPT_DIR" > "$HOME/.xinitrc"
chmod +x "$HOME/.xinitrc"

if ! grep -qF '>>> tvbox-esus kiosk >>>' "$HOME/.profile" 2>/dev/null; then
  cat >> "$HOME/.profile" <<'PROFILE'

# >>> tvbox-esus kiosk >>> (install.sh — sobe o kiosk ao ligar)
if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = /dev/tty1 ]; then
  exec startx -- -nocursor >/dev/null 2>&1
fi
# <<< tvbox-esus kiosk <<<
PROFILE
fi

log "Verificação"
echo -n "mpv:      "; mpv --version | sed -n 1p
echo -n "yt-dlp:   "; "$LOCAL_BIN/yt-dlp" --version
echo -n "curl:     "; curl --version | sed -n 1p
echo -n "chromium: "; (command -v chromium || command -v chromium-browser) >/dev/null 2>&1 \
  && (chromium --version 2>/dev/null || chromium-browser --version 2>/dev/null) \
  || echo "não encontrado"
if [ "$WITH_BUILD_DEPS" = true ]; then
  echo -n "cmake:  "; cmake --version | sed -n 1p
  echo -n "g++:    "; g++ --version | sed -n 1p
fi
echo -n "controle remoto (rc0): "
if [ -e /sys/class/rc/rc0 ]; then echo "detectado"; else echo "NÃO detectado (ver docs/memory/known-issues.md)"; fi
echo -n "triggerhappy:          "; systemctl is-active triggerhappy 2>/dev/null || echo "inativo"
echo -n "runtime JS (yt-dlp):   "; "$LOCAL_BIN/yt-dlp" -v --simulate --no-warnings "https://www.youtube.com/watch?v=rx-MuBPAWPM" 2>&1 | grep -o "JS runtimes: .*" | sed -n 1p || echo "NÃO detectado (ver known-issues item -9)"
echo -n "autostart (tty1):      "; [ -f /etc/systemd/system/getty@tty1.service.d/tvbox-autologin.conf ] && echo "ok (reinicie pra ver o kiosk subir sozinho)" || echo "não configurado"
echo -n "ffmpeg:                "; command -v ffmpeg >/dev/null 2>&1 && echo "ok" || echo "NÃO encontrado (cache de vídeos não consegue juntar vídeo+áudio)"
echo -n "volume (Master):       "; amixer get Master 2>/dev/null | grep -o '\[[0-9]*%\]' | sed -n 1p || echo "controle não encontrado"
echo -n "wifi onboard (ssv6051): "
if lsmod 2>/dev/null | grep -q '^ssv6051'; then
  echo "carregado (blacklist nao aplicada ainda? reboot pendente)"
else
  echo "bloqueado (esperado — ver known-issues item -7, use dongle USB)"
fi

echo
echo "Pronto. O yt-dlp se atualiza sozinho toda semana (tvbox-ytdlp-update.timer)."
echo "Pra forçar agora: sudo systemctl start tvbox-ytdlp-update.service"
echo
echo "WiFi onboard (SSV6051) e conhecidamente quebrado nesta placa — use"
echo "um dongle USB WiFi (Realtek RTL8188EUS/RTL8192EU recomendado)."
echo "Depois de conectar o dongle, pra configurar a rede DE FORMA"
echo "PERSISTENTE (sobrevive a reboot/religar da tomada — ver"
echo "docs/memory/known-issues.md item -7 pro passo a passo completo):"
echo "  ip link                      # confirme o nome da interface (ex.: wlan1)"
echo "  sudo tee -a /etc/network/interfaces <<CFG"
echo "  auto wlan1"
echo "  iface wlan1 inet dhcp"
echo "      wpa-ssid \"SEU_SSID\""
echo "      wpa-psk  \"SUA_SENHA\""
echo "  CFG"
echo "  sudo ifup wlan1   # ou reboot"
echo
echo "O kiosk (+ painel institucional no Chromium) sobe sozinho no próximo"
echo "boot (autologin no tty1 -> startx -> exec.sh). Pra testar agora: reboot"
echo
echo "Pra desligar o painel do Chromium e rodar só o vídeo, edite"
echo "PANEL_ENABLED em exec.sh."
