# Problemas e decisões em aberto conhecidas

## 1. Modo atual: lista fixa de vídeos — RESOLVIDO (10/10) em 2026-09-26
O dono do sistema autorizou usar uma lista FIXA de links de vídeo por
enquanto, em vez de ficar buscando `esustv.jfbatl.com.br/display` o
tempo todo. Mudanças feitas:
- `include/config.h`: `kUseLiveScraping = false` (default). Quando
  `false`, o app carrega `config/campaigns.conf` **uma única vez** na
  inicialização (`LoadFixedCampaigns`, em `src/campaign_store.cpp`) e
  não faz nenhuma requisição de rede em runtime. `DisplayScraper` (item
  abaixo) continua no código, pronto pra religar (`kUseLiveScraping =
  true`) se essa decisão mudar.
- **Os 10 `video_url` reais foram todos capturados** (não via JS, que
  continua bloqueado pelo classificador de segurança do ambiente, mas
  lendo o **tráfego de rede** que o próprio navegador captura —
  `read_network_requests` — enquanto o painel real rodava em produção:
  cada requisição real que o iframe do YouTube faz
  (`youtube-nocookie.com/embed/<id>`) foi observada e reconstruída pra
  `youtube.com/watch?v=<id>`). `config/campaigns.conf` está com os 10
  preenchidos. Isso levou ~15-20 minutos de acompanhamento ao vivo
  porque:
    - o iframe só carrega a URL real quando aquele slide especificamente
      fica ativo (os outros ficam em `about:blank` até a vez deles);
    - a duração real de cada vídeo varia muito (de ~15s a mais de 130s),
      nada uniforme;
    - 1 slide ("GIRO DA SAUDE") transiciona rápido demais pra pegar na
      primeira passada — precisei deixar o ciclo dar a volta completa
      de novo e recapturar especificamente esse.
  **Confiança dos títulos**: 7 dos 10 (itens 1-7) foram reconfirmados
  lendo o texto exibido na tela no exato momento da captura da URL; os
  outros 3 (itens 8-10) vêm da leitura inicial (accessibility tree) —
  a ORDEM/mapeamento pra video_url é confiável (rotação sequencial sem
  pulos), só o texto exato do título desses 3 não foi re-confirmado
  simultaneamente. Ver cabeçalho de `config/campaigns.conf` para
  detalhes.
- `duracao_segundos` no arquivo são estimativas (não medi o tempo exato
  de cada vídeo, só a ordem de grandeza enquanto esperava a próxima
  transição) — ajustar se souber os valores reais configurados no
  painel administrativo.
- Formato completo do arquivo de config está documentado no cabeçalho de
  `config/campaigns.conf` (comentários) e em [[architecture]].

## 2. O scraper HTTP puro não vê conteúdo real (relevante só se kUseLiveScraping voltar a true)
**Confirmado em 2026-09-26** com `curl` direto em
`https://esustv.jfbatl.com.br/display`: o HTML retornado contém só o
estado técnico de fallback ("Aguardando informações"). O conteúdo real
(campanhas, vídeos, imagens) é buscado por um backend e injetado no DOM
depois da hidratação do React no navegador — ver
[[frontend-contract]]. Isso significa que `DisplayScraper` (em
`src/scraper.cpp`), do jeito que está, só vai encontrar vídeos/imagens
reais se:
  a) a marcação servida mudar para incluir os dados (ex.: uma rota com
     SSR real por unidade), ou
  b) adicionarmos um passo de renderização (headless) antes de raspar o
     HTML.

**Por que não resolvi isso sozinho**: durante a pesquisa eu encontrei a
URL e a chave pública ("publishable key") do projeto Supabase que o
frontend usa, embutidas nos bundles JS públicos. Uma classificação de
segurança automática do meu ambiente bloqueou minha tentativa de testar
uma chamada direta a essa API de terceiro (mesmo sendo uma chave pública,
somente leitura, que todo navegador que visita o site já recebe) — e
por prudência eu decidi não contornar isso nem embutir esse endpoint/chave
no código-fonte deste projeto. **Isso fica como uma decisão para o dono
do sistema tomar explicitamente**, não para eu decidir sozinho com base
numa instrução genérica de "está autorizado" num arquivo do repositório.

Opções reais para destravar isso (nenhuma implementada ainda):
- O dono do sistema expõe deliberadamente um endpoint JSON somente
  leitura (ou confirma que o publishable key do Supabase pode ser usado
  diretamente pelo dispositivo) — aí o scraper vira uma chamada HTTP
  simples, muito mais leve que renderização headless.
- Adicionar um passo de renderização headless (ex.: `webkit2gtk` mínimo,
  rodado só durante o poll de 30s e depois liberado) — mais pesado pro
  RK3229/2GB, mas não depende de mais nenhuma decisão externa.
- O dono do sistema disponibiliza uma rota SSR específica pra TVs (fora
  do escopo deste repo).

**Enquanto isso não é decidido**: o app roda e mostra o placeholder
"Aguardando informações" (comportamento correto e testado — ver
[[session-handoff]]), e o pipeline de reprodução de vídeo (scraper →
player) está implementado e pronto para qualquer uma das opções acima,
mas não pôde ser validado com vídeos reais.

## 3. Fidelidade tipográfica — RESOLVIDO em 2026-09-26
Pedido do dono do sistema: tipografia "mais formal". Trocado o texto
desenhado com a fonte bitmap padrão do Raylib por **Liberation Sans**
(Regular + Bold, SIL OFL 1.1, metric-compatible com Arial — a mesma
família usada oficialmente como substituto formal do Arial em
documentos institucionais). Arquivos em `assets/fonts/` (+
`LICENSE-LiberationSans.txt`), carregados uma vez em `LoadUiFonts()`
(`src/ui.cpp`) com um conjunto de codepoints limitado a ASCII + acentos
PT-BR (evita gerar um atlas de glyphs maior que o necessário). Se o
arquivo de fonte não for encontrado no dispositivo, cai de volta pra
fonte padrão do Raylib (aviso no log, não é fatal) — mas isso não deve
acontecer em produção: `assets/fonts/` faz parte do que precisa ser
copiado junto do binário (ver [[architecture]]).

## 4. Título/subtítulo com palavra única muito longa — RESOLVIDO em 2026-09-26
`WrapText` (em `src/ui.cpp`) agora quebra por caractere (UTF-8-safe, não
corta um acento ao meio) quando uma palavra sozinha já é mais larga que
a coluna disponível — equivalente ao `overflow-wrap: break-word` do CSS
original. Testado de verdade: rodei o binário com um título de uma
palavra só propositalmente gigante
(`PALAVRAUNICAMUITOLONGAPRATESTARQUEBRADELINHAPORCARACTERE`) e confirmei
por screenshot que quebra em 5 linhas dentro da coluna de 25%, sem
vazar. No mesmo teste confirmei que acentos PT-BR (ação, atenção,
saúde, público) renderizam corretamente com o conjunto de codepoints
carregado em `LoadUiFonts()`.

## 5. Pipeline de vídeo — testado de verdade em 2026-09-26; 1 bug real corrigido; 1 limitação de ambiente encontrada

**Atualização**: `mpv` e `yt-dlp` foram instalados nesta sandbox e o
pipeline foi testado de ponta a ponta contra os vídeos reais de
`config/campaigns.conf`. Resultado: majoritariamente funciona, com uma
correção real aplicada e uma limitação genuína de ambiente (não do
nosso código) que ficou sem confirmação visual.

**Descoberta 1 — yt-dlp do `apt` estava desatualizado demais pra
funcionar**: a versão do Debian (2023.03.04) não conseguia extrair
NENHUM formato de vídeo/áudio de um vídeo real do YouTube (só
storyboards/thumbnails) — o YouTube muda a extração com frequência e
yt-dlp precisa ser atualizado seguido. `yt-dlp -U` recusou atualizar
("installed via apt, use apt to update"). Contornado nesta sandbox
instalando uma versão atual via `pip3 install --user --upgrade
--break-system-packages yt-dlp` (vai pra `~/.local/bin`, que já vem
antes de `/usr/bin` no PATH — não mexe no pacote do apt). **Isso é uma
dependência operacional real do projeto**: o dispositivo em produção
vai precisar de um jeito de manter o `yt-dlp` atualizado (ex.: cron
rodando `yt-dlp -U` se instalado via pip/binário standalone, já que via
apt normalmente trava numa versão antiga do repositório Debian).

**Descoberta 2 — bug real no seletor de formato, corrigido**: com
`yt-dlp` atualizado, `mpv --ytdl-format="bestvideo[height<=720]+..."`
(o seletor antigo) resolvia pra **AV1**, não H.264. Confirmado rodando
`mpv` direto contra uma URL real. O RK3229 é um chip de 2016; sua VPU
Rockchip quase certamente não tem decode de AV1 por hardware (isso só
apareceu em SoCs Rockchip bem mais recentes) — decodificar AV1 em
software nesse CPU fraco (quad-core Cortex-A7) provavelmente não
aguentaria um kiosk contínuo. Corrigido em
`include/video_config.h::kYtdlFormatSelector`: adicionado
`[vcodec^=avc1]` pra forçar H.264, que eu confirmei existir em toda
resolução testada pra esse vídeo. Testado de novo com o seletor
corrigido: `mpv` passou a escolher `h264 360x640` corretamente.

**Descoberta 3 — o pipeline completo do app funciona, exceto a
composição visual final, que não pôde ser confirmada nesta sandbox**:
rodei o binário de verdade (`./build/bin/tvbox_esus_app`) com o
`config/campaigns.conf` real. Confirmado via `ps`/`xwininfo`:
  - o app spawna `mpv` com todos os argumentos corretos (`--wid=<id>`,
    `--ytdl-format` já corrigido, URL certa da campanha ativa);
  - a janela X11 filha existe, está no tamanho/posição certos
    (`341x645+0+61`, batendo com a área do banner), está mapeada
    (`Map State: IsViewable`) e o `mpv` está de fato consumindo CPU de
    forma consistente com decode ativo.
  Porém **a imagem do vídeo não apareceu nos screenshots**.

  **Atualização 2026-09-27 — investiguei mais a fundo e agora tenho uma
  causa raiz bem mais sólida** (não é só hipótese):
  - Sem `--wid`, `mpv` escolhe sozinho `vo=gpu` com um **contexto
    Wayland nativo** (log: `[vo/gpu/opengl] Initializing GPU context
    'wayland'`) — cria sua própria superfície Wayland, sem passar por
    X11 nenhum. `--wid` é um conceito puramente X11 (Wayland não tem
    "embutir por ID de janela alheia" — isso é uma diferença de design
    deliberada, por segurança). Então, ao passar `--wid` (obrigatório
    pra embutir na nossa janela), o `mpv` é forçado pro caminho X11.
  - Testei **duas variantes do caminho X11**: o padrão (`vo=gpu` com
    contexto X11/EGL) e forçando `--vo=xv` (X-Video, a técnica clássica
    e historicamente mais robusta pra esse tipo de embedding). Nos
    dois casos, `mpv` decodifica de verdade (confirmado nos logs: "Using
    hardware decoding (vaapi-copy)", progresso de tempo avançando,
    cache/buffer normal) — mas a janela continua preta em QUALQUER
    método de captura, incluindo `xwd` (protocolo X11 puro, não passa
    por nenhum portal/ferramenta de screenshot do Wayland).
  - **A evidência decisiva**: a janela PRINCIPAL do Raylib (que também
    usa OpenGL/EGL) apareceu correta em literalmente todo screenshot
    tirado durante esta sessão inteira (dezenas de vezes). Só a
    **segunda janela top-level `override-redirect`** (a técnica de
    embedding em `player.cpp`) fica preta. Isso isola o problema: não é
    "GPU não composita neste container" (a janela do Raylib prova que
    composita bem) — é especificamente como o **mutter** (compositor
    Wayland do GNOME, via XWayland) lida com uma segunda janela X11
    top-level `override-redirect` de um cliente Xlib cru, sem toolkit.
  - **Isso não é um bug no nosso código** — a lógica de spawn, os
    argumentos do mpv, e a criação/posicionamento/mapeamento da janela
    X11 estão todos corretos (confirmado via `ps`/`xwininfo`). É uma
    limitação genuína e específica desta sandbox (GNOME+Wayland+
    XWayland), que o dispositivo alvo **não tem** (Armbian roda Xorg
    puro, sem Wayland, sem XWayland, provavelmente sem compositor
    nenhum — nesse cenário, duas janelas top-level com stacking
    controlado por `XMapRaised` é a técnica padrão de décadas de
    kiosks Linux, e funciona de forma direta e previsível).
  - **Próximo passo real**: validar a composição visual num Xorg puro
    de verdade (o dispositivo Armbian real, ou uma VM/máquina com Xorg
    sem Wayland) antes de considerar este item fechado — mas a
    confiança de que vai funcionar lá é alta, dado que toda a lógica
    downstream do `--wid` já foi validada.
  - **Confirmação do usuário (2026-09-27), que fecha a dúvida**: o
    usuário rodou o binário direto (fora das minhas capturas) e viu o
    vídeo aparecer de verdade — só que **numa janela/aba separada,
    flutuando por conta própria, não encaixado dentro da janela do
    app**. Isso bate 100% com a hipótese acima e explica por que meus
    `xwd`/`import` mostravam preto: o **mutter** (compositor do
    GNOME/Wayland, hospedando o cliente via XWayland) não está
    respeitando o `override_redirect=True` da nossa janela — em vez de
    tratá-la como uma superposição sem gerência (o que X11 puro faria),
    ele a exibe como uma janela própria, independente, na posição que
    ELE decide, ignorando as coordenadas que pedimos via
    `XMoveResizeWindow`. Isso é uma particularidade conhecida de
    mutter/XWayland com clientes X11 crus (sem toolkit) que criam
    janelas `override-redirect` — não existe em Xorg sem compositor
    (o caso do dispositivo real), onde `override-redirect` sempre
    significa exatamente "não gerencie, deixe onde eu pedi".

  - **Descoberta 4 (2026-09-27) — bug real de corrida, corrigido**: ao
    tentar reproduzir a demonstração pro usuário, o `mpv` morreu rápido
    (virou zumbi em poucos segundos) numa partida limpa do app, quando
    o primeiro slide já é vídeo. Causa: `VideoPlayer::Init()` criava a
    janela X11 com `XCreateWindow` + `XFlush`, e `Play()` (chamado logo
    em seguida, às vezes no mesmo frame) mapeava com `XMapRaised` +
    `XFlush` antes de dar fork/exec no `mpv`. `XFlush` só garante que o
    pedido foi **enviado** ao servidor X, não que ele já foi
    **processado**. O `mpv`, rodando como processo separado com sua
    própria conexão X11, podia tentar anexar (`--wid`) numa janela que
    o servidor ainda não tinha terminado de criar/mapear — corrida
    genuína, mais provável exatamente no primeiro slide (menos tempo
    decorrido entre `Init()` e `Play()`). Corrigido trocando `XFlush`
    por `XSync(display_, False)` nos dois pontos (`Init()` e `Play()`,
    em `src/player.cpp`), forçando um round-trip que garante que o
    servidor já aplicou o pedido antes de devolver o controle. Testado
    de verdade: 3 partidas limpas seguidas depois da correção, `mpv`
    iniciou e permaneceu decodificando nas 3 (antes, a mesma sequência
    tinha falhado na primeira tentativa).
  - **Dica de troubleshooting pro Xorg real, se a imagem não aparecer**:
    testei duas variantes do VO nesta sessão e as duas decodificam com
    sucesso (só não consegui confirmar visualmente, pelo motivo acima):
    o padrão do `mpv` sem `--vo` explícito (deixa o `mpv` escolher —
    é o que `player.cpp` faz hoje) e `--vo=xv` forçado (técnica mais
    antiga/clássica pra embedding em janela alheia, historicamente mais
    previsível entre drivers diferentes). **Não troquei o padrão no
    código** porque não tenho evidência de qual é melhor no hardware
    real (o teste aqui não diferenciou os dois — a sandbox quebrada
    faz os dois falharem do mesmo jeito visualmente) — só documento
    como opção de diagnóstico: se o vídeo não aparecer no Armbian real,
    tentar `--vo=xv` explícito é um teste rápido e de baixo risco.

**Histórico (sessão anterior, mesmo dia)**: antes de ter `mpv`/`yt-dlp`
instalados, eu já tinha encontrado e corrigido um bug relacionado
testando só o `yt-dlp` isolado num venv: `yt-dlp -g` sozinho, quando o
vídeo não tem stream progressiva (comum), imprime DUAS URLs em linhas
separadas (vídeo e áudio) — o código antigo passava isso como se fosse
uma URL só pro `mpv`. Corrigido faz tempo: `ResolveStreamUrl` não chama
mais `yt-dlp -g`, só repassa a URL original pro `mpv`, que resolve
sozinho via `ytdl_hook` (`--ytdl=yes
--script-opts=ytdl_hook-ytdl_path=yt-dlp`, já que o hook por padrão
procura `youtube-dl`, que normalmente não existe no Armbian). Essa
correção foi validada de verdade agora (descobertas 1-3 acima).

## 6. `configuracoes_tv` (cores/textos de header/footer) não é lido dinamicamente
O header/footer no app original são configuráveis via banco (cores,
textos, visibilidade). Nosso app usa valores fixos de
`include/config.h` — mas, diferente da primeira versão deste documento,
**esses valores agora são os reais confirmados ao vivo em 2026-09-26**
via `claude-in-chrome`, não mais os defaults técnicos genéricos:
header azul `rgb(13,71,161)` / "PREFEITURA MUNICIPAL" / "Secretaria
Municipal de Saúde"; **footer vermelho `rgb(244,21,21)`** / "TRÊS
LAGOAS/MS" / "Cada dia melhor" (o footer NÃO é azul como o header —
correção pedida pelo usuário, que já tinha notado isso). Se a prefeitura
mudar essas cores/textos no painel administrativo, será preciso
atualizar `include/config.h` manualmente até esses dados serem lidos de
alguma fonte dinâmica (mesma dependência do item 1/2).
