# Handoff de sessão

## Sessão de 2026-09-26 — implementação inicial do kiosk

### O que foi feito
- Pesquisa estática (sem headless browser) de
  `https://esustv.jfbatl.com.br/display`: HTML servido + CSS + bundles JS
  públicos baixados via `curl` e lidos diretamente, para extrair paleta
  de cores, tipografia, estrutura de layout e a lógica real de resolução
  de vídeo (YouTube/Instagram/Facebook → URL de embed; upload/direto →
  arquivo direto). Tudo registrado em [[frontend-contract]].
- Arquitetura definida e documentada em [[architecture]]: Raylib desenha
  o "chrome" (header/footer/slides de texto/imagem); vídeo é sempre
  tocado por um processo `mpv` externo, embutido via uma janela X11
  filha (`--wid`), nunca via iframe/webview.
- Código novo:
  - `include/config.h`, `include/campaign.h`, `include/procexec.h`
  - `include/scraper.h` + `src/scraper.cpp` (busca HTML + extração por
    regex de `<video>`, `<iframe>` youtube/instagram/facebook, `<img>`)
  - `include/player.h` + `src/player.cpp` (janela X11 filha + spawn de
    `mpv`, resolução de URL externa via `yt-dlp -g`)
  - `include/ui.h` + `src/ui.cpp` (desenho do header/footer e slides de
    texto/imagem replicando a paleta/tipografia real do site)
  - `apps/tvbox_esus_app.cpp` (entrypoint único: janela fixa 25% larg. x
    100% alt., thread de scraping em background a cada 30s, máquina de
    estados do slideshow)
- Build consolidado: `CMakeLists.txt` agora é o único pipeline de
  compilação real (compila tudo acima, linka X11 + raylib via
  FetchContent + Threads); `Makefile` virou um atalho fino sobre o
  CMake. Removidos `apps/tvbox_esus_app.c` (hello-world C) e
  `include/library.h` (header vazio) — substituídos pela implementação
  real acima. Removido também `src/main.cpp` (demo Raylib de exemplo).

### O que foi verificado de fato (não é só "deveria funcionar")
- `cmake -S . -B build && cmake --build build` compila limpo neste
  ambiente (x86_64, Debian, com headers X11 disponíveis).
- O binário resultante **rodou de verdade** (`DISPLAY=:0` disponível
  nesta sandbox) e foi capturado em screenshot: janela abre na largura
  certa (25% do monitor), header azul (`#0d47a1`) e footer, fundo escuro
  (`#0b1c33`), placeholder "Aguardando informações" com quebra de linha
  correta dentro da coluna estreita, cores/tipografia batendo com os
  tokens do CSS original. Ver captura discutida na conversa (não commitada
  ao repo).
- A thread de scraping chegou a fazer uma requisição real (`curl`) para
  a URL de produção durante o teste rápido acima — não retornou dados
  úteis (ver [[known-issues]] item 1), mas confirma que o fluxo
  fetch→parse→lock→UI não trava nem crasha com resposta vazia/placeholder.

### O que NÃO foi verificado (ficou pra próxima sessão / pro dono do projeto)
- Reprodução de vídeo de ponta a ponta: `mpv` e `yt-dlp` não estão
  instalados nesta sandbox, e o hardware é x86_64, não o RK3229 real.
  Ver [[known-issues]] item 4 para o checklist de validação no
  dispositivo real.
- Scraping de conteúdo real: a página só expõe o placeholder técnico
  via HTTP puro (confirmado, não é achismo). **Isso é o maior bloqueio
  antes de qualquer uso em produção** — ver [[known-issues]] item 1, que
  também explica por que não decidi isso sozinho (uma tentativa de
  chamar a API de backend real durante a pesquisa foi bloqueada pelo
  classificador de segurança do meu ambiente, e por prudência não
  contornei isso nem embutir o endpoint/chave descobertos no código).
- `configuracoes_tv` (cores/textos configuráveis de header/footer) — não
  lido ainda, usa defaults fixos.

### Próximos passos sugeridos (em ordem)
1. **Decisão do dono do sistema**: como o dispositivo vai obter os dados
   reais de campanha (ver as 3 opções em [[known-issues]] item 1). Sem
   isso, o app funciona mas só mostra o placeholder pra sempre.
2. Testar o player de vídeo (`mpv`/`yt-dlp`) num Armbian real ou numa VM
   ARM, com um vídeo de cada `video_origem` (upload/direto/youtube/
   instagram/facebook).
3. Medir uso de RAM real no RK3229 com o binário + `mpv` rodando, pra
   validar as premissas de otimização de [[architecture]].
4. Se a fidelidade da fonte importar, avaliar embutir uma TTF leve (ver
   [[known-issues]] item 2).
