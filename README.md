# TV Box e-SUS Otimizado

Player de kiosk em C++ (Raylib + mpv + yt-dlp) para telas informativas da
Secretaria Municipal de Saúde, rodando em Armbian (Debian) + Xorg num
Rockchip RK3229 (2GB RAM / 16GB flash). Réplica funcional — sem iframe —
de `https://esustv.jfbatl.com.br/display`, fixa em 25% de largura e 100%
de altura da tela.

## Leitura obrigatória antes de mexer no código

Nesta ordem:
1. Este README.
2. [`docs/memory/session-handoff.md`](docs/memory/session-handoff.md) —
   o que foi feito, o que foi de fato verificado, o que falta.
3. [`docs/memory/known-issues.md`](docs/memory/known-issues.md) —
   limitações conhecidas e decisões em aberto (tem pelo menos um
   bloqueio real para uso em produção, ver item 1).
4. Conforme a área que for mexer:
   - Frontend/UI/dados do site original:
     [`docs/memory/frontend-contract.md`](docs/memory/frontend-contract.md)
   - Arquitetura/build/otimização:
     [`docs/memory/architecture.md`](docs/memory/architecture.md)

## Estrutura

```
apps/tvbox_esus_app.cpp   # entrypoint único (main + loop do kiosk)
include/                  # headers dos módulos
src/                      # implementação dos módulos (scraper, player, video_cache, ui, remote_control, procexec, campaign_store)
config/campaigns.conf     # lista FIXA de vídeos/campanhas (fonte de conteúdo padrão, ver known-issues)
assets/fonts/             # Liberation Sans (tipografia formal, SIL OFL)
cache/videos/             # vídeos baixados em runtime + manifest.tsv (gerado, fora do git)
CMakeLists.txt            # build canônico (resolve Raylib via FetchContent)
Makefile                  # atalho fino sobre o CMake
docs/memory/              # memória do projeto (ver acima)
```

> O binário resolve `assets/` e `config/` como caminhos relativos ao
> diretório de trabalho atual — rode-o sempre com a raiz do projeto
> como cwd (`make run` já faz isso). Ao copiar pro dispositivo final,
> leve `assets/` e `config/` junto do binário.

## Build

```sh
make          # configura e compila via CMake em build/
make run      # compila (se preciso) e executa
make clean    # remove artefatos de build
```

Dependências de desenvolvimento (para compilar): `cmake`, `g++` (C++17),
headers de desenvolvimento do X11 — **não é só `libx11-dev`**: o GLFW
(empacotado dentro do Raylib) também precisa de RandR/Xinerama/Xcursor/
Xi/Xext, então use o metapacote `xorg-dev` (mais `libgl1-mesa-dev` pros
headers de OpenGL) — é isso que `install.sh --build` instala. O Raylib
em si é baixado e compilado automaticamente pelo CMake.

Dependências de runtime no dispositivo (não precisam estar presentes
para compilar, só para o app funcionar de verdade em produção):
`mpv`, `yt-dlp` (+ `python3`), `ffmpeg` (cache de vídeos: junta
vídeo+áudio baixados, sem recodificar), `curl`, `chromium` (painel institucional,
ver seção "Rodar em modo kiosk" abaixo), `x11-xserver-utils` (`xrandr`/
`xset`), `unclutter-xfixes` (esconde o cursor do mouse), e
`ir-keytable`/`triggerhappy`/`alsa-utils` (controle remoto IR do
hardware — liga/desliga e volume, ver
[`docs/memory/known-issues.md`](docs/memory/known-issues.md) item -6),
e `wpasupplicant`/`isc-dhcp-client` (conexão WiFi via dongle USB
externo — o WiFi onboard desta placa é conhecidamente quebrado sob
Armbian, sem fix disponível; ver item -7 do mesmo arquivo).

> Fluxo recomendado: compilar num host de desenvolvimento (ou CI) e
> copiar apenas o binário final para o dispositivo Armbian — não é
> necessário instalar toolchain de build no RK3229.

### Instalação das dependências

```sh
./install.sh          # só runtime (X11, mpv, ffmpeg, yt-dlp, curl, áudio HDMI, controle remoto)
./install.sh --build   # runtime + cmake/g++/libx11-dev (pra compilar no próprio dispositivo)
```

⚠️ `yt-dlp` **nunca** via `apt` — o pacote do Debian trava numa versão
antiga que para de funcionar contra o YouTube. O script instala via
`pip3 install --user`. Ver `docs/memory/known-issues.md` item 5.

### Rodar em modo kiosk (sem ambiente de desktop)

`exec.sh` cuida de entrar no diretório certo (pros caminhos relativos
de `assets/`/`config/` funcionarem), desligar blank/DPMS/screensaver do
X, reiniciar o app sozinho se ele cair, **e subir o Chromium em modo
app** (sem barra de endereço/abas) mostrando o painel institucional nos
outros 75% da tela (o app de vídeo fica ancorado nos 25% da direita —
ver `kAnchorWindowToRightEdge` em `include/config.h`). Aponte o
`~/.xinitrc` pra ele:

```sh
echo 'exec /caminho/completo/para/tvbox-esus-otimizado/exec.sh' > ~/.xinitrc
chmod +x ~/.xinitrc
startx
```

**Painel institucional (Chromium)**: URL fixa em `exec.sh`
(`PANEL_URL`), pode ser sobrescrita sem editar o script:
```sh
PANEL_URL="https://outra.url/painel" startx
```
Pra desligar o painel e rodar só o vídeo:
```sh
PANEL_ENABLED=0 startx
```

Por que Chromium e não WPE WebKit/Cog (mais leve): o pacote `cog` do
Debian só tem renderização DRM/Wayland/headless, sem X11 — rodar ele
exigiria um compositor Wayland por baixo, reabrindo o problema de
`--wid` sendo ignorado que já tivemos que resolver à força (ver
`docs/memory/known-issues.md` item 5). Chromium em modo `--app` é um
cliente X11 comum, sem esse conflito.

Logs ficam na raiz do projeto: `kiosk.log` (app de vídeo, inclui as
linhas `VideoCache:` de download/exibição) e `panel.log` (Chromium).

### Atualizar o dispositivo

```sh
cd /root/tvbox-esus-otimizado
git pull
./install.sh      # só se install.sh mudou (pacotes/configs de sistema)
make              # sempre recompila o que mudou
pkill -x tvbox_esus_app   # exec.sh sobe o binário novo em 2s
```

**Nenhuma mensagem do navegador aparece na tela** (aviso de tradução
automática, aviso de "flag de linha de comando não suportada" causado
pelo `--no-sandbox` que o kiosk real precisa por rodar como root,
etc.) — `install.sh` já escreve uma política de enterprise do Chromium
(`/etc/chromium/policies/managed/tvbox-esus-kiosk.json`) desligando
isso. Rodar `exec.sh` sem ter passado pelo `install.sh` faz essas
mensagens voltarem a aparecer.

## Estado atual

**Validado no RK3229 real (2026-09-29, via SSH)**: vídeo tocando do
cache local dentro da área reservada, header/footer com acentuação
correta, volume do controle remoto com indicador no rodapé.

O app compila, abre a janela no tamanho/posição corretos (canto
superior direito, 25% largura x 100% altura) e desenha o "chrome"
(header azul/footer vermelho, cada um com título+subtítulo) com a
paleta e tipografia (Liberation Sans) confirmadas ao vivo no site
original. O conteúdo é uma lista **fixa** de campanhas
(`config/campaigns.conf`), por autorização do dono do sistema — sem
scraping em runtime. Os 10 `video_url` reais já estão preenchidos.
Os vídeos são **baixados em disco** (`cache/videos/`, uma vez por boot
do dispositivo, em segundo plano, sempre o próximo da rotação) e
tocados pelo `mpv` embutido como janela filha; cada vídeo é exibido
inteiro antes de passar ao próximo slide. Ver
[`docs/memory/architecture.md`](docs/memory/architecture.md) > "Cache de
vídeos". Pendências reais: WiFi onboard quebrado (só com dongle USB,
item -7) e
`yt-dlp` sem runtime JavaScript (item -9) em
[`docs/memory/known-issues.md`](docs/memory/known-issues.md).
