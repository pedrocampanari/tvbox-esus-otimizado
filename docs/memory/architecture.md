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
 └─ quando o slide ativo é vídeo:
     └─ spawna um processo `mpv --wid=<janela X11 filha> --ytdl=yes ...`
        posicionado exatamente sobre a área do banner, passando a URL da
        campanha como está; o próprio mpv resolve internamente (via seu
        hook `ytdl_hook` + `yt-dlp`) quando não é um arquivo direto, e
        decodifica/desenha nessa janela X11 (fora do pipeline Raylib/OpenGL)
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
- `include/player.h` / `src/player.cpp` — dado um `Campaign` de vídeo:
  1. Cria/reaproveita uma janela X11 filha (Xlib puro, sem depender de
     internals do GLFW/Raylib — por isso `player.cpp` não pode incluir
     `raylib.h`/`include/config.h`; ver `include/video_config.h`)
     posicionada sobre a área do banner.
  2. Lança `mpv --wid=<id> --loop-file=inf --mute=yes --ytdl=yes
     --ytdl-format=<kYtdlFormatSelector> ...` passando a URL da campanha
     **como está** (arquivo direto para `upload`/`direto`; URL original
     do post/vídeo para `youtube`/`instagram`/`facebook`). Nunca
     embutimos o player oficial da plataforma (nunca iframe).
  3. Para `youtube`/`instagram`/`facebook`, quem resolve a URL de stream
     é o próprio `mpv`, via seu hook interno `ytdl_hook` (que chama
     `yt-dlp`) — **não chamamos `yt-dlp -g` nós mesmos**. Decisão tomada
     depois de testar de verdade e confirmar que `yt-dlp -g` sozinho,
     sem stream progressiva disponível (comum hoje no YouTube), imprime
     vídeo e áudio em URLs separadas; só o `mpv` sabe tocar isso sem
     mux/ffmpeg. Ver [[known-issues]] item 5.
  4. Ao trocar de slide, desmapeia (hide) a janela do mpv e mata o
     processo; ao voltar pra um slide de vídeo, recria.
- `include/ui.h` / `src/ui.cpp` — desenho Raylib do chrome (header/footer,
  cada um com título+subtítulo, cores diferentes entre si — header azul,
  footer vermelho) e dos slides de texto/imagem, usando exatamente a
  paleta/tipografia do [[frontend-contract]]. Tipografia: Liberation
  Sans carregada de `assets/fonts/` (ver [[known-issues]] item 3), não a
  fonte bitmap padrão do Raylib.

## Restrição de headers: `player.cpp`/`player.h` nunca podem incluir `raylib.h`
Descoberto testando de verdade em 2026-09-26 (não é teórico): `<X11/
Xlib.h>` faz `typedef XID Font;` (um inteiro); `raylib.h` faz `typedef
struct Font {...} Font;` (uma struct). Incluir os dois na mesma
translation unit é erro de compilação (`using typedef-name 'Font' after
'struct'`), não um simples aviso. Por isso `include/player.h` só usa
forward declarations de `Display`/`Window` (nunca inclui `Xlib.h` no
header) e `src/player.cpp` nunca inclui `include/config.h` nem
`raylib.h` — qualquer constante que `player.cpp` precisar vai em
`include/video_config.h` (sem dependência de raylib), nunca em
`config.h`. Se precisar adicionar uma constante nova pro player, o
lugar certo é `video_config.h`.

## Janela fixa 25% x 100%
No `main()`, antes do primeiro frame: pega `GetMonitorWidth/Height` do
monitor primário, calcula `w = monitor_w * 0.25`, `h = monitor_h`,
chama `InitWindow(w, h, ...)`, remove decoração (`FLAG_WINDOW_UNDECORATED`)
e posiciona em `(0, 0)`. Esses três números (fração de largura, fração de
altura, canto de ancoragem) ficam centralizados em `include/config.h`
para não virar mágica espalhada pelo código.

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
- Um único binário C++ (raylib estático via FetchContent) — sem runtime
  adicional (nada de Node/Python/Electron; `yt-dlp` é Python, mas quem o
  invoca é o próprio `mpv` — via `ytdl_hook` — só quando o vídeo não é
  um arquivo direto, e não fica residente).

## Build
- `CMakeLists.txt` é o build system canônico (já resolve a dependência do
  Raylib via `FetchContent`, que não costuma estar empacotado pra
  Armbian). Compila `apps/tvbox_esus_app.cpp` + tudo em `src/*.cpp`.
- `Makefile` é um atalho fino em cima do CMake (`make` = configure+build,
  `make run`, `make clean`) — mantido porque já existia no repo, mas não
  é mais um segundo pipeline de compilação C independente.
- Dependências de runtime no dispositivo alvo (fora do binário): `mpv`,
  `yt-dlp` (+ `python3`), `curl` (só necessário se `kUseLiveScraping`
  voltar a `true`). Testado de verdade nesta sandbox depois de instalar
  `mpv`/`yt-dlp` (ver [[known-issues]] item 5 e [[session-handoff]]) —
  o pipeline de resolução/spawn funciona; só a composição visual final
  não pôde ser confirmada aqui (ambiente Wayland, não Xorg puro).
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
