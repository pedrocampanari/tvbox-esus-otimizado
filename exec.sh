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

# Esconde o cursor do mouse (opcional; não é dependência obrigatória).
if command -v unclutter >/dev/null 2>&1; then
  unclutter -idle 0 &
fi

echo "$(date '+%Y-%m-%d %H:%M:%S') kiosk iniciado" >> "$LOG_FILE"

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
