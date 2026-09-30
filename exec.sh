#!/usr/bin/env bash
# Lança o kiosk (TV Box e-SUS Otimizado) — pensado pra ser chamado pelo
# ~/.xinitrc dentro de uma sessão X já ativa (via `startx`).
#
# Uso no ~/.xinitrc:
#   exec /caminho/completo/para/tvbox-esus-otimizado/exec.sh
#
# O que este script faz, além de rodar o binário:
#   - resolve o próprio diretório e entra nele, pra `assets/` e
#     `config/campaigns.conf` (caminhos relativos) serem encontrados
#     não importa de onde o script foi chamado;
#   - desliga blank/DPMS/screensaver do X (senão a tela apaga sozinha
#     depois de um tempo parado — comportamento errado pra um painel
#     que deve ficar sempre ligado);
#   - esconde o cursor do mouse com `unclutter`, se estiver instalado
#     (opcional — não trava se não tiver);
#   - reinicia o binário sozinho se ele cair (crash do app, do mpv
#     puxando o processo, etc.) em vez de deixar a tela preta parada;
#   - sobe o Chromium (modo app, sem chrome de navegador) nos outros
#     75% da tela mostrando o painel institucional — ver PANEL_URL
#     abaixo. Desative com PANEL_ENABLED=0 (variável de ambiente) se
#     quiser rodar só o kiosk de vídeo.
#
# Por que Chromium e não WPE WebKit/Cog: o pacote `cog` do Debian só
# tem plugins de renderização DRM/Wayland/headless, nenhum X11 — não dá
# pra rodar como mais uma janela ao lado do nosso app sem reintroduzir
# um compositor Wayland (o que reabriria o problema de `--wid` sendo
# ignorado que já resolvemos à força — ver known-issues.md item 5).
# Chromium em modo `--app` é um cliente X11 normal, sem esse conflito.

set -u

PANEL_ENABLED="${PANEL_ENABLED:-1}"
PANEL_URL="${PANEL_URL:-https://esus.treslagoas.ms.gov.br/painel}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="$SCRIPT_DIR/build/bin/tvbox_esus_app"
LOG_FILE="$SCRIPT_DIR/kiosk.log"

if [ ! -x "$BINARY" ]; then
  echo "Binário não encontrado ou sem permissão de execução: $BINARY" >&2
  echo "Compile primeiro: make (ou make run)" >&2
  exit 1
fi

# Sem $DISPLAY, o GLFW falha ao inicializar e o Raylib crasha (segfault)
# mais na frente em vez de sair limpo — isso é uma limitação do
# GLFW/Raylib, não dá pra evitar o crash em si daqui. O que dá pra
# evitar é ficar reiniciando em loop confuso sem dizer o motivo real:
# checa isso ANTES de entrar no loop e falha rápido, com uma mensagem
# clara. Isso significa que este script está sendo chamado fora de uma
# sessão X ativa (direto num terminal/SSH, sem ter rodado `startx`
# antes) — ver o ~/.xinitrc.
if [ -z "${DISPLAY:-}" ]; then
  echo "ERRO: a variável \$DISPLAY não está definida." >&2
  echo "Este script só funciona de dentro de uma sessão X ativa" >&2
  echo "(chamado pelo ~/.xinitrc via 'startx'), não direto num" >&2
  echo "terminal/SSH comum. Rode 'startx' primeiro." >&2
  exit 1
fi

# Kiosk nunca deve apagar a tela ou cair num protetor de tela.
xset s off      2>/dev/null || true
xset s noblank  2>/dev/null || true
xset -dpms      2>/dev/null || true

# Esconde o cursor do mouse. Um kiosk sem teclado/mouse à mostra não
# pode deixar a setinha parada no meio da tela. Prefere
# `unclutter-xfixes` (usa a extensão Xfixes do X pra esconder o cursor
# de forma confiável e leve — a extensão do `unclutter` clássico é
# conhecida por falhar em esconder sobre janelas filhas, exatamente o
# tipo de janela que o `mpv` usa aqui). Cai pro `unclutter` clássico se
# só ele estiver instalado.
if command -v unclutter-xfixes >/dev/null 2>&1; then
  unclutter-xfixes -idle 0 &
elif command -v unclutter >/dev/null 2>&1; then
  unclutter -idle 0 &
fi

echo "$(date '+%Y-%m-%d %H:%M:%S') kiosk iniciado" >> "$LOG_FILE"

# Volume (controle remoto VOL+/VOL-/MUTE): o HDMI do RK3229 não tem
# volume em hardware — install.sh cria um controle "Master" por software
# (softvol, /etc/alsa/conf.d/99-tvbox-hdmi-softvol.conf). Esse controle
# só passa a existir depois que o PCM padrão é aberto uma vez; abre por
# 0,1s em silêncio e restaura o último volume salvo pelo app (ver
# src/remote_control.cpp, `alsactl store` a cada ajuste). Tudo opcional.
if [ -f /etc/alsa/conf.d/99-tvbox-hdmi-softvol.conf ]; then
  timeout 3 aplay -q -D default -f S16_LE -r 48000 -c 2 -s 4800 /dev/zero 2>/dev/null || true
  alsactl restore 2>/dev/null || true
fi

# --- Painel institucional (Chromium) nos outros 75% da tela ---
# --ozone-platform=x11 é obrigatório: sem forçar, o Chromium (assim
# como o mpv, ver known-issues.md item 5) prefere Wayland nativo sempre
# que existe um compositor Wayland alcançável, ignorando
# --window-position/--window-size (que só fazem sentido em X11) e
# nunca aparecendo como uma janela visível por fora dele. Confirmado
# testando de verdade: sem essa flag, a janela nem aparece no
# `xwininfo`; com ela, aparece corretamente com o título/classe certos.
#
# --no-sandbox: o kiosk real roda tudo como root (login direto como
# root no Armbian, sem usuário separado) e o Chromium recusa iniciar
# como root sem essa flag ("Running as root without --no-sandbox is
# not supported" — confirmado no panel.log do dispositivo real, processo
# morrendo e reiniciando em loop sem nunca abrir janela nenhuma). O
# sandbox do Chromium normalmente isola o processo de renderização do
# resto do sistema: como aqui TUDO já roda como root sem isolamento
# nenhum (nem um usuário não-privilegiado dedicado ao kiosk), perder
# essa camada extra não piora o modelo de ameaça real deste dispositivo
# fechado de exibição institucional.
#
# Nenhuma mensagem/barra do navegador pode aparecer neste kiosk (nem o
# aviso "You are using an unsupported command-line flag" causado pelo
# próprio --no-sandbox acima, nem a barra de tradução automática). Só
# flag de linha de comando NÃO é confiável pra isso (o `--disable-
# translate`/`--disable-features=Translate,TranslateUI` abaixo já
# existiam e a barra apareceu mesmo assim, testado de verdade). A forma
# suportada de verdade pelo Chromium é política de enterprise via JSON
# em /etc/chromium/policies/managed/ — `install.sh` já escreve isso
# (`TranslateEnabled: false` +
# `CommandLineFlagSecurityWarningsEnabled: false`). Se rodar sem passar
# pelo install.sh, essas mensagens voltam a aparecer.
if [ "$PANEL_ENABLED" = "1" ]; then
  CHROMIUM_BIN=""
  if command -v chromium >/dev/null 2>&1; then
    CHROMIUM_BIN="chromium"
  elif command -v chromium-browser >/dev/null 2>&1; then
    CHROMIUM_BIN="chromium-browser"
  fi

  if [ -z "$CHROMIUM_BIN" ]; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') AVISO: chromium nao encontrado no PATH — painel nao sera exibido (rode ./install.sh)" >> "$LOG_FILE"
  else
    # Detecta a resolução da tela pra calcular os 75% do painel (o
    # nosso app já ocupa os outros 25%, ancorado à direita — ver
    # include/config.h::kAnchorWindowToRightEdge). Cai num padrão
    # 1920x1080 se não conseguir detectar (ex.: xrandr indisponível).
    SCREEN_WH=$(xrandr --current 2>/dev/null | awk '
      / connected/ {
        for (i = 1; i <= NF; i++) {
          if ($i ~ /^[0-9]+x[0-9]+\+[0-9]+\+[0-9]+$/) { print $i; exit }
        }
      }')
    SCREEN_WH="${SCREEN_WH:-1920x1080+0+0}"
    SCREEN_W="${SCREEN_WH%%x*}"
    SCREEN_H="${SCREEN_WH#*x}"
    SCREEN_H="${SCREEN_H%%+*}"
    PANEL_W=$((SCREEN_W * 75 / 100))

    PANEL_PROFILE_DIR="$SCRIPT_DIR/.chromium-kiosk-profile"
    PANEL_LOG="$SCRIPT_DIR/panel.log"

    (
      while true; do
        "$CHROMIUM_BIN" \
          --app="$PANEL_URL" \
          --window-position=0,0 \
          --window-size="${PANEL_W},${SCREEN_H}" \
          --user-data-dir="$PANEL_PROFILE_DIR" \
          --ozone-platform=x11 \
          --no-sandbox \
          --noerrdialogs \
          --disable-infobars \
          --disable-session-crashed-bubble \
          --disable-translate \
          --disable-features=Translate,TranslateUI \
          --no-first-run \
          --check-for-update-interval=31536000 \
          >> "$PANEL_LOG" 2>&1
        echo "$(date '+%Y-%m-%d %H:%M:%S') painel (chromium) saiu; reiniciando em 3s" >> "$PANEL_LOG"
        sleep 3
      done
    ) &

    echo "$(date '+%Y-%m-%d %H:%M:%S') painel (chromium) iniciado: ${PANEL_URL} em ${PANEL_W}x${SCREEN_H}+0+0" >> "$LOG_FILE"
  fi
fi

# Loop de resiliência: se o app cair por qualquer motivo, reinicia
# sozinho em vez de deixar a tela preta parada. Um kiosk sem ninguém
# olhando não pode depender de alguém apertar Enter de novo.
#
# Mas se ele cair rápido demais várias vezes seguidas (crash logo na
# inicialização — driver de vídeo ruim, GL indisponível, etc.), reiniciar
# pra sempre só polui o log sem resolver nada. Desiste depois de 5
# quedas rápidas seguidas (< 5s de vida cada) e avisa claramente.
fast_crash_count=0
while true; do
  start_ts=$(date +%s)
  "$BINARY" >> "$LOG_FILE" 2>&1
  exit_code=$?
  end_ts=$(date +%s)
  echo "$(date '+%Y-%m-%d %H:%M:%S') app saiu (codigo $exit_code); reiniciando em 2s" >> "$LOG_FILE"

  if [ $((end_ts - start_ts)) -lt 5 ]; then
    fast_crash_count=$((fast_crash_count + 1))
  else
    fast_crash_count=0
  fi

  if [ "$fast_crash_count" -ge 5 ]; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') 5 quedas rápidas seguidas — desistindo. Veja $LOG_FILE." >> "$LOG_FILE"
    echo "ERRO: o app caiu 5 vezes seguidas logo na inicialização. Veja $LOG_FILE." >&2
    exit 1
  fi

  sleep 2
done
