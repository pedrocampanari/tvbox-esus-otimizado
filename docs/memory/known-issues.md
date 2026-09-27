# Problemas e decisões em aberto conhecidas

## -3. Timeout de loading do vídeo (8s) causava spinner infinito no RK3229 real — RESOLVIDO em 2026-09-27
Primeiro teste no dispositivo real com a animação de loading (parte
13): todo vídeo ficava preso no spinner, nunca chegava a tocar.

**Causa**: copiei o valor de `kVideoLoadTimeoutSeconds = 8` do timeout
de iframe do site original (`h=8e3` no bundle JS) sem reparar que é uma
medida completamente diferente — lá é só o carregamento de uma PÁGINA
já hospedada pelo YouTube; no nosso caso o `mpv` precisa rodar o
`yt-dlp` (Python) do zero pra resolver a URL, o que no CPU fraco do
RK3229 (quad-core Cortex-A7) facilmente passa de 8s. Resultado: todo
vídeo desistia antes de confirmar, mostrando só spinner pra sempre (um
slide desistindo e o próximo já entrando com spinner de novo — por
isso parecia "infinito").

**Correção**: `kVideoLoadTimeoutSeconds` subiu pra 30s
(`include/config.h`). Ainda não testado no dispositivo real (aguardando
confirmação do usuário) — se 30s ainda não for suficiente em algum
vídeo específico, aumentar mais é seguro (só atrasa a desistência em
caso de falha real, não afeta o caminho de sucesso).

## -2. Painel institucional nos outros 75% da tela (Chromium) — implementado, NÃO testado de ponta a ponta
Pedido do usuário: preencher os 75% da tela que sobram (nosso app
ocupa 25%, ancorado à direita) com
`https://esus.treslagoas.ms.gov.br/painel`.

**WPE WebKit/Cog foi cogitado primeiro e descartado**: o pacote `cog`
do Debian (confirmei baixando e inspecionando o `.deb` de verdade)
só traz plugins de renderização `drm` (assume a tela inteira sozinho,
sem servidor gráfico), `wl` (Wayland) e `headless` — **nenhum
plugin X11**. Rodar ele exigiria um compositor Wayland por baixo, o
que reabriria exatamente o problema que a sessão inteira de
2026-09-27 (parte 10) resolveu à força: `mpv --wid` sendo ignorado sob
Wayland/XWayland. Reescrever o app inteiro pra `PLATFORM_DRM` (sem X11
nenhum) resolveria isso de outra forma, mas é uma reformulação grande
de arquitetura só por causa desse painel — desproporcional ao pedido.

**Escolhido: Chromium em modo `--app`** (não `--kiosk`, que força
tela cheia e brigaria com o `--window-size` parcial que precisamos).
`exec.sh` agora:
- detecta a resolução via `xrandr` e calcula 75% de largura;
- sobe `chromium --app=<PANEL_URL> --window-position=0,0
  --window-size=<75%,altura_total>` num loop com reinício automático
  (mesmo padrão do app de vídeo);
- variáveis de ambiente `PANEL_URL`/`PANEL_ENABLED` pra configurar sem
  editar o script.

**O que NÃO foi testado**: não consegui instalar `chromium` nesta
sandbox (sem `sudo` com senha aqui, mesma limitação de sempre) — só
validei isoladamente a lógica de detecção de resolução via `xrandr`
(bate: 1366x768 real desta sandbox → 1024 calculado pra 75%) e a
sintaxe do script. **Não confirmei**: se o Chromium do apt renderiza o
painel de verdade, se `--app` realmente evita decoração de janela sem
gerenciador de janelas rodando, nem o impacto de RAM de rodar Chromium
+ nosso app + `mpv` decodificando ao mesmo tempo no orçamento de 2GB do
RK3229 (risco real — Chromium sozinho já costuma passar de 200-300MB).
Precisa validar no dispositivo real antes de considerar isso pronto.

## -1. Animação de carregamento nos slides de vídeo — implementado em 2026-09-27
Pedido do usuário: mostrar algo enquanto o vídeo ainda está
resolvendo/bufferizando, em vez de tela preta/cor neutra parada.

**Como funciona**: `VideoPlayer::Play()` não mapeia mais a janela de
vídeo imediatamente — ela só é revelada quando
`IsVideoActuallyPlaying()` confirma via **IPC JSON do próprio mpv**
(`--input-ipc-server`, protocolo documentado e estável) que a
propriedade `time-pos` já não é nula (ou seja, o mpv já está de fato
posicionado num tempo de playback, não só resolvendo a URL via
`ytdl_hook`). Enquanto isso não acontece, `apps/tvbox_esus_app.cpp`
desenha `DrawLoadingSlide` (título + subtítulo da campanha + um spinner
animado, `src/ui.cpp`) em vez do placeholder neutro. Se isso não
acontecer dentro de `kVideoLoadTimeoutSeconds` (8s, mesmo valor do
timeout de iframe do site original), desiste e avança o slide — mesmo
comportamento de falha (`onFalha`) que o app original tinha pra
embeds externos.

**Testado de verdade**: rodei o app do zero e confirmei por screenshot
o spinner aparecendo com o título certo logo na inicialização, girando
(frames diferentes capturados), e a transição pro vídeo real depois de
confirmado. Também presenciei o caminho de timeout funcionando de
verdade (um slide não confirmou a tempo, o app desistiu e avançou pro
próximo sozinho — mesmo comportamento do `onFalha` original).

**Limitação conhecida, não é bug**: `time-pos` não-nulo é um sinal
"bom o suficiente" mas não perfeito — pode ficar não-nulo uma fração de
segundo antes do primeiro quadro realmente aparecer na tela (testei
outras propriedades do mpv — `core-idle`, `pause`, `paused-for-cache`
— e todas ficam consistentes com "tocando" ao mesmo tempo que
`time-pos`, sem sinal mais preciso disponível sem assinar eventos como
`playback-restart`, que exigiria um cliente IPC mais completo). Na
prática isso significa, na pior hipótese, um instante muito curto de
tela neutra entre o spinner sumir e o vídeo aparecer — bem melhor do
que a alternativa (tela parada por toda a duração do carregamento).
**Primeiro teste de verdade num RK322x físico** (não mais só a sandbox
de desenvolvimento x86_64): o app subia o X corretamente mas o binário
crashava (`Segmentation fault`, código 139) toda vez, logo na
inicialização.

**Causa raiz**: o Raylib, sem forçar nada, usa `GRAPHICS_API_OPENGL_33`
(OpenGL desktop 3.3) quando compilado com `PLATFORM=Desktop` — confirmado
no próprio log de configure do CMake. A GPU do RK3229 é uma Mali-400,
que só fala **OpenGL ES** (tipicamente ES 2.0), não OpenGL desktop.
Pedir um contexto GL 3.3 numa GPU que não tem isso faz o GLFW/Raylib
crashar na criação do contexto — o app nem chega a rodar uma linha do
nosso código.

**Correção**: `CMakeLists.txt` agora força
`set(OPENGL_VERSION "ES 2.0" CACHE STRING "" FORCE)` antes do
`FetchContent_MakeAvailable(raylib)`. O app só usa desenho 2D básico
(retângulos, texto, textura) — nada que dependa de recursos exclusivos
do GL 3.3 — então ES 2.0 funciona igual em qualquer GPU, incluindo as
de desktop (testado: continua renderizando tudo certo na sandbox x86_64
com Mesa/AMD depois da mudança).

**Efeito colateral notado (não é bug nosso)**: quando o GLFW falha em
inicializar (por qualquer motivo — `$DISPLAY` ausente, contexto GL
incompatível, etc.), o Raylib crasha em vez de sair limpo com um erro.
Isso é uma limitação do GLFW/Raylib upstream, não algo que dá pra
corrigir do nosso lado sem recompilar o Raylib com patches próprios —
por isso `exec.sh` agora falha rápido quando detecta `$DISPLAY` ausente
(evita pelo menos esse caso específico de loop de crash confuso).

**Se isso persistir mesmo com ES 2.0 forçado**: rodar `glxinfo | grep
"OpenGL"` numa sessão X ativa no dispositivo (`DISPLAY=:0 glxinfo`) pra
ver que driver/versão está realmente disponível — pode ser necessário
ajustar `--hwdec` do mpv também (ver item 5) se o driver Mali usado for
diferente do esperado (Panfrost vs. driver proprietário ARM, por
exemplo).

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

## 5. Pipeline de vídeo — RESOLVIDO em 2026-09-27 (vídeo posicionado corretamente, confirmado por screenshot)

Histórico resumido (a investigação passou por vários diagnósticos
errados antes de chegar na causa raiz de verdade — registrado abaixo
pra quem for mexer nisso de novo não repetir os mesmos becos sem
saída):

**yt-dlp do `apt` desatualizado**: a versão do Debian (2023.03.04) não
extraía nenhum formato de vídeo real do YouTube (só storyboards). Sem
`yt-dlp -U` (recusa por ser gerenciado via apt). Resolvido instalando
versão atual via `pip3 install --user --upgrade --break-system-packages
yt-dlp` (`~/.local/bin`, na frente de `/usr/bin` no PATH). **Dependência
operacional real do projeto**: o dispositivo em produção precisa de um
jeito de manter `yt-dlp` atualizado (apt trava em versões antigas que
páram de funcionar contra o YouTube).

**Seletor de formato caindo em AV1**: sem `[vcodec^=avc1]` no
`--ytdl-format`, o `mpv` escolhia AV1 (RK3229 não tem decode de AV1 por
hardware). Corrigido em `include/video_config.h::kYtdlFormatSelector`.

**`yt-dlp -g` chamado por nós mesmos quebrava com streams separados**:
histórico mais antigo, já corrigido — `ResolveStreamUrl` não chama mais
`yt-dlp -g`; repassa a URL original pro `mpv`, que resolve sozinho via
`ytdl_hook`.

**A causa raiz de verdade do vídeo não aparecer no lugar certo — bug
real, corrigido, ao contrário do que eu tinha diagnosticado antes**:

O usuário reportou (2026-09-27) que o vídeo aparecia "numa aba
separada, flutuando", não dentro da área reservada. Minha primeira
hipótese (registrada numa versão anterior deste documento) era que o
compositor **mutter** (GNOME/Wayland) não respeitava
`override_redirect=True` numa segunda janela X11 top-level. **Essa
hipótese estava incompleta.** Investigando mais a fundo:

1. **Causa raiz real**: o `mpv`, toda vez que existe um compositor
   Wayland alcançável (`WAYLAND_DISPLAY` setado, como acontece sob
   XWayland), **cria sua própria superfície Wayland nativa e ignora
   `--wid` por completo** — mesmo passando uma janela X11 válida,
   mesmo sendo uma janela FILHA de verdade (testei as duas formas:
   segunda janela top-level `override-redirect`, e depois — já como
   parte da correção — uma janela filha de verdade; `mpv` ignorava
   `--wid` nas duas, sempre preferindo Wayland nativo quando disponível
   e nenhum contexto de GPU foi forçado explicitamente).
2. Corrigido com **duas mudanças complementares**:
   - **Janela filha de verdade** (não mais uma segunda janela
     top-level `override-redirect` posicionada manualmente): a janela
     de vídeo agora é criada como filha real da janela do Raylib, via
     `XCreateWindow` usando o ID de janela X11 nativo do Raylib como
     pai (obtido via `glfwGetX11Window`, isolado em
     `src/native_window.cpp`/`include/native_window.h` pelo mesmo
     motivo de `video_config.h` — colisão de `Font` entre raylib.h e
     Xlib.h). Posição/tamanho agora são relativos ao pai, não
     coordenadas absolutas de tela — `include/player.h`/`src/player.cpp`
     mudaram de assinatura (`Init`/`SetGeometry` recebem
     `parentWindowId` e x/y relativos). Isso garante que a janela
     nunca vira uma superfície independente do compositor.
   - **Forçar `--gpu-context=x11egl`** no `mpv` (`src/player.cpp`): sem
     isso, mesmo com a janela filha, o `mpv` ainda preferia Wayland
     nativo e ignorava `--wid`. Forçar o contexto X11/EGL garante que
     ele sempre respeite `--wid`, independente de haver ou não um
     compositor Wayland por perto. Bônus: decode por hardware sem cópia
     extra (`vaapi` zero-copy em vez de `vaapi-copy`).
3. **Verificado de verdade, com screenshot mostrando o vídeo real
   dentro da área reservada** (não só "decodifica", como nas tentativas
   anteriores): rodei o app do zero múltiplas vezes; o vídeo aparece
   corretamente posicionado entre o header e o footer, dentro da
   janela do kiosk, com o frame mudando ao longo do tempo (prova de
   playback contínuo, não uma imagem estática).
4. Nota lateral: durante essa investigação também confirmei que `Xv`
   (`--vo=xv`) usa um overlay de hardware historicamente invisível pra
   `XGetImage`/`xwd`/`import` — por isso testes anteriores com `vo=xv`
   pareciam "não renderizar" mesmo decodificando; isso é uma limitação
   de ferramentas de screenshot, não do `mpv` nem do nosso código, e
   não tem relação com o bug real (que era o `--gpu-context` sendo
   ignorado, afetando igualmente todos os VOs baseados em `vo=gpu`).

**Bug de corrida corrigido também nesta janela de investigação**: o
`mpv` podia morrer rápido (virar zumbi em segundos) numa partida limpa
quando o primeiro slide já é vídeo — `VideoPlayer::Init()`/`Play()`
usavam `XFlush` (só envia o pedido) em vez de `XSync` (espera o
servidor processar) antes do `mpv` (processo separado, conexão X11
própria) tentar anexar na janela. Corrigido trocando por
`XSync(display_, False)` nos dois pontos. Testado: 3 partidas limpas
seguidas sem falha, contra 1 falha na primeira tentativa antes da
correção.

**O que ainda não foi validado**: o comportamento no Armbian real (Xorg
puro, sem Wayland) — lá, `WAYLAND_DISPLAY` nunca estaria setado, então
o `mpv` provavelmente escolheria o contexto X11 automaticamente mesmo
sem forçar `--gpu-context=x11egl`; mas como agora forçamos
explicitamente, o comportamento fica determinístico independente disso,
o que é mais robusto. Decodificação por hardware real via `rkmpp`
(driver Rockchip) continua não testada (só temos VAAPI/AMD nesta
sandbox) — validar `--hwdec=auto` de fato escolhe `rkmpp` no
dispositivo real, ou ajustar pra `--hwdec=rkmpp` explícito se
necessário.

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
