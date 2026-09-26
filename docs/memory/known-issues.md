# Problemas e decisões em aberto conhecidas

## 1. O scraper HTTP puro não vê conteúdo real (bloqueante para produção)
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

## 2. Fidelidade tipográfica
A UI usa a fonte bitmap padrão do Raylib, não a pilha `system-ui`
sans-serif do site original (ver [[frontend-contract]]). Decisão
consciente pra v1: carregar uma fonte TTF do sistema custa memória e
complexidade extra num dispositivo de 2GB RAM. Se a fidelidade visual da
fonte importar, o próximo passo é embutir uma única fonte leve (ex.: uma
variante condensada, carregada uma vez via `LoadFontEx` com um conjunto
de caracteres limitado a acentos PT-BR) em vez de carregar uma família
completa.

## 3. Título/subtítulo com palavra única muito longa
`WrapText` (em `src/ui.cpp`) só quebra em espaços. Uma palavra isolada
mais larga que a coluna de 25% ainda vai vazar da tela. Não corrigido
porque não apareceu nos textos reais observados até agora; se acontecer,
a correção é quebra por caractere como último recurso dentro de
`WrapText`.

## 4. Pipeline de vídeo não testado de ponta a ponta
Este ambiente de desenvolvimento é x86_64 sem `mpv`, sem `yt-dlp` e sem
o hardware RK3229 real. O código de `include/player.h`/`src/player.cpp`
(criação de janela X11 filha, spawn do `mpv --wid=...`, resolução via
`yt-dlp -g` para YouTube/Instagram/Facebook) compila e a lógica foi
revisada manualmente contra a documentação do `mpv`/`yt-dlp`, mas **não
foi executada com um vídeo real** nem no dispositivo alvo. Antes de
considerar isso pronto pra produção, validar no Armbian real:
  - `mpv --wid=<id> --hwdec=auto <url>` decodifica usando a VPU do
    RK3229 (senão, testar `--hwdec=rkmpp` explicitamente, se o mpv
    empacotado tiver suporte);
  - `yt-dlp -g -f "best[ext=mp4]/best" <url_youtube>` retorna uma URL que
    o `mpv` local consegue abrir direto (sem precisar mesclar
    áudio/vídeo separados, o que exigiria `ffmpeg`).

## 5. `configuracoes_tv` (cores/textos de header/footer) não é lido
O header/footer no app original são configuráveis via banco (cores,
textos, visibilidade). Nosso app usa valores fixos de
`include/config.h` (mesmos valores default observados no site). Isso é
consequência direta do item 1 (sem fonte de dados, não tem o que ler) —
resolve junto quando o scraping de dados reais for destravado.
