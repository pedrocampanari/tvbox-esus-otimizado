#!/usr/bin/env bash
# Controla o Chromium do painel institucional (os 75% da tela) que o
# exec.sh mantém rodando em loop.
#
# Uso:
#   ./panel-ctl.sh reload   # reabre o painel (mesmo perfil: cookies e
#                           # vínculo do e-SUS preservados)
#   ./panel-ctl.sh reset    # reabre com o perfil APAGADO: cookies,
#                           # localStorage, cache e vínculo do e-SUS
#   ./panel-ctl.sh kiosk    # fecha qualquer navegador EXTRA (Chromium/
#                           # Firefox fora do perfil do kiosk); o painel
#                           # oficial e o Display não são tocados
#
# Quem chama: o app (controle: 1 → kiosk, 3 segurado 10s → reset) e o
# watchdog de rede do exec.sh (internet voltou → reload).
#
# Este script só encerra o Chromium. Quem apaga o perfil e sobe o
# navegador de novo é o loop do painel no exec.sh, DEPOIS que o processo
# saiu — apagar o perfil com o Chromium ainda aberto deixaria ele
# regravando cookies por cima.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROFILE_DIR="$SCRIPT_DIR/.chromium-kiosk-profile"
RESET_FLAG="$SCRIPT_DIR/.panel-reset-request"
PANEL_LOG="$SCRIPT_DIR/panel.log"

case "${1:-}" in
  reload) ;;
  reset) touch "$RESET_FLAG" ;;
  kiosk) ;;
  *)
    echo "uso: $0 reload|reset|kiosk" >&2
    exit 2
    ;;
esac

echo "$(date '+%Y-%m-%d %H:%M:%S') panel-ctl: $1 pedido" >> "$PANEL_LOG"

# Casa pelo --user-data-dir: pega só o Chromium do kiosk, nunca outro
# navegador que alguém tenha aberto na mão.
pattern="--user-data-dir=$PROFILE_DIR"

if [ "$1" = "kiosk" ]; then
  # Só os processos principais (sem --type=): renderer/gpu/utility saem
  # junto com eles. Sem gerenciador de janelas no X, uma janela extra
  # fica por cima do painel até o processo dono sair. Se o painel
  # oficial não estiver de pé, o loop do exec.sh já o reabre em 3s.
  for pid in $(pgrep -x 'chromium|firefox|firefox-esr'); do
    args=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null) || continue
    case "$args" in
      *--type=*|*"$pattern"*) continue ;;
    esac
    echo "$(date '+%Y-%m-%d %H:%M:%S') panel-ctl: fechando navegador extra (pid $pid)" >> "$PANEL_LOG"
    kill -TERM "$pid" 2>/dev/null
  done
  exit 0
fi
if ! pkill -TERM -f -- "$pattern"; then
  # Chromium não estava rodando (ex.: entre um reinício e outro do
  # loop). Com reset, a flag já está gravada e o loop age na próxima volta.
  exit 0
fi

# SIGTERM fecha limpo (grava o estado e sai); se travar, KILL depois de 10s.
for _ in $(seq 1 20); do
  pgrep -f -- "$pattern" > /dev/null || exit 0
  sleep 0.5
done
pkill -KILL -f -- "$pattern"
exit 0
