# Contrato de Frontend (réplica visual/comportamental)

Fonte analisada: `https://esustv.jfbatl.com.br/display` (HTML servido + bundles JS/CSS
públicos baixados e lidos estaticamente em 2026-09-26). Nenhuma credencial de
backend foi usada; tudo abaixo vem de assets que o próprio navegador do
usuário final recebe.

## Stack observada
- React (TanStack Start/Router), build Vite, hospedado na Vercel.
- Backend de dados: Supabase (Postgres + Realtime). O cliente web assina
  `postgres_changes` nas tabelas `campanhas`, `campanhas_unidades`,
  `configuracoes_tv`, `unidades`, e também faz polling de segurança a cada
  30s (`refetchInterval: 30000`) independente do realtime.
- Rotas relevantes: `/display` (visão geral) e `/display/$unidadeSlug`,
  `/tv/$unidadeSlug` (por unidade de saúde).

## Modelo de conteúdo (campanha)
Cada item exibido no painel é um objeto com este formato (nomes em
português, como usados no app):

```
id, origem ("padrao" | "campanha" | "tecnico"),
tipo ("texto" | "imagem" | "video"),
titulo, subtitulo, texto,
imagem_url, video_url,
video_origem ("upload" | "direto" | "youtube" | "instagram" | "facebook"),
duracao_segundos (default 10, mínimo efetivo 10),
ordem, estilo
```

Lista de exibição = itens "padrão" ativos + campanhas, ordenados por
`ordem` e então por `id`. Se a lista ficar vazia, mostra um item técnico
fixo: título "SECRETARIA MUNICIPAL DE SAÚDE", subtítulo "Aguardando
informações" — **este é o estado que a página `/display` genérica mostra
sempre**, porque ela não tem uma unidade associada.

## Rotação/slideshow
- Um item fica ativo por vez, avança por `setTimeout(duracao_segundos*1000)`.
- Troca de slide é cross-fade via CSS (`opacity 0→1` em 400ms), todos os
  slides ficam empilhados com `position:absolute;inset:0` dentro do
  container `.institutional-banner`.
- Cada slide ocupa 100% da área do banner (`.institutional-message`).

## Vídeo — regra central (por isso este projeto existe)
- `video_origem = "upload"` ou `"direto"`: o vídeo já é um arquivo
  reproduzível direto (checado no app original via regex
  `/\.(mp4|webm)(\?.*)?$/i`). Renderizado como `<video autoPlay muted loop
  playsInline>` — **sem iframe**.
- `video_origem = "youtube" | "instagram" | "facebook"`: o app original
  renderiza um `<iframe>` (institutional-embed) porque a plataforma exige.
  Regras de conversão observadas no bundle (`configuracoes-*.js`):
  - YouTube: extrai o ID via regex (`v=`, `youtu.be/`, `/shorts/`,
    `/embed/`, `/live/`) e monta
    `https://www.youtube-nocookie.com/embed/<id>?autoplay=1&mute=1&controls=0&loop=1&playlist=<id>&playsinline=1&modestbranding=1&rel=0&iv_load_policy=3&disablekb=1`.
  - Instagram: casa `instagram.com/(p|reel|reels|tv)/<code>` e monta
    `https://www.instagram.com/<p|reel>/<code>/embed/captioned/`.
  - Facebook: exige `facebook.com` ou `fb.watch` na URL original e monta
    `https://www.facebook.com/plugins/video.php?href=<url original>&show_text=false&autoplay=true&mute=1`.
  - Timeout de 8s: se o iframe não disparar `onLoad` em 8000ms, o app
    original considera falha e pula pro próximo item.
- **Nosso app não pode usar iframe** (requisito do projeto). Para os
  três casos de plataforma externa, a URL original (antes da conversão em
  embed) é o que deve ser passado ao `yt-dlp` para resolver uma URL de
  stream direta; para upload/direto, a URL já é reproduzível e vai direto
  pro player. Ver [[architecture]] para o desenho do módulo de player.

## Paleta e tipografia (tokens reais do CSS + confirmação ao vivo)
| Uso | Cor |
|---|---|
| Fundo geral da página (`display-page`) | `#0b1c33` |
| Fundo do **header** (`tv-chrome-header`) | `#0d47a1` (`rgb(13,71,161)`) |
| Fundo do **footer** (`tv-chrome-footer`) | **`#f41515`** (`rgb(244,21,21)`) — **vermelho, diferente do header** |
| Texto de header/footer | `#ffffff` |
| Fundo de mídia/letterbox (vídeo, imagem, texto) | `#06121f` |
| Texto de campanha (título/corpo) | `#ffffff` |
| Destaque/subtítulo (`--tc-accent`) | `#bfdbfe` |

> A primeira versão deste documento assumia (a partir do HTML puro, sem
> JS) que header e footer usavam a mesma cor azul — era só o estado
> técnico de fallback genérico da rota `/display` sem unidade. Inspeção
> ao vivo via `claude-in-chrome` em 2026-09-26 (painel real em produção)
> confirmou que o footer é **vermelho**, e que header/footer têm duas
> linhas cada: um título em negrito uppercase + um subtítulo menor
> (ex.: header "PREFEITURA MUNICIPAL" / "Secretaria Municipal de Saúde";
> footer "TRÊS LAGOAS/MS" / "Cada dia melhor"), via as classes
> `.tv-chrome-text` (título) e `.tv-chrome-subtext` (subtítulo, opacity
> .85). `getComputedStyle` confirmou a fonte real: `system-ui,
> -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica
> Neue", Arial, sans-serif` — nosso app usa Liberation Sans (metric
> compatível com Arial) como substituto formal, ver [[known-issues]]
> item 3.

- Fonte: pilha `system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica
  Neue", Arial, sans-serif` (chamada de "sistema" na configuração).
- Título da campanha: bold/900, uppercase, tamanho responsivo via
  `clamp()` (~2.2rem a 4rem conforme viewport).
- Subtítulo: cor de destaque, ~1.5rem a 2.6rem.
- Corpo de texto: peso 500, `white-space: pre-line` (preserva quebras de
  linha do autor).
- Densidade adaptativa: classes `data-densidade="curto|medio|longo|extenso"`
  reduzem o tamanho de fonte conforme o texto é mais longo — replicar essa
  lógica de escala é opcional para v1, mas documentado aqui para v2.
- Header/footer: texto uppercase, `letter-spacing: .12em`, borda sutil
  separando do banner (`1px solid #ffffff1f`).
- Mídia (vídeo/imagem) sempre `object-fit: contain` — nunca cortar,
  sempre "letterbox" com o fundo `#06121f`.

## Diferença deliberada de layout (requisito do projeto)
A versão web ocupa `100vw x 100vh`. Nosso app deve abrir fixo em
**25% da largura x 100% da altura** do monitor, mantendo a mesma
estrutura interna (header / banner rotativo / footer) dentro dessa faixa
vertical estreita. Ver [[architecture]] para como isso afeta o
dimensionamento de fontes e da janela de vídeo embutida.
