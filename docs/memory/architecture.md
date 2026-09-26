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
     ├─ resolve URL de stream (direto, ou via processo yt-dlp pontual)
     └─ spawna um processo `mpv --wid=<janela X11 filha>` posicionado
        exatamente sobre a área do banner; mpv decodifica e desenha
        diretamente nessa janela X11 (fora do pipeline do Raylib/OpenGL)
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
- `include/scraper.h` / `src/scraper.cpp` — busca e interpreta o HTML da
  página `/display` procurando vídeos/imagens (requisito explícito do
  projeto: nada de iframe, o app tem que "sair procurando" a mídia).
  **Limitação conhecida e documentada em** [[known-issues]]: a página real
  renderiza o conteúdo client-side depois de buscar dados num backend: o
  HTML puro (o que `curl` vê) só contém o placeholder "Aguardando
  informações". Um scraper puramente HTTP não vai encontrar vídeos reais
  até essa lacuna ser resolvida (ver known-issues para as opções
  cogitadas e por que nenhuma foi escolhida ainda sem confirmação do
  usuário).
- `include/player.h` / `src/player.cpp` — dado um `Campaign` de vídeo:
  1. Se `video_origem` for `upload`/`direto`, usa a URL como está.
  2. Se for `youtube`/`instagram`/`facebook`, roda `yt-dlp -g <url
     original>` (processo pontual, sem daemon) pra obter a URL de stream
     direta, replicando as regras de extração de ID/URL documentadas em
     [[frontend-contract]] — mas resolvendo pra reprodução direta via
     `mpv`, nunca embutindo o player oficial da plataforma (nunca iframe).
  3. Cria/reaproveita uma janela X11 filha (Xlib puro, sem depender de
     internals do GLFW/Raylib) posicionada sobre a área do banner e
     lança `mpv --wid=<id> --loop-file=inf --mute=yes ...`.
  4. Ao trocar de slide, desmapeia (hide) a janela do mpv e mata o
     processo; ao voltar pra um slide de vídeo, recria.
- `include/ui.h` / `src/ui.cpp` — desenho Raylib do chrome (header/footer)
  e dos slides de texto/imagem, usando exatamente a paleta/tipografia do
  [[frontend-contract]].

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
  adicional (nada de Node/Python/Electron; `yt-dlp` é Python mas só roda
  pontualmente, por vídeo externo, não fica residente).

## Build
- `CMakeLists.txt` é o build system canônico (já resolve a dependência do
  Raylib via `FetchContent`, que não costuma estar empacotado pra
  Armbian). Compila `apps/tvbox_esus_app.cpp` + tudo em `src/*.cpp`.
- `Makefile` é um atalho fino em cima do CMake (`make` = configure+build,
  `make run`, `make clean`) — mantido porque já existia no repo, mas não
  é mais um segundo pipeline de compilação C independente.
- Dependências de runtime no dispositivo alvo (fora do binário): `mpv`,
  `yt-dlp` (+ `python3`), `curl`. Nenhuma delas está instalada nesta
  sandbox de desenvolvimento (x86_64, sem Armbian) — o binário foi
  compilado e checado localmente, mas o fluxo de vídeo real (spawn do
  mpv, resolução via yt-dlp) não foi executado de ponta a ponta neste
  ambiente. Ver [[known-issues]] e [[session-handoff]].
