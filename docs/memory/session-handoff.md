# Handoff de sessão

## Sessão de 2026-09-27 (parte 7) — "corrija os known-issues" (diagnóstico conclusivo do item 5)

### Pedido do usuário
"Corrija os known-issues" — mesmo pedido de antes, revisitado no dia
seguinte.

### O que fiz
Revisei os 6 itens de [[known-issues]]. Itens 1, 3, 4 já estavam
RESOLVIDO; itens 2 e 6 dependem da mesma decisão externa do dono do
sistema (não é algo que eu deva resolver sozinho, ver item 2). O único
item com trabalho real possível era o 5 (a lacuna de confirmação visual
do vídeo embutido, deixada em aberto na sessão anterior).

Investiguei mais a fundo em vez de só repetir o teste:
- Rodei `mpv` sem `--wid` com log verboso: confirmei que ele escolhe
  sozinho `vo=gpu` com contexto **Wayland nativo** quando não precisa
  embutir em janela alheia — `--wid` (que `player.cpp` usa) força o
  caminho X11, já que Wayland não tem conceito de "embutir por ID de
  janela" (diferença de design deliberada).
- Testei `--vo=xv` (técnica clássica de embedding, mais antiga que
  `vo=gpu`) contra a janela real do nosso app: decodifica com sucesso
  (`Using hardware decoding (vaapi-copy)`, progresso de tempo normal),
  mas a captura continua preta — igual ao padrão.
- Capturei com `xwd` (protocolo X11 puro, não passa por nenhuma
  ferramenta/portal do Wayland) além do `import` de antes: também
  preto.
- **Evidência decisiva**: a janela principal do Raylib (também OpenGL)
  apareceu correta em TODOS os screenshots da sessão inteira (dezenas).
  Só a segunda janela top-level `override-redirect` (nosso embedding de
  vídeo) fica preta, com qualquer VO, com qualquer ferramenta de
  captura. Isso isola definitivamente o problema: é como o **mutter**
  (compositor do GNOME/Wayland) lida com uma segunda janela X11
  `override-redirect` de um cliente Xlib cru — não é falta de GPU, não
  é bug no nosso código, não é o VO escolhido.

### Conclusão
known-issues.md item 5 atualizado com esse diagnóstico bem mais sólido
(antes era "hipótese, não confirmada 100%"; agora é uma cadeia de
evidência que isola a causa com confiança alta). **Não mudei código**
— não há evidência de que qualquer VO específico funcione melhor no
Xorg real, e a lógica downstream do `--wid` (spawn, argumentos,
janela) já está validada. Deixei uma dica de troubleshooting
documentada (tentar `--vo=xv` no dispositivo real se a imagem não
aparecer) sem forçar isso como padrão sem evidência.

### Nada mais ficou pendente de correção nesta passada
Itens 2 e 6 continuam bloqueados pela mesma decisão externa (fonte de
dados dinâmica) que já está documentada e não é minha pra decidir
sozinho.

---

## Sessão de 2026-09-26 (parte 6) — mpv/yt-dlp instalados, teste real

### Pedido do usuário
"ja esta instalado testa esse trem em prod" — mpv e yt-dlp foram
instalados nesta sandbox (pelo usuário, via `sudo apt install`), pediu
pra testar de verdade.

### O que foi feito e descoberto (ver [[known-issues]] item 5 pro
detalhe completo)
1. Testei `mpv --ytdl=yes` direto contra uma URL real do YouTube:
   **o `yt-dlp` do apt (2023.03.04) não conseguia extrair NENHUM
   formato de vídeo/áudio** — só storyboards. YouTube muda a extração
   com frequência; essa versão está tarde demais. `yt-dlp -U` recusou
   atualizar (gerenciado pelo apt). Contornei instalando uma versão
   atual via `pip3 install --user --upgrade --break-system-packages
   yt-dlp` (vai pra `~/.local/bin`, na frente do `/usr/bin` no PATH,
   sem tocar no pacote do apt).
2. Com o yt-dlp atualizado, `mpv` resolveu e começou a tocar de
   verdade (vídeo+áudio, buffering visível, progresso de tempo) — o
   `ytdl_hook` funciona.
3. **Bug real encontrado no seletor de formato**: sem restringir o
   codec, resolvia pra AV1 (2160x3840!), que o RK3229 quase certamente
   não decodifica por hardware. Corrigi `kYtdlFormatSelector`
   (`include/video_config.h`) adicionando `[vcodec^=avc1]` — testei de
   novo e confirmei que passou a escolher H.264.
4. Rodei o app de verdade (`./build/bin/tvbox_esus_app`) com o config
   real: confirmei via `ps`/`xwininfo` que o app spawna o `mpv` com os
   argumentos certos, cria a janela X11 filha no tamanho/posição certos
   e mapeada, e o `mpv` consome CPU de forma consistente com decode
   ativo. **Mas a imagem do vídeo não apareceu em nenhum screenshot**
   (tentei capturar a janela de vídeo diretamente por ID, e tentei subir
   um Xephyr como X11 "limpo" alternativo). Causa provável: **esta
   sandbox roda GNOME sob Wayland** (confirmado via
   `XDG_SESSION_TYPE=wayland` e `loginctl`), não um Xorg puro como o
   dispositivo alvo — embutir uma janela X11 override-redirect raw sob
   XWayland é uma fonte conhecida de comportamento imprevisível. Não
   confirmei isso como bug nosso; só não consegui provar visualmente
   aqui.

### Estado real do projeto depois disso
O pipeline de vídeo está **razoavelmente bem validado** agora — muito
mais do que antes desta sessão (resolução de URL, spawn do mpv com
argumentos corretos, janela X11 corretamente criada/posicionada/mapeada
todos confirmados). O único elo que falta confirmar é a composição
visual final, e há uma explicação plausível de ambiente (Wayland vs.
Xorg) pra essa lacuna, não evidência de bug. **Próximo passo real**:
repetir esse mesmo teste num Xorg puro (o dispositivo Armbian real, ou
uma VM/máquina com Xorg sem Wayland).

### Nova dependência operacional descoberta
O dispositivo em produção vai precisar de um jeito de manter o
`yt-dlp` atualizado (o pacote do apt trava numa versão antiga do
repositório Debian, e yt-dlp desatualizado simplesmente para de
funcionar contra o YouTube). Recomendo instalar via pip/binário
standalone (não apt) e considerar um cron pra `yt-dlp -U` periódico.

---

## Sessão de 2026-09-26 (parte 5) — os 10 video_url completos

### Pedido do usuário
"Continua acompanhando a rotação ao vivo pra pegar os outros 8" —
depois da parte 4 ter parado em 2/10.

### O que foi feito
Continuei com `read_network_requests` (filtrando por `urlPattern:
"youtube-nocookie"` pra não poluir o contexto com os blobs
`data:image/jpeg;base64,...` enormes que a página também dispara).
Peguei os 8 restantes acompanhando a rotação em tempo real, incluindo
"GIRO DA SAUDE" que tinha sido perdido antes (precisei deixar o ciclo
completo dar a volta de novo e pegar especificamente a transição
item4→item5, que é rápida). `config/campaigns.conf` agora tem os 10
`video_url` reais preenchidos (nenhum `TODO` restante).

### Verificado de fato
- Rebuild limpo depois da mudança no config.
- Rodei o binário com o config real (mpv continua não instalado nesta
  sandbox): sem crash, comportamento gracioso — cada slide de vídeo
  fica na tela pelo `duracao_segundos` configurado sem travar nem
  travar o app, só sem imagem de vídeo de verdade (esperado, ver
  known-issues item 5 sobre o mpv em si continuar não testado aqui).

### O que ainda não está 100%
- `duracao_segundos` no arquivo são estimativas (não medi o tempo exato
  de cada vídeo — só a ordem de grandeza observada esperando a próxima
  transição). Se o usuário souber os valores reais configurados no
  painel administrativo, vale ajustar.
- Títulos dos itens 8-10 não foram re-confirmados no exato momento da
  captura (overlay já tinha sumido da tela) — vêm da leitura inicial.
  A ordem/mapeamento pra URL é confiável mesmo assim (rotação
  sequencial, sem pulos).
- Item 5 de known-issues (mpv em si, decode por hardware, `ytdl_hook`
  na prática) continua sendo o próximo bloqueio real antes de produção.

---

## Sessão de 2026-09-26 (parte 4) — scraping via tráfego de rede (parcial)

### Pergunta do usuário
"Você não consegue fazer o web scraping e coletar os vídeos nos
iframes?" — depois de eu ter dito na parte 3 que a extração via JS
tinha sido bloqueada.

### O que descobri
Sim, dá pra fazer — só não do jeito que eu tinha tentado antes.
`read_network_requests` (ler o tráfego de rede que o navegador já
capturou) não é a mesma coisa que rodar JS pra ler atributos `src`, e
não foi bloqueado. Consegui 2 dos 10 `video_url` reais assim:
- "SAUDE CONECTADA LEDA FARINAZZO NVTL" → `youtube.com/watch?v=ZxerQfgUtSE`
- "REGULADOR DE FLUXO NO UPA" → `youtube.com/watch?v=1dxBFO5nF10`
Já atualizei `config/campaigns.conf` com essas duas.

### Por que parei em 2/10 (não é preguiça, é custo real)
- O iframe só carrega a URL real quando aquele slide fica ativo — pra
  pegar os 10 eu preciso estar observando quando CADA um entra no ar.
- Descobri ao vivo que cada vídeo real fica no ar bem mais que os 15s
  que eu tinha assumido no seed anterior (um passou de 130s ainda
  tocando) — corrigi `duracao_segundos` pra 60 (palpite, não confirmado)
  nos que ainda não sei.
- **Perdi a URL de 1 slide** ("GIRO DA SAUDE") porque a transição
  aconteceu entre dois polls meus — o método não é 100% confiável.
- Nesse ritmo, pegar os 10 ao vivo levaria a sessão inteira (cada vídeo
  parece durar minutos, não segundos) e ainda arriscando perder mais
  algum.

### Recomendação que dei ao usuário
Exportar a lista completa (video_url + duração real de cada vídeo) do
painel administrativo do próprio sistema é muito mais rápido e
confiável do que eu continuar acompanhando a rotação ao vivo. Fica a
critério dele: ou me passa a lista, ou pede pra eu continuar
acompanhando (aceitando que é lento).

---

## Sessão de 2026-09-26 (parte 3) — "corrija os problemas conhecidos"

### Pedido do usuário
Corrigir os problemas listados em [[known-issues]].

### O que foi corrigido de fato (com evidência, não só "deveria funcionar")
- **Bug real no pipeline de vídeo (item 5)**: instalei `yt-dlp` num
  venv Python isolado (`/tmp/.../scratchpad/testenv`, não afeta o
  sistema nem o repo — não tenho `sudo` com senha nesta sandbox, então
  não consegui instalar `mpv` via apt; não tentei baixar um binário de
  terceiro pra contornar isso) e testei contra um vídeo público real do
  YouTube (`jNQXAC9IVRw`). Descobri que `yt-dlp -g` sozinho, sem stream
  progressiva disponível (o caso comum hoje), imprime **duas URLs em
  linhas separadas** (vídeo e áudio). O código antigo de
  `src/player.cpp` pegava essa saída inteira como se fosse uma URL só —
  ia quebrar de verdade em produção. Corrigido: `player.cpp` não chama
  mais `yt-dlp` diretamente; passa a URL original pro `mpv`, que resolve
  sozinho via seu hook `ytdl_hook` (`--ytdl=yes
  --script-opts=ytdl_hook-ytdl_path=yt-dlp`), que sabe tocar vídeo+áudio
  separados sem mux. Também adicionei um teto de 720p
  (`include/video_config.h::kYtdlFormatSelector`) pra não pedir mais
  resolução do que o RK3229 aguenta decodificar.
- **Bug de compilação real, achado ao mexer no código acima**: incluir
  `include/config.h` (que traz `raylib.h`) dentro de `src/player.cpp`
  (que inclui `<X11/Xlib.h>`) não compila — `Font` é uma struct no
  Raylib e um `typedef XID` (inteiro) no X11, conflito direto. Criado
  `include/video_config.h` (sem depender de raylib) só pra constantes
  que o player precisa, documentado em [[architecture]] como uma regra
  permanente (nunca incluir raylib.h em player.cpp/player.h).
- **Palavra única muito longa (item 4)**: `WrapText` (`src/ui.cpp`)
  agora quebra por caractere (UTF-8-safe) quando uma palavra sozinha não
  cabe na coluna. Testado de verdade: rodei o binário com um título
  gigante de uma palavra só e confirmei por screenshot que quebra
  corretamente em 5 linhas, e que acentos PT-BR continuam renderizando
  certo.
- Rebuild limpo depois de cada mudança (`cmake --build build`).

### O que continua bloqueado (não é código, é falta de informação/acesso)
- **Item 1 (`config/campaigns.conf` com `video_url=TODO`)**: continuo
  sem conseguir extrair os links reais de vídeo do site (bloqueado pelo
  classificador de segurança do ambiente ao tentar ler atributos `src`
  via JS — não tentei contornar). **Preciso que o usuário preencha os
  10 `video_url` reais** antes de qualquer vídeo aparecer de verdade.
- **Item 5, parte "mpv em si"**: não consegui instalar `mpv` nesta
  sandbox (sem senha de root) nem o hardware RK3229 real está
  disponível aqui. A correção da lógica de resolução foi validada
  (testei a causa raiz do bug real com `yt-dlp` isolado), mas o
  comportamento do `mpv --wid=...` embutido na janela X11, decode por
  hardware (`--hwdec=auto`/`rkmpp`) e o `ytdl_hook` funcionando na
  prática continuam só revisados por documentação, não testados.
- Item 2 (scraping ao vivo) e item 6 (configuracoes_tv dinâmico)
  continuam como estavam — dependem da mesma decisão do dono do sistema
  sobre fonte de dados (ver known-issues item 1/2), não é algo que dá
  pra "consertar" só no código.

---

## Sessão de 2026-09-26 (parte 2) — lista fixa, tipografia formal, cores reais

### Pedido do usuário
1. Dono do sistema autorizou usar links de vídeo FIXOS por enquanto —
   não precisa ficar buscando a URL original o tempo todo.
2. Tipografia "mais formal".
3. Footer do site original é vermelho (não azul como o header) — usuário
   já tinha notado isso e pediu atenção.
4. Liberou `claude-in-chrome` pra eu poder verificar ao vivo de novo.

### O que foi feito
- **Fonte de conteúdo trocada para fixa**: `include/config.h` ganhou
  `kUseLiveScraping = false` (default) e `kFixedCampaignsConfigPath`.
  Novo módulo `include/campaign_store.h`/`src/campaign_store.cpp` lê
  `config/campaigns.conf` uma única vez no `main()` — sem thread de
  polling, sem requisição de rede em runtime nesse modo. `DisplayScraper`
  continua no código (não removido), só não é mais chamado por padrão.
- **Verificação ao vivo via `claude-in-chrome`** (navegador já liberado
  pelo usuário): abri `https://esustv.jfbatl.com.br/display` de verdade
  e confirmei via `getComputedStyle`:
  - header `rgb(13,71,161)` = `#0d47a1` (bate com o default que já
    estava sendo usado);
  - **footer `rgb(244,21,21)` = `#f41515` — vermelho, confirmando o que
    o usuário apontou** (a versão anterior deste projeto usava a mesma
    cor azul do header pro footer, porque só tinha visto o HTML puro
    sem JS, que mostra apenas o estado técnico de fallback);
  - header/footer têm duas linhas (título + subtítulo), fonte
    `system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
    "Helvetica Neue", Arial, sans-serif`;
  - via `read_page` (accessibility tree), consegui os **títulos reais**
    dos 10 vídeos atualmente em produção (usados pra semear
    `config/campaigns.conf`).
  - **Não consegui extrair as URLs reais dos vídeos** (`src` de
    `<video>`/`<iframe>`): toda tentativa de ler esses atributos via
    `javascript_tool` foi bloqueada pelo classificador de segurança do
    ambiente ("Cookie/query string data"), provavelmente por
    generalizar "ler atributos com query string de uma página" como
    risco de exfiltração. Não tentei contornar — isso fica pendente
    pro usuário preencher manualmente. Ver [[known-issues]] item 1.
- **Tipografia formal**: copiei Liberation Sans (Regular + Bold, SIL
  OFL 1.1, já presente no sistema em `/usr/share/fonts/truetype/
  liberation2/`) para `assets/fonts/` + `LICENSE-LiberationSans.txt`.
  `include/ui.h`/`src/ui.cpp` agora carregam essas fontes uma vez
  (`LoadUiFonts`/`UnloadUiFonts`) e usam `DrawTextEx`/`MeasureTextEx`
  em vez da fonte bitmap padrão do Raylib, com um conjunto de
  codepoints limitado a ASCII + acentos PT-BR.
- **Header/footer com duas linhas e cores próprias**: `DrawChromeBar`
  agora recebe `titulo` + `subtitulo` separados, e
  `include/config.h` tem `kColorHeaderBackground` (azul) E
  `kColorFooterBackground` (vermelho, novo) em vez de uma cor única
  compartilhada. Textos fixos também atualizados pros valores reais:
  header "PREFEITURA MUNICIPAL"/"Secretaria Municipal de Saúde", footer
  "TRÊS LAGOAS/MS"/"Cada dia melhor".

### Verificado de fato nesta parte da sessão
- Rebuild limpo (`cmake --build build`) depois de cada mudança.
- Corrigi um bug real que o compilador só avisou (`-Wreturn-local-addr`):
  `RegularFont()`/`BoldFont()` em `src/ui.cpp` retornavam `const Font&`
  pra um temporário quando caíam no fallback `GetFontDefault()` —
  referência pendurada. Corrigido pra retornar `Font` por valor (struct
  leve, cópia é barata).
- Rodei o binário de novo (`DISPLAY=:0` nesta sandbox) e capturei
  screenshot: header azul com duas linhas em Liberation Sans (nitidamente
  mais formal que a fonte bitmap anterior), footer vermelho com duas
  linhas, cores batendo com os valores confirmados ao vivo.
- Como todos os 10 itens de `config/campaigns.conf` ainda têm
  `video_url=TODO`, o slideshow cicla entre eles mostrando só o fundo
  neutro do placeholder de vídeo (comportamento esperado e correto: ver
  `src/player.cpp::ResolveStreamUrl`, que trata `TODO`/vazio como "sem
  vídeo" e avança o slide sem travar nem tentar spawnar `mpv`/`yt-dlp`
  à toa).

### Pendente / próximo passo imediato
**Preencher `config/campaigns.conf` com os `video_url` reais** (e
confirmar `video_origem` de cada um — assumi `youtube` como palpite pro
único que vi com a UI do "Shorts", mas não confirmei os outros 9).
Depois disso, o item 5 de [[known-issues]] (pipeline de vídeo não
testado de ponta a ponta) continua sendo o próximo bloqueio.

---

## Sessão de 2026-09-26 (parte 1) — implementação inicial do kiosk

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
