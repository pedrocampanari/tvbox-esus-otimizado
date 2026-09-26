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
src/                      # implementação dos módulos (scraper, player, ui, procexec)
CMakeLists.txt            # build canônico (resolve Raylib via FetchContent)
Makefile                  # atalho fino sobre o CMake
docs/memory/              # memória do projeto (ver acima)
```

## Build

```sh
make          # configura e compila via CMake em build/
make run      # compila (se preciso) e executa
make clean    # remove artefatos de build
```

Dependências de desenvolvimento (para compilar): `cmake`, `g++` (C++17),
headers de desenvolvimento do X11 (`libx11-dev` no Debian/Armbian). O
Raylib é baixado e compilado automaticamente pelo CMake.

Dependências de runtime no dispositivo (não precisam estar presentes
para compilar, só para o app funcionar de verdade em produção):
`mpv`, `yt-dlp` (+ `python3`), `curl`.

> Fluxo recomendado: compilar num host de desenvolvimento (ou CI) e
> copiar apenas o binário final para o dispositivo Armbian — não é
> necessário instalar toolchain de build no RK3229.

## Estado atual

O app compila, abre a janela no tamanho/posição corretos e desenha o
"chrome" (header/footer/slideshow) com a paleta e tipografia replicadas
do site original. A busca de conteúdo real (vídeos/imagens/textos das
campanhas) tem uma limitação de arquitetura bloqueante ainda não
resolvida — ver o item 1 de
[`docs/memory/known-issues.md`](docs/memory/known-issues.md) antes de
assumir que isso já funciona em produção.
