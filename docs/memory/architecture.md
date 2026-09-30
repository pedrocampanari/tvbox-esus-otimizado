# Arquitetura

## Alvo de hardware
Armbian (Debian) + Xorg em modo mínimo, rodando em RK3229 (quad-core
Cortex-A7), 2GB RAM, 16GB flash. Isso descarta qualquer solução baseada em
navegador completo (Chromium/WebView) rodando continuamente — não cabe
nesse orçamento de RAM junto com decodificação de vídeo. Toda decisão
abaixo prioriza: menos processos residentes, menos libs linkadas, decode
de vídeo por hardware (VPU do RK3229 via mpv, não software).

## Visão geral dos processos
```
tvbox_esus_app (Raylib, C++)
 ├─ desenha: header, footer, slide de texto, slide de imagem (via textura)
 ├─ thread de scraping: poll HTTP a cada N segundos (default 30s, mesmo
 │  intervalo do fallback do app original) — não mantém conexão persistente
 ├─ thread de cache de vídeos (VideoCache): baixa em disco, um por vez,
 │  o PRÓXIMO vídeo da rotação (`nice 19 ionice -c3 yt-dlp` + ffmpeg
 │  pro merge) — ver "Cache de vídeos" abaixo
 └─ quando o slide ativo é vídeo:
     └─ spawna um processo `mpv --wid=<janela X11 filha> --gpu-context=x11egl
        --loop-file=no --no-audio ...` posicionado exatamente sobre a área
        do banner, tocando o ARQUIVO DO CACHE quando existe; senão
        passa a URL da campanha como está (streaming) e o próprio mpv resolve
        internamente (via seu hook `ytdl_hook` + `yt-dlp`) quando não é
        um arquivo direto, e decodifica/desenha nessa janela X11 (fora
        do pipeline Raylib/OpenGL, mas dentro da MESMA hierarquia de
        janela X11 — a janela do vídeo é filha de verdade da janela do
        Raylib, não uma segunda top-level, ver módulo player abaixo)
```

Raylib nunca desenha o vídeo em si — ele só existe pra decidir "agora é
vídeo" e posicionar/mostrar a janela X11 onde o `mpv` vai desenhar. Isso
evita puxar um decoder de vídeo pra dentro do processo gráfico principal
e deixa o `mpv` (que já sabe usar aceleração de hardware) cuidar do que
ele faz de melhor.

## Por que não uma libmpv embutida (linkada)?
Cogitado, mas rejeitado pra v1: linkar `libmpv` direto no processo Raylib
economizaria um fork, mas acopla o ciclo de vida do decoder ao processo
principal (um crash de decode derruba o app inteiro) e complica o link
(mpv precisa ser buildado com suporte a render API OpenGL). Um processo
`mpv` separado com `--wid` é mais simples de compilar num Armbian
genérico (pacote `mpv` do apt já roda), mais fácil de reiniciar sozinho
se travar, e é o padrão usado por outros kiosks Linux embarcados.
Revisitar se o overhead de fork/exec por troca de vídeo for medido como
problema real no hardware (ver [[known-issues]]).

## Módulos (`include/` + `src/` + `apps/`)
- `apps/tvbox_esus_app.cpp` — único ponto de entrada. Loop principal,
  orquestra scraper → estado de slideshow → UI/player.
- `include/campaign.h` — struct `Campaign` + enums `CampaignType`,
  `VideoOrigin` (espelham o modelo de dados do [[frontend-contract]]).
- `include/config.h` — constantes de configuração: fração da janela
  (0.25 largura / 1.0 altura), paleta de cores (tokens do
  [[frontend-contract]]), URL do display, intervalos de polling/timeout.
  Inclui `raylib.h` (por causa do tipo `Color`) — por isso **não pode
  ser incluído por `player.cpp`/`player.h`** (ver `include/video_config.h`
  logo abaixo e a nota de "Restrição de headers").
- `include/video_config.h` — constantes que `include/player.h`/
  `src/player.cpp` precisam mas que não podem vir de `config.h` (ver
  nota de "Restrição de headers" abaixo). Hoje só tem
  `kYtdlFormatSelector`; `config.h` inclui este header também, então
  código que só conhece `config.h` continua enxergando essas constantes
  normalmente.
- `include/campaign_store.h` / `src/campaign_store.cpp` — **fonte de
  conteúdo padrão (v1)**: lê `config/campaigns.conf` (formato
  chave=valor simples, documentado no cabeçalho do próprio arquivo) uma
  única vez na inicialização e monta a lista de `Campaign`. Autorizado
  pelo dono do sistema em 2026-09-26 usar uma lista FIXA de vídeos por
  enquanto, então **não há nenhuma requisição de rede em runtime** nesse
  modo — ver [[known-issues]] item 1 para o porquê e para o estado de
  preenchimento do arquivo (títulos reais confirmados, `video_url`
  ainda pendente).
- `include/scraper.h` / `src/scraper.cpp` — busca e interpreta o HTML da
  página `/display` procurando vídeos/imagens (requisito explícito do
  projeto: nada de iframe, o app tem que "sair procurando" a mídia).
  **Não usado por padrão** (`kUseLiveScraping = false` em
  `include/config.h`) desde que a lista fixa foi autorizada — o código
  fica pronto pra religar se essa decisão mudar. Limitação conhecida e
  documentada em [[known-issues]]: a página real renderiza o conteúdo
  client-side depois de buscar dados num backend; o HTML puro (o que
  `curl` vê) só contém o placeholder "Aguardando informações". Um
  scraper puramente HTTP não vai encontrar vídeos reais até essa lacuna
  ser resolvida (ver known-issues para as opções cogitadas).
- `include/native_window.h` / `src/native_window.cpp` — extrai o ID de
  janela X11 nativo da janela do Raylib, via `glfwGetX11Window` (GLFW é
  vendorizado dentro do raylib — ver [[architecture]] > Build). Isolado
  num módulo próprio pelo mesmo motivo do `video_config.h`:
  `glfw3native.h` (com `GLFW_EXPOSE_NATIVE_X11`) inclui `<X11/Xlib.h>`
  internamente, que colide com `raylib.h` na mesma translation unit —
  ver "Restrição de headers" abaixo.
- `include/player.h` / `src/player.cpp` — dado um `Campaign` de vídeo:
  1. Cria a janela de vídeo como **filha de verdade** da janela do
     Raylib (`XCreateWindow` usando o ID de `native_window.h` como pai
     — não uma segunda janela top-level `override-redirect`
     posicionada manualmente, que era a v1). Posição/tamanho relativos
     ao pai, não coordenadas absolutas de tela.
  2. Lança `mpv --wid=<id> --gpu-context=x11egl --loop-file=no
     --no-audio --ytdl=yes --ytdl-format=<kYtdlFormatSelector> ...`
     passando a URL da campanha **como está** (arquivo direto para
     `upload`/`direto`; URL original do post/vídeo para
     `youtube`/`instagram`/`facebook`). Nunca embutimos o player
     oficial da plataforma (nunca iframe). `--gpu-context=x11egl` é
     obrigatório: sem forçar, o `mpv` cria sua própria superfície
     Wayland nativa sempre que existe um compositor Wayland alcançável
     e ignora `--wid` completamente, independente de a janela ser
     filha ou top-level — descoberto testando de verdade, ver
     [[known-issues]] item 5.
  3. Para `youtube`/`instagram`/`facebook`, quem resolve a URL de stream
     é o próprio `mpv`, via seu hook interno `ytdl_hook` (que chama
     `yt-dlp`) — **não chamamos `yt-dlp -g` nós mesmos**. Decisão tomada
     depois de testar de verdade e confirmar que `yt-dlp -g` sozinho,
     sem stream progressiva disponível (comum hoje no YouTube), imprime
     vídeo e áudio em URLs separadas; só o `mpv` sabe tocar isso sem
     mux/ffmpeg. Ver [[known-issues]] item 5.
  4. Ao trocar de slide, desmapeia (hide) a janela do mpv e mata o
     processo (SIGTERM, 1s, SIGKILL — nunca espera sem limite); ao
     voltar pra um slide de vídeo, recria. O slide de vídeo termina
     quando o mpv sai sozinho no fim do arquivo (`HasExited()`), não por
     `duracao_segundos`. O mpv nasce com `PR_SET_PDEATHSIG=SIGKILL`: se o
     app cair, o kernel mata o mpv junto (antes ele ficava órfão
     decodificando). `Shutdown()` tem que rodar ANTES do `CloseWindow()`
     do Raylib (a janela de vídeo é filha e morre junto; mexer nela
     depois = `BadWindow` = Xlib aborta o processo).
  5. `Init()`/`Play()` usam `XSync` (não `XFlush`) depois de criar/mapear
     a janela — sem isso existe uma corrida real onde o `mpv` (processo
     separado, conexão X11 própria) tenta anexar numa janela que o
     servidor X ainda não terminou de criar/mapear (mais provável
     quando o primeiro slide já é vídeo). Ver [[known-issues]] item 5.
  6. **A janela de vídeo só é mapeada quando `IsVideoActuallyPlaying()`
     confirma** (via socket IPC JSON do mpv, `--input-ipc-server` +
     evento `playback-restart` — ver [[known-issues]] item -5) que o mpv
     já está de fato tocando, não só
     resolvendo/bufferizando. Enquanto isso, `apps/tvbox_esus_app.cpp`
     desenha `DrawLoadingSlide` (spinner + título/subtítulo). Timeout de
     `kVideoLoadTimeoutSeconds` (config.h) — se não confirmar a tempo,
     desiste e avança o slide. Ver [[known-issues]] item -1.
- `include/ui.h` / `src/ui.cpp` — desenho Raylib do chrome (header/footer,
  cada um com título+subtítulo, cores diferentes entre si — header azul,
  footer vermelho) e dos slides de texto/imagem, usando exatamente a
  paleta/tipografia do [[frontend-contract]]. Tipografia: Liberation
  Sans carregada de `assets/fonts/` (ver [[known-issues]] item 3), não a
  fonte bitmap padrão do Raylib.

- `include/video_cache.h` / `src/video_cache.cpp` — cache local dos
  vídeos, ver seção "Cache de vídeos" abaixo.

## Cache de vídeos (2026-09-29)
Pedido do usuário: baixar os vídeos em vez de fazer streaming a cada
exibição, baixando só quando o dispositivo reinicia, e sem busca
"linear" (nada de baixar tudo antes de começar; um vídeo exibido por
completo → registro → próximo), sem perder as animações.
- **Onde**: `cache/videos/` relativo ao cwd (`kVideoCacheDir`,
  `include/video_config.h`), na flash — nunca tmpfs (RAM é o recurso
  escasso). Arquivo `<fnv64(url)>-<boot_id[0:8]>.<ext>` + `manifest.tsv`
  (o "registro": chave, arquivo, boot_id, bytes, data, url — reescrito
  via tmp+rename a cada download).
- **Quando**: cada vídeo é baixado de novo **uma vez por boot** do
  dispositivo (`/proc/sys/kernel/random/boot_id`). Reinício só do app
  (crash → `exec.sh`) não baixa nada. Arquivo de boot anterior continua
  sendo tocado até o novo terminar — sem rede no boot, o kiosk segue
  com o conteúdo que já tinha. Vídeo sem nenhum arquivo local cai no
  streaming antigo (só a primeira volta depois de instalar).
- **Ordem**: uma única thread, um download por vez, sempre o próximo
  vídeo da rotação a partir do slide atual (`SetCurrentIndex`) que
  ainda não está fresco neste boot. Falha → retry com backoff
  (30s…10min). Não baixa com menos de 512MB livres.
- **Custo**: `nice -n 19` + `ionice -c 3` (nunca disputa com o mpv/
  Chromium); mesmo seletor de formato do streaming (H.264 ≤720p), merge
  vídeo+áudio pelo ffmpeg só com cópia de stream (qualidade intacta).
  Medido no RK3229: 10 vídeos, 63MB, ~5 min, com o kiosk rodando.
- **Nunca trava a UI**: download fora da thread principal; o
  `RunCaptureStdout` aceita um flag de cancelamento (encerramento do
  app mata o `yt-dlp` e o `ffmpeg` neto via grupo de processos).
- Precisa do pacote `ffmpeg` (no `install.sh`).

## Restrição de headers: `player.cpp`/`player.h`/`native_window.cpp` nunca podem incluir `raylib.h`
Descoberto testando de verdade em 2026-09-26 (não é teórico): `<X11/
Xlib.h>` faz `typedef XID Font;` (um inteiro); `raylib.h` faz `typedef
struct Font {...} Font;` (uma struct). Incluir os dois na mesma
translation unit é erro de compilação (`using typedef-name 'Font' after
'struct'`), não um simples aviso. `glfw3native.h` (com
`GLFW_EXPOSE_NATIVE_X11`) também inclui `Xlib.h` internamente — mesmo
problema, mesma regra. Por isso `include/player.h` só usa forward
declarations de `Display`/`Window` (nunca inclui `Xlib.h` no header),
`src/player.cpp` nunca inclui `include/config.h` nem `raylib.h`, e
`src/native_window.cpp` (que precisa de `glfw3native.h`) também nunca
inclui `raylib.h` — qualquer constante que esses módulos precisarem vai
em `include/video_config.h` (sem dependência de raylib), nunca em
`config.h`. Quem CHAMA essas funções (`apps/tvbox_esus_app.cpp`) pode
incluir `raylib.h` normalmente, já que ele não inclui `Xlib.h`
diretamente nem `glfw3native.h` — só os dois lados isolados (que
precisam de X11/GLFW nativo) não podem se misturar com o lado que
precisa de `raylib.h`.

## Janela fixa 25% x 100%
No `main()`, antes do primeiro frame: pega `GetMonitorWidth/Height` do
monitor primário, calcula `w = monitor_w * 0.25`, `h = monitor_h`,
chama `InitWindow(w, h, ...)`, remove decoração (`FLAG_WINDOW_UNDECORATED`)
e posiciona no canto superior **direito** (`x = monitor_w - w, y = 0` —
mudou de esquerdo pra direito a pedido do usuário em 2026-09-27, ver
`kAnchorWindowToRightEdge` em `include/config.h`). Esses números (fração
de largura, fração de altura, canto de ancoragem) ficam centralizados em
`include/config.h` para não virar mágica espalhada pelo código. Como a
janela de vídeo é filha da janela do Raylib (coordenadas relativas ao
pai, não absolutas de tela — ver módulo `player` abaixo), mudar o canto
de ancoragem da janela principal não afeta o posicionamento do vídeo.

## Otimizações de memória (RK3229 / 2GB RAM)
- Sem libcurl linkada: HTTP via `fork`+`exec` do binário `curl` já
  presente no Armbian (evita puxar OpenSSL/libcurl pro processo
  principal; custo de processo é pontual, não por frame).
- Sem parser de HTML (libxml2/tidy): extração por regex simples e
  tolerante, focada só nos padrões de classe que o site usa
  (`institutional-media`, `institutional-embed`). Ver [[known-issues]]
  para o que isso não cobre.
- Decodificação de imagem via `stb_image` (já embutido no Raylib) — sem
  lib extra.
- Decodificação de vídeo sempre via `mpv` com hwdec (`--hwdec=auto`, e
  documentar `--hwdec=rkmpp` como override pra quem builda especificamente
  pro driver Rockchip MPP do RK3229) — nunca decode de vídeo em software
  dentro do processo principal.
- FPS dinâmico no loop principal: 30fps só com animação na tela
  (spinner de carregamento, indicador de volume), 10fps no resto
  (texto/imagem estáticos, ou o mpv desenhando o vídeo na janela dele).
  Medido no RK3229: CPU do app ~16% → ~9%.
- mpv com `--no-audio`: o vídeo é mudo (igual ao site); sem trilha de
  áudio não decodifica áudio nem abre o ALSA.
- Um único binário C++ (raylib estático via FetchContent) — sem runtime
  adicional (nada de Node/Python/Electron; `yt-dlp` é Python, mas quem o
  invoca é o próprio `mpv` — via `ytdl_hook` — só quando o vídeo não é
  um arquivo direto, e não fica residente).

## Controle remoto IR
O receptor IR já vem embutido no hardware e é reconhecido de fábrica
pelo kernel/device-tree do RK3229 (`gpio_ir_recv` + decodificador NEC,
confirmado via `dmesg` num dispositivo real) — o que faltava era só um
keymap (tradução scancode→tecla), inexistente na imagem Armbian
genérica. Habilitação de baixo nível (keymap em `/etc/rc_keymaps/`,
regra `udev` própria pra carregá-lo, `ir-keytable`) fica em
`install.sh`. Detalhe completo (scancodes, formato do `.toml`, bugs
reais corrigidos no caminho) em [[known-issues]] item -6.

Duas teclas são tratadas em lugares diferentes, por decisão deliberada:
- **`KEY_POWER`** → `triggerhappy` (daemon de sistema, fora do
  `tvbox_esus_app`) chama `/usr/sbin/poweroff` direto. Fica fora do
  processo principal de propósito: um desligamento real não precisa de
  feedback visual, e um daemon de sistema independente é mais robusto
  pra essa ação específica (continua funcionando mesmo que o app
  trave/reinicie).
- **`KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`/`KEY_MUTE`** → lidas DENTRO do
  `tvbox_esus_app` (`include/remote_control.h`/`src/remote_control.cpp`),
  que abre `/dev/input/eventN` do receptor diretamente (acha o device
  certo varrendo `/proc/bus/input/devices` pelo nome do driver
  `gpio_ir_recv` — não assume um número de `eventN` fixo, que muda
  conforme a ordem de enumeração a cada boot) e ajusta o volume via
  `amixer` (fork/exec pontual, mesmo princípio de "processo em vez de
  lib" usado pra curl/mpv — sem linkar libasound). O HDMI do RK3229
  não tem mixer em hardware: o controle "Master" vem de um `softvol`
  configurado pelo `install.sh` (ver [[known-issues]] item -8); MUTE é
  emulado (volume 0 ↔ restaura) quando o mixer não tem chave on/off. Motivo de ficar
  DENTRO do processo, ao contrário do POWER: o pedido era mostrar um
  indicador visual de volume no mesmo estilo do resto do kiosk
  (`ui.h::DrawVolumeOsd`), e só o processo Raylib pode desenhar isso.
  **Nunca deixar o `triggerhappy` reagir a essas três teclas também** —
  duplicaria o ajuste (dois processos mexendo no mesmo mixer pro mesmo
  toque de botão).

**Onde o indicador é desenhado, e por quê**: `DrawVolumeOsd` substitui
temporariamente o RODAPÉ (`kVolumeOsdDurationSeconds`, `config.h`) —
nunca a área do banner. A janela de vídeo do `mpv` é filha real da
janela do Raylib e fica posicionada exatamente sobre o banner sempre
que um slide de vídeo está tocando (ver módulo `player` abaixo);
qualquer coisa desenhada pelo Raylib nessa área ficaria fisicamente
coberta pela janela do `mpv` na tela real, mesmo que o código de desenho
rode normalmente (o problema é de composição de janelas X11, não do
código). Header/footer, ao contrário, nunca são cobertos pelo `mpv`
(ele só ocupa a `bannerRect`) — por isso o indicador usa o rodapé.

## Build
- `CMakeLists.txt` é o build system canônico (já resolve a dependência do
  Raylib via `FetchContent`, que não costuma estar empacotado pra
  Armbian). Compila `apps/tvbox_esus_app.cpp` + tudo em `src/*.cpp`.
- **Força `OPENGL_VERSION="ES 2.0"`** na configuração do Raylib (em vez
  do padrão `GRAPHICS_API_OPENGL_33` que `PLATFORM=Desktop` usaria
  sozinho). Testado num RK322x real: sem isso, o app crasha
  (`Segmentation fault`) na criação do contexto gráfico, porque a Mali-400
  do RK3229 não fala OpenGL desktop, só ES. Ver [[known-issues]] item 0.
- `Makefile` é um atalho fino em cima do CMake (`make` = configure na
  primeira vez + build incremental SEMPRE — antes o alvo era o binário
  sem dependências e `make` depois de `git pull` não recompilava nada;
  `make run`, `make clean`) — mantido porque já existia no repo, mas não
  é mais um segundo pipeline de compilação C independente.
- Dependências de runtime no dispositivo alvo (fora do binário): `mpv`,
  `yt-dlp` (+ `python3`), `curl` (só necessário se `kUseLiveScraping`
  voltar a `true`). Testado de verdade nesta sandbox depois de instalar
  `mpv`/`yt-dlp` (ver [[known-issues]] item 5 e [[session-handoff]]) —
  **o vídeo aparece corretamente posicionado dentro da janela do app**,
  confirmado por screenshot com frames mudando ao longo do tempo (não
  só "decodifica sem erro", de fato visível). Ainda não testado no
  Armbian real (essa sandbox só tem VAAPI/AMD, não `rkmpp`).
  **Importante**: `yt-dlp` instalado via `apt` trava numa versão antiga
  do repositório Debian e simplesmente para de funcionar contra o
  YouTube (confirmado: a versão do apt não conseguia extrair nenhum
  formato de vídeo real). Instalar via pip (`pip3 install --user
  --upgrade yt-dlp`) ou binário standalone, e manter atualizado — `apt`
  não é uma fonte confiável pra isso.
- **Arquivos além do binário que precisam acompanhar o deploy**: o app
  usa caminhos relativos (`assets/fonts/...`, `config/campaigns.conf`),
  resolvidos a partir do diretório de trabalho atual — por isso precisa
  ser executado com a raiz do projeto como cwd (é o que `make run` já
  faz). Ao empacotar pro dispositivo, copiar `assets/` e `config/`
  junto do binário, mantendo essa mesma estrutura relativa.
