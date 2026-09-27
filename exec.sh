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
#     puxando o processo, etc.) em vez de deixar a tela preta parada.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="$SCRIPT_DIR/build/bin/tvbox_esus_app"
LOG_FILE="$SCRIPT_DIR/kiosk.log"

if [ ! -x "$BINARY" ]; then
  echo "Binário não encontrado ou sem permissão de execução: $BINARY" >&2
  echo "Compile primeiro: make (ou make run)" >&2
  exit 1
fi

# Kiosk nunca deve apagar a tela ou cair num protetor de tela.
xset s off      2>/dev/null || true
xset s noblank  2>/dev/null || true
xset -dpms      2>/dev/null || true

# Esconde o cursor do mouse (opcional; não é dependência obrigatória).
if command -v unclutter >/dev/null 2>&1; then
  unclutter -idle 0 &
fi

echo "$(date '+%Y-%m-%d %H:%M:%S') kiosk iniciado" >> "$LOG_FILE"

# Loop de resiliência: se o app cair por qualquer motivo, reinicia
# sozinho em vez de deixar a tela preta parada. Um kiosk sem ninguém
# olhando não pode depender de alguém apertar Enter de novo.
while true; do
  "$BINARY" >> "$LOG_FILE" 2>&1
  exit_code=$?
  echo "$(date '+%Y-%m-%d %H:%M:%S') app saiu (codigo $exit_code); reiniciando em 2s" >> "$LOG_FILE"
  sleep 2
done
