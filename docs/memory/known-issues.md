# Problemas e decisões em aberto conhecidas

## -12. Arquivos zerados depois de tirar da tomada (`commit=120` do Armbian) — PARCIALMENTE MITIGADO em 2026-09-30; decisão em aberto
Visto no dispositivo: depois de um religamento às 10:50, arquivos
gravados poucos minutos antes voltaram com **0 byte** — `install.sh`,
docs, `src/remote_control.cpp` e 9 objetos do `.git` (o repositório
ficou com `fatal: bad object HEAD`). O próximo `make` falhou no link e o
kiosk caiu (o autologin do item -10 o trouxe de volta assim que o
binário foi recompilado). Causa: `/` montado com `commit=120` (fstab do
Armbian, pra poupar a eMMC) + alocação atrasada do ext4 → até ~2 min de
gravações só existem na RAM.
Reparo feito: objetos vazios apagados, `git fetch` + `git reset --hard
origin/main`, `git fsck` limpo.
Mitigação no código: o cache de vídeos faz `fsync` do arquivo baixado,
do `manifest.tsv` (antes do `rename`) e do diretório, e ignora arquivos
de 0 byte ao carregar.
**Em aberto (decisão do usuário)**: reduzir o `commit` (ex.: 15-30s)
protege também logs, `asound.state` e o perfil do Chromium (pareamento
do painel!) ao custo de mais escrita na flash. Até lá: depois de
atualizar o dispositivo, rodar `sync` antes de desligar da tomada.

## -11. Tela preta no fim dos vídeos — RESOLVIDO em 2026-09-30 (medido no dispositivo)
Relato do usuário: "tela preta ao final da execução". Investigação no
RK3229 (sonda no socket IPC do mpv + captura da tela a 5 fps):
1. **O próprio arquivo termina preto**: `blackdetect` do ffmpeg mostrou
   8 dos 10 vídeos com 2 a 60 s de preto no final (fade-out/encerramento
   do YouTube). O pior, `8j0AOhflraE`: conteúdo até ~184 s, preto de
   ~186 s a 245 s (confirmado extraindo quadros). Correção: o cache mede
   o preto final depois de baixar (só o trecho final, `nice`) e grava o
   "fim útil" no `manifest.tsv`; o player seta a opção `end` do mpv
   antes do `loadfile`. Sem recodificar. Arquivos já em cache são
   analisados sem baixar de novo. Verificado: o `end-file` chega
   exatamente no fim útil (ex.: 115,87 s de 120,70 s).
2. **~11 s de "carregando" entre vídeos, mesmo com arquivo local**: cada
   vídeo abria um mpv novo, e só criar o contexto EGL (Mesa/lima) leva
   ~3 s no RK3229 (medido com `--log-file`), mais o resto com a CPU
   disputada. Correção: UM mpv persistente (`--idle --force-window`),
   arquivos trocados via IPC (`loadfile`). Fim do vídeo → primeiro quadro
   do próximo: **0,6 s**.
3. **Janela preta entre o fim do arquivo e a troca de slide**: a janela
   de vídeo (fundo preto) ficava mapeada até o app perceber a saída do
   processo. Agora é escondida no instante do evento `end-file`.
Resultado filmado: fade-out do vídeo (~0,4 s) → título da próxima
campanha com spinner (~0,4 s) → próximo vídeo.

## -10. Kiosk não subia sozinho ao ligar (exigia `startx` manual) — RESOLVIDO em 2026-09-30 (boot real testado no dispositivo)
Requisito do projeto: "ao ligar na tomada, aparece o binário". Visto no
dispositivo depois de um reboot: nenhum autologin, nenhum `startx`
automático — ficava no prompt de login do console (o X que eu via antes
era aberto à mão pelo usuário no tty2). `install.sh` agora configura:
1. `/etc/systemd/system/getty@tty1.service.d/tvbox-autologin.conf`
   (autologin do usuário que rodou o `install.sh` — `root` no
   dispositivo);
2. `~/.xinitrc` → `exec <repo>/exec.sh`;
3. bloco marcado em `~/.profile`: login no **tty1** sem `$DISPLAY` →
   `exec startx -- -nocursor`. Se o X cair, o getty loga de novo e o X
   volta. SSH e outros ttys não são afetados.
**Testado com reboot real**: Xorg (`-nocursor vt1`) + app + mpv de pé
~20s depois do boot, painel do Chromium carregado, vídeo tocando do
cache.

Bug achado no caminho: o `install.sh` abortava com código 141 no bloco
de verificação (`set -o pipefail` + `mpv --version | head -1` → SIGPIPE
no mpv). Todos os `| head -1` da verificação viraram `| sed -n 1p`.

## -9. `yt-dlp` sem runtime JavaScript — RESOLVIDO em 2026-09-30 (quickjs, testado no dispositivo)
Aviso visto no RK3229: `No supported JavaScript runtime could be found
... some formats may be missing`. `deno` (padrão do yt-dlp) não tem
build armv7; `nodejs` do apt puxaria ~12 pacotes (libicu + libnode,
~80MB). Adotado **`quickjs`** (pacote de 1,2MB, suportado oficialmente
pelo yt-dlp) + `yt-dlp-ejs` via pip (o extra `yt-dlp[default]` falha no
armv7: `brotli` sem wheel). `/etc/yt-dlp.conf` com `--js-runtimes
quickjs` vale pro cache de vídeos e pro `ytdl_hook` do mpv. Medido no
dispositivo: extração em ~9s com e sem quickjs, mesmo formato H.264,
aviso sumiu, `-v` mostra `JS runtimes: quickjs-2025-04-26`.

Junto: `yt-dlp -U` (que o `install.sh` recomendava) não funciona com
instalação via pip. Substituído por `tvbox-ytdlp-update.timer`
(domingo 03:30, `pip3 install --user --upgrade yt-dlp yt-dlp-ejs`,
`Nice=19`). Serviço executado uma vez no dispositivo com sucesso.

## -8. Controle remoto VOL+/VOL-/MUTE nunca ajustavam nada: nenhum mixer ALSA — RESOLVIDO em 2026-09-29 (testado no dispositivo; restauração do volume confirmada em boot real em 2026-09-30)
Diagnóstico via SSH no RK3229: `amixer -c 0|1|2 scontrols` vazio nas
três placas (analog, SPDIF, HDMI) — o HDMI deste SoC não tem volume em
hardware. `AdjustVolume()` falhava sempre (e o aviso nem aparecia no
`kiosk.log` porque o stdout era bufferizado). Além disso a placa ALSA
padrão era a **analógica** (card 0), não o HDMI da TV.

Correção (`install.sh`, arquivo próprio
`/etc/alsa/conf.d/99-tvbox-hdmi-softvol.conf`, sem tocar no
`/etc/asound.conf` do pacote `armbian-bsp-cli`): `pcm.!default` = plug →
`softvol` (cria o controle "Master") → `dmix` (vários processos no
HDMI ao mesmo tempo) → `hw:HDMI,0`; `ctl.!default` = HDMI.
Particularidades:
- O controle "Master" do softvol só existe depois que alguém abre o PCM
  padrão uma vez → `install.sh` e `exec.sh` tocam 0,1s de silêncio.
- softvol não tem chave on/off → MUTE é emulado no app (volume 0; o
  próximo MUTE devolve o volume anterior). `src/remote_control.cpp`
  detecta sozinho se o mixer tem chave de verdade e usa `toggle` nesse
  caso.
- O kiosk é desligado tirando da tomada (sem `alsactl store` do
  shutdown) → o app roda `alsactl store` a cada ajuste e o `exec.sh`
  faz `alsactl restore` ao iniciar.
- mpv agora roda com `--no-audio` (o vídeo sempre foi mudo, igual ao
  site) — o volume do controle afeta o Chromium (painel de chamadas).
Testado no dispositivo injetando `EV_KEY` em `/dev/input/event0`
(mesmo caminho do receptor IR): VOL- 100→90%, MUTE →0%, MUTE →90%,
VOL+ →100%, indicador no rodapé confirmado por captura de frames.
Em 2026-09-30, boot real: volume deixado em 70% antes de reiniciar voltou
em 70% (`exec.sh` cria o "Master" e faz `alsactl restore`).

**Passo do volume errado (corrigido em 2026-09-30)**: com o controle
físico, cada toque andava ~10% (100→90→80→69) e VOL+ foi de 50% a 100%
em dois toques. O log do kernel mostrou UM `key_down` por toque — não
era repetição. Causa: o passo relativo do `amixer` (`5%+`/`5%-`) no
softvol (escala em dB) não anda 5% na escala que o próprio `amixer`
exibe. `AdjustVolume()` agora calcula o alvo absoluto (atual ± 5) e faz
`sset Master N%`, que faz ida e volta exata.

**Confirmado com o controle físico (2026-09-30)**: VOL+ ×3 → 55/60/65%,
VOL- ×3 → 60/55/50%, MUTE → "VOLUME: MUDO", MUTE → 50%. Só falta
ouvir o som na TV (o vídeo é mudo; o som vem do painel do Chromium).

## -7. WiFi onboard (SSV6051) não funciona sob Armbian — driver `ssv6x5x` TESTADO em 2026-09-30 e DESCARTADO; caminho: dongle USB

### Teste do driver `ssv6x5x` (2026-09-30, no dispositivo) — não funciona, não repetir
Em 2026-09-29 levantei a hipótese de o chip ser SV6256P (`SSV6006C0`,
driver `ssv6x5x`) e não SSV6051, porque os dois usam o mesmo ID SDIO
`3030:3030`. **Hipótese errada.** A pedido do usuário, instalei os
headers (`linux-headers-current-rockchip` + `libssl-dev`/`libelf-dev`/
`libzstd-dev`) e compilei o port
<https://github.com/cdhigh/armbian_sv6256p> (commit `5b7824a`) no próprio
RK3229: compilou limpo, `vermagic` idêntico ao kernel `6.18.54`. Carregado
tanto depois do `ssv6051` quanto sozinho desde um boot limpo (com o
`ssv6051` na blacklist), o resultado foi o mesmo:
```
TU_SSV6XXX_SDIO mmc1:0001:1: vendor = 0x3030 device = 0x3030
TU_SSV6XXX_SDIO mmc1:0001:1: CHIP ID: \xc0
```
Os 4 registradores de ID do chip voltam praticamente zerados (só um byte
`0xC0`), o driver não reconhece o chip e não cria a `wlan`. A thread do
fórum abaixo mostra que o chip destas placas se identifica como
`RSV6200A0-201311`, ou seja, **SSV6051 mesmo**. O problema continua
sendo o da thread: escritas via SDIO não persistem no chip, sem fix
conhecido.

**Revertido**: o módulo `ssv6x5x` e o diretório de build foram removidos
do dispositivo (`depmod -a`). Ficou só a blacklist do `ssv6051` (o mesmo
estado que o `install.sh` deixa). Headers do kernel e libs `-dev`
também removidos (`apt purge` + `apt-get clean`, a pedido do usuário):
disco em 2,6GB usados, abaixo dos 2,9GB de antes do teste.

**Caminho restante**: dongle USB WiFi (Realtek RTL8188EUS/RTL8192EU,
driver nativo no kernel), ver instruções do `install.sh`.

### Diagnóstico original (2026-09-29, parte 20)
Usuário achou `armbian-config` com opção de trocar de kernel, pensando
em usar isso pra resolver o WiFi que não funcionava. Antes de mexer no
kernel (risco real: destabilizaria tudo que já validamos nesta sessão —
vídeo por hardware, GPU ES2.0, controle remoto), pedi diagnóstico.

**Diagnóstico, feito com o dispositivo recém-ligado (antes do kiosk
iniciar — descarta disputa de memória/CMA com o `mpv`/Chromium)**:
- `wlan0` existe (`ip link`), driver `ssv6051`/`mac80211`/`cfg80211`
  carregados, `rfkill list` mostra nenhum bloqueio (soft/hard). Ou seja,
  o hardware É reconhecido e o driver carrega — o problema é mais fundo.
- `dmesg` mostra: `Using SSV6051Q setting` (perfil que o driver
  configura sozinho no probe) mas depois, ao tentar subir a interface
  de verdade, `chip id: SSV6006C0` — um identificador diferente do
  perfil assumido. Entre esses dois pontos, calibração de RF **falha
  100 de 100 tentativas** (`calibation fail after N iterations`, N de 1
  a 100). Na hora de inicializar de verdade: `Failed to allocate packet
  buffer of 904 bytes`, `opps allocate pbuf error`, `WARNING` do kernel
  em `ssv6xxx_init_mac`, terminando em `Failed to initialize mac,
  ret=1`.

**Confirmado como problema conhecido da comunidade, não específico
desta unidade**: uma thread do fórum oficial do Armbian
([rk322x-box: SSV6051 wifi never comes up — register writes are ACKed
but never persist (6.6.16 and 6.18.46)](https://forum.armbian.com/topic/61742-rk322x-box-ssv6051-wifi-never-comes-up-%E2%80%94-register-writes-are-acked-but-never-persist-6616-and-61846/))
documenta EXATAMENTE os mesmos sintomas, com uma investigação bem mais
profunda que a nossa:
- Escritas de registro via SDIO são confirmadas no nível de transporte
  (`sdio_memcpy_toio()` retorna sucesso, contadores de erro SDIO ficam
  zerados) **mas o valor nunca chega de verdade no chip** — leituras
  funcionam perfeitamente, só escritas não persistem.
- Testado em **duas versões de kernel diferentes** (6.6.16 e 6.18.46),
  comportamento **idêntico** nas duas — ou seja, trocar de kernel pelo
  `armbian-config` **não deveria resolver isso** (já foi testado, na
  prática, em duas versões diferentes por outra pessoa).
- Testado em **dois dispositivos** iguais, mesmo problema nos dois —
  não é peça defeituosa isolada.
- Confirmado que o WiFi funciona normalmente no **Android original**
  (firmware de fábrica) desses mesmos boxes — o driver vendor/Android
  faz algo diferente na sequência de energização/inicialização do
  barramento SDIO que o Armbian não replica.
- **Nenhuma solução foi encontrada** até a data da thread — fica em
  aberto pedindo ajuda de especialista em SSV6051/análise de barramento.

**Decisão tomada com o usuário**: não vale a pena perseguir esse bug
(nem um especialista dedicado numa thread pública conseguiu resolver) —
e trocar de kernel é uma aposta com evidência CONTRA funcionar, com
risco real de quebrar IR/GPU/vídeo já validados. Caminho escolhido:
**dongle USB WiFi externo** (chipset Realtek RTL8188EUS/RTL8192EU
recomendado — suporte nativo bem estabelecido no kernel Linux,
plug-and-play), contornando o SSV6051 quebrado por completo.

**Implementado em `install.sh`**:
- `wpasupplicant` + `isc-dhcp-client` adicionados aos pacotes de
  runtime (necessários pra conectar QUALQUER adaptador WiFi via
  `/etc/network/interfaces`, independente do dongle específico).
- `/etc/modprobe.d/blacklist-ssv6051-wifi.conf` bloqueando o driver
  quebrado — evita ~4s de tentativas de calibração fadadas ao fracasso
  a cada boot e o ruído de `WARNING`s no `dmesg` (que podia confundir
  diagnóstico futuro deste projeto). Reversível (apagar o arquivo) se
  algum dia um driver corrigido aparecer upstream.
- Detecção de adaptador USB WiFi conectado (`lsusb`, procurando
  Realtek/Ralink/Atheros/MediaTek) no bloco de verificação final.
- Instruções impressas no fim do `install.sh` pra configurar a conexão
  de forma **persistente** (sobrevive reboot/religar da tomada — via
  `/etc/network/interfaces`, o mecanismo padrão do Debian/`ifupdown`,
  que é o que provavelmente já gerencia a interface `end0` cabeada
  neste dispositivo, já que não há evidência de NetworkManager
  instalado). **Não automatizado** — SSID/senha são segredos do usuário
  e não devem ir pro `install.sh` (que é versionado no git); também não
  dá pra saber o nome exato da interface (`wlan1`, etc.) até o dongle
  específico estar conectado.

**O que ainda NÃO foi testado**: nenhum dongle USB foi conectado ainda
nesta sessão — pedido pro usuário `git pull` + `./install.sh` de novo
(aplica a blacklist + instala os pacotes) + conectar um dongle
Realtek/Ralink/Atheros/MediaTek + seguir as instruções impressas no
fim do `install.sh` pra configurar a rede.

## -6. Controle remoto IR do hardware não fazia nada — RESOLVIDO (POWER em 2026-09-27; VOL+/VOL-/MUTE remapeados e confirmados com o controle físico em 2026-09-30, ver também item -8)
Pedido do usuário: "preciso reabilitar o controle remoto que veio com o
hardware". Não é regressão nossa — o Armbian genérico nunca tinha esse
suporte configurado (o firmware Android original do box trazia isso
pronto, mas fora do escopo desta imagem).

**Diagnóstico, feito ao vivo no RK3229 real via `dmesg`/`ir-keytable`**:
o receptor IR já é reconhecido pelo kernel de fábrica — driver
`gpio_ir_recv` carrega sozinho, registra `rc0`
(`/devices/platform/ir-receiver/rc/rc0`), decodificador NEC ligado. Só
que `ir-keytable -t` mostrava os eventos chegando como
`EV_MSC(scancode)` + `EV_SYN`, **nunca `EV_KEY`** — ou seja, o kernel
decodifica o sinal (protocolo `necx`, scancodes limpos e repetição
correta) mas não existe **keymap** (tradução scancode→tecla) carregado,
então nenhuma tecla chega em lugar nenhum. O próprio driver já reporta
esperar um keymap chamado `rc-rk322x-tvbox` (visível em `ir-keytable`,
campo "Default keymap"), mas esse arquivo não existe nesta imagem
Armbian — é específico do firmware Android original.

**Scancodes** (`ir-keytable -t`, controle físico desta unidade).
⚠️ **Corrigido em 2026-09-30**: o mapeamento original (captura em
sequência, 2026-09-27) estava deslocado — no uso real, VOL- aumentava e
VOL+ não fazia nada. Re-capturado **um botão por vez**, com o usuário
apertando só o botão pedido e eu lendo o log entre cada um:
- POWER → `0x50540` (não recapturado: desliga o aparelho; já tinha sido
  confirmado desligando)
- VOL+ → `0x50511` (antes: não mapeado)
- VOL- → `0x5054c` (antes: mapeado como VOL+)
- MUTE → `0x50541` (antes: mapeado como VOL-)
- `0x50518` (antes "MUTE") não saiu de nenhum desses botões — removido.
A regra udev passou a usar `ir-keytable -c -w` (sem `-c`, reaplicar ao
vivo soma os códigos novos aos antigos). Dica pra futuras capturas:
`ir-keytable -t` gravando em arquivo precisa de `stdbuf -oL`, senão o
arquivo fica vazio (buffer).

**Decisão do usuário sobre o botão POWER** (pergunta feita
explicitamente, não assumida): desligamento real do sistema
(`systemctl`/`poweroff`), não DPMS/standby nem suspend — suspend em
boards RK3229/Armbian é historicamente instável (pode não voltar
sozinho) e por isso foi descartado sem implementar. Religar depois de
um poweroff é só tirar/recolocar da tomada — mesmo fluxo que o kiosk já
usa pra ligar (ver README, "ao ligar na tomada, aparece a execução do
binário").

**Fix aplicado, tudo em `install.sh`** (nada mudou no binário C++ — o
controle remoto não navega slides, só liga/desliga e mexe no volume,
então fica inteiramente no nível de sistema):
1. Keymap gravado em `/etc/rc_keymaps/rc-rk322x-tvbox.toml` com os 4
   scancodes acima.
2. Regra `udev` própria (`/etc/udev/rules.d/99-tvbox-ir-remote.rules`)
   que roda `ir-keytable -w <keymap> -s $kernel` toda vez que uma
   interface `rc*` aparece — **decisão deliberada de não confiar no
   casamento automático do `/etc/rc_maps.cfg`** do pacote `ir-keytable`
   (mesmo padrão de preferir determinismo sobre autodetecção que já
   usamos pro `--gpu-context=x11egl` do mpv, ver item 5 abaixo).
   **Bug real corrigido nesta mesma sessão**: a primeira versão usava
   `-a` (`--auto-load`), que **não** carrega um keymap diretamente — é
   pra um arquivo estilo `rc_maps.cfg` que *associa* driver→tabela→
   arquivo, formato totalmente diferente do nosso `.toml`. Resultado:
   `Invalid parameter on line 1` ao tentar aplicar. A flag certa pra
   carregar um `.toml` de keymap direto num dispositivo é `-w`
   (`--write`, "adiciona" o keymap) — confirmado extraindo o `.deb` real
   do pacote Debian (`ir-keytable 1.22.1-5+b2`) e testando `-w` contra o
   nosso arquivo e contra `pine64.toml` (exemplo oficial do próprio
   pacote, mesmo protocolo `nec`/`necx`) nesta sandbox: ambos imprimem
   `Read <nome> table` com `-w`, e ambos falham com `Invalid parameter`
   usando `-a` — não era só o nosso arquivo, é uso errado da flag.
   Também corrigido o formato do `.toml` em si nessa mesma investigação:
   a sintaxe certa é `[protocols.scancodes]` (tabela, colchete simples)
   com pares `0xHEX = "KEY_NOME"`, não `[[protocols.scancodes]]` (array
   de tabelas) com campos `scancode =`/`keycode =` (o que eu tinha
   escrito na primeira tentativa, por analogia errada com a sintaxe de
   `[[protocols]]`).
3. `triggerhappy` (daemon leve, sem X, sem desktop) convertendo as
   teclas em ações: `KEY_POWER` → `/usr/sbin/poweroff`; `KEY_VOLUMEUP`/
   `KEY_VOLUMEDOWN`/`KEY_MUTE` → script `/usr/local/bin/tvbox-volume`
   (instalado pelo próprio `install.sh`) que **detecta em runtime** o
   primeiro mixer ALSA disponível via `amixer scontrols`, em vez de
   assumir um nome fixo tipo "Master" — necessário porque a saída de
   áudio deste hardware é via HDMI (`snd_soc_hdmi_codec`) e o nome do
   controle varia por board; se não houver mixer nenhum (comum em
   HDMI puro, onde a própria TV controla o volume), o script loga e sai
   sem erro em vez de quebrar.

**Verificado de fato no dispositivo real** (não só "deveria
funcionar"): usuário rodou `ir-keytable -t` antes e depois — antes, só
`EV_MSC`/`EV_SYN`; os 4 scancodes de POWER/VOL+/VOL-/MUTE foram
capturados e confirmados nominalmente pelo usuário na ordem que
apertou. `install.sh` com as novas seções foi validado com `bash -n`
(sintaxe), mas a aplicação de ponta a ponta (keymap carregando via
`udev` + `triggerhappy` disparando `poweroff`/`amixer` de verdade ao
apertar o controle) **ainda não foi testada após o `git pull` +
`./install.sh`** — pedido pro usuário rodar de novo no dispositivo e
confirmar. Testar `poweroff` de propósito desliga o aparelho de
verdade, então não foi acionado remotamente durante o diagnóstico.

**Confirmado de ponta a ponta no dispositivo real**: usuário aplicou o
keymap corrigido na mão (`ir-keytable -w ... -s rc0`, bypassando o
`udev` de propósito pra isolar o teste), apertou POWER e reportou:
"apertei power, funcionou e desligou" — `triggerhappy` capturou o
`KEY_POWER`, disparou `/usr/sbin/poweroff`, e o sistema desligou de
verdade. Cadeia inteira validada: receptor → decodificação `necx` →
keymap → `EV_KEY` → `triggerhappy` → `poweroff`.

**Ainda não testado nesta rodada**: VOL+/VOL-/MUTE (o dispositivo
desligou antes de chegar a testar esses três) e se a regra `udev`
aplica o keymap sozinha num boot real (o teste que funcionou foi manual
via `-w`, bypassando o `udev` de propósito pra isolar se o problema era
o `.toml`/flag ou o `udev` em si — ver histórico de bugs corrigidos
nesta mesma sessão logo abaixo). Como o boot real gera um evento
`ACTION=="add"` genuíno quando o kernel cria `rc0` (diferente da
simulação manual via `udevadm trigger`, que manda `change` por padrão
— bug também corrigido nesta sessão), a expectativa é que funcione
sozinho sem intervenção manual, mas isso precisa ser confirmado no
próximo boot depois de religar da tomada.

**O que ficou fora do escopo, documentado mas não implementado**: os
outros botões do controle (setas, OK, voltar, números) foram
capturados no log bruto mas não mapeados — o usuário optou por só
POWER e VOL+/VOL-/MUTE por ora. Se no futuro o app ganhar navegação
manual de slides (hoje é 100% automático por `duracao_segundos`, ver
[[architecture]]), esses scancodes extras já estão registrados na
sessão e podem ser adicionados ao mesmo `.toml` sem precisar recapturar
nada.

### Adendo (mesma sessão): indicador visual de volume, integrado ao app C++
Depois de confirmar POWER funcionando, o usuário perguntou se
VOL+/VOL-/MUTE mostrariam algo na tela — não mostravam (implementação
original era 100% `triggerhappy`+`amixer`, sem UI nenhuma). Perguntei se
queria um overlay separado ou o próprio app Raylib desenhando; usuário
escolheu o app desenhar ele mesmo, no mesmo estilo visual do resto do
kiosk.

**Implementado**: novo módulo `include/remote_control.h`/
`src/remote_control.cpp` — lê `/dev/input/eventN` do receptor
diretamente (acha o device certo varrendo `/proc/bus/input/devices` por
`gpio_ir_recv`, sem assumir número fixo), sem depender de
`triggerhappy`/`raylib.h`/`Xlib.h` (mesma regra de isolamento de headers
do `player.cpp`, ver [[architecture]]). `apps/tvbox_esus_app.cpp` faz
`PollButtonPress()` uma vez por frame; ao detectar VOL+/VOL-/MUTE, ajusta
o volume via `amixer` (fork/exec) e ativa `ui.h::DrawVolumeOsd` por
`kVolumeOsdDurationSeconds` (2s, `config.h`).

**Decisão de posicionamento que evitou um bug de UX antes de escrever
qualquer código**: o indicador NÃO pode ser desenhado na área do
banner — a janela X11 do `mpv` (filha da janela do Raylib) fica
posicionada exatamente ali sempre que um vídeo está tocando, e cobriria
fisicamente qualquer coisa que o Raylib desenhasse embaixo, na tela
real (não é um bug de código, é como composição de janelas X11
funciona — mesma categoria de limitação já documentada no item 5 sobre
o `mpv --wid`). Como a maioria dos slides de `campaigns.conf` é vídeo,
um indicador ali ficaria invisível quase sempre. Corrigido desenhando
sobre o **rodapé** em vez do banner — área que o Raylib sempre controla
sozinho, nunca coberta pelo `mpv`.

**Removido de `install.sh`/`triggerhappy` nesta mesma mudança**: as
linhas de `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`/`KEY_MUTE` e o script
`/usr/local/bin/tvbox-volume` — como o app C++ agora lê o mesmo
`/dev/input` e já ajusta o `amixer` sozinho, deixar o `triggerhappy`
reagindo às mesmas teclas duplicaria o ajuste (dois processos mexendo
no mesmo mixer pro mesmo toque). `KEY_POWER` continua só no
`triggerhappy` (não precisa de feedback visual, e um desligamento real
fica mais robusto fora do processo principal do kiosk).

**Verificado nesta sandbox (sem hardware IR real aqui)**: `cmake
--build` limpo, zero warnings no código novo. Rodei o binário de
verdade com `mpv` tocando um vídeo real — capturei screenshot
confirmando que o rodapé continua normal (sem indicador, como esperado:
`RemoteControl` não encontra `gpio_ir_recv` nesta sandbox x86_64, então
`PollButtonPress()` sempre devolve `kNone` e o rodapé nunca troca).
**Não testado no dispositivo real ainda**: pedido pro usuário `git
pull` + rebuild (`make` no dispositivo, ou compilar no host e copiar o
binário — ver README) + `./install.sh` de novo (pra aplicar a mudança
no `triggerhappy`) + reteste físico de VOL+/VOL-/MUTE, incluindo
confirmar que o indicador aparece no rodapé mesmo com um vídeo tocando.

## -5. Vídeo ficava preto pra sempre SÓ no RK3229 real, depois da animação de loading ser adicionada — RESOLVIDO e CONFIRMADO no hardware real (2026-09-29/30)
**Confirmação (2026-09-29/30, via SSH)**: vídeos tocando de verdade no
RK3229 com a detecção por `playback-restart`, vistos em screenshots da
tela real (`ffmpeg -f x11grab`), tanto por streaming quanto do cache
local, e depois de um boot real.

Usuário confirmou o regressivo mais direto desta sessão toda: "Mas
estava funcionando, foi depois de adicionar o loading" — ou seja, o
vídeo tocava certinho ANTES da parte 13 (animação de carregamento +
confirmação via IPC antes de mapear a janela) e parou de funcionar
(fica preto pra sempre) depois, só no dispositivo real — nunca
reproduziu nesta sandbox x86_64/VAAPI.

**Causa provável**: `IsVideoActuallyPlaying()` (adicionado na parte 13)
confirmava playback checando a propriedade `time-pos` via polling (uma
pergunta por frame, 30x/segundo) — assim que vinha um número (não
`null`), mapeava a janela do vídeo. Problema: `time-pos` reflete o
relógio interno de playback do mpv, que pode começar a avançar ANTES
do primeiro frame decodificado ter sido de fato composto/exibido na
janela X11 — a folga entre "relógio avançando" e "frame realmente na
tela" é imperceptível num decode rápido (VAAPI/x86, como aqui na
sandbox), mas pode ser bem maior num pipeline de hardware mais lento
ou diferente (`rkmpp` no Mali-400 do RK3229, presumivelmente o que
`--hwdec=auto` escolhe lá) — mapeando a janela de vídeo antes dela ter
qualquer frame de verdade pra mostrar, resultando em preto permanente
(o "loading" desaparece — a confirmação disparou — mas nada aparece no
lugar, e nosso código para de checar qualquer coisa depois de
confirmado uma vez).

**Como cheguei nessa causa**: reproduzi o pipeline completo várias
vezes nesta sandbox depois do usuário relatar o bug, incluindo uma
inspeção direta e ao vivo do socket IPC do mpv (via `socat`) durante a
fase de resolução/buffering de um vídeo real do YouTube — confirmei
que o mpv manda sozinho (sem pedirmos nada) uma sequência de eventos
JSON pro socket, terminando em `{"event":"playback-restart"}`, que a
própria documentação do mpv descreve como o sinal de "o vídeo
realmente recomeçou a tocar" — a mesma técnica usada por bibliotecas
cliente do mpv (ex.: `wait_for_playback()` do python-mpv). Não consegui
provar a corrida exata (`time-pos` disparando ANTES de
`playback-restart`) nesta sandbox porque aqui a resolução é rápida
demais (~1s) pra separar os dois eventos com as ferramentas que tinha
à mão — mas trocar pro sinal que o próprio mpv documenta como
definitivo elimina a ambiguidade de qualquer jeito, independente de eu
conseguir reproduzir a corrida exata aqui.

**Fix aplicado**: `IsVideoActuallyPlaying()` (`src/player.cpp`) não
faz mais polling de `time-pos` — só escuta passivamente o socket IPC
(sem mandar pergunta nenhuma) esperando a linha
`{"event":"playback-restart"}`, e só aí mapeia a janela. Efeito
colateral bom: também é mais leve (zero queries/segundo em vez de 30,
importante no CPU fraco do RK3229). Retestado de ponta a ponta nesta
sandbox depois da mudança: vídeo continua confirmando e tocando
normalmente (ainda mais rápido a aparecer que antes, na prática).

**O que NÃO está confirmado ainda**: não tenho como reproduzir a
corrida original (decode lento o bastante pra `time-pos` mentir) nesta
sandbox, então não tenho uma prova direta de que ESTE era o bug exato
visto no RK3229 — é a explicação mais plausível e mais bem
fundamentada que encontrei, e a correção (usar o sinal que o mpv
recomenda) é estritamente melhor independente disso. Precisa
confirmação real do usuário no dispositivo.

## -4. Cursor do mouse visível + mensagens do navegador (tradução automática, aviso de flag não suportada) — RESOLVIDO em 2026-09-27
Usuário reportou, depois de confirmar que o Chromium já renderizava:
"nao quero que apareca o cursor. nem mensagens do navegador como O
tradutor" (também tinha visto o banner "You are using an unsupported
command-line flag: -no-sandbox").

**Cursor**: `exec.sh` já tinha lógica pra esconder via `unclutter`, mas
era só opcional (`if command -v unclutter`) — no dispositivo real esse
pacote nunca foi instalado por `install.sh`, então o cursor ficava
visível. Troquei a dependência preferida pra `unclutter-xfixes` (usa a
extensão Xfixes do X, mais confiável que o `unclutter` clássico —
projetos de kiosk reportam o clássico falhando em esconder o cursor
sobre janelas filhas, que é exatamente o tipo de janela que o `mpv` usa
aqui via `--wid`), com fallback pro `unclutter` clássico se só ele
estiver disponível. Adicionado a `install.sh`.

**Mensagens do navegador**: `exec.sh` já tinha `--disable-translate` e
`--disable-features=Translate,TranslateUI`, mas a barra de tradução
apareceu mesmo assim (confirmado pelo usuário) — flag de linha de
comando não é garantia de verdade pra isso em todas as versões do
Chromium. A forma realmente suportada é política de enterprise via
JSON: `install.sh` agora escreve
`/etc/{chromium,chromium-browser}/policies/managed/tvbox-esus-kiosk.json`
com `TranslateEnabled: false` (desliga tradução automática de vez) e
`CommandLineFlagSecurityWarningsEnabled: false` (desliga o aviso de
"flag não suportada" causado pelo `--no-sandbox` do item -3 abaixo).
Essas duas chaves são políticas reais e documentadas do Chromium, não
um hack — ainda **não confirmado no dispositivo real** (aplicado nesta
sessão, aguardando `./install.sh` + reteste).

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

## -2. Painel institucional nos outros 75% da tela (Chromium) — RESOLVIDO em 2026-09-27
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

**Bug encontrado e corrigido: mesma categoria do `mpv --wid` (item 5)**.
Depois que o usuário instalou `chromium` na sandbox e testamos de
verdade, a janela do painel simplesmente não aparecia — `xwininfo` não
mostrava nenhuma janela com o título/classe do painel, mesmo com o
processo do Chromium rodando normalmente. Causa: Chromium, assim como o
`mpv`, prefere criar uma superfície Wayland nativa sempre que existe um
compositor Wayland alcançável (`$WAYLAND_DISPLAY`), ignorando
completamente `--window-position`/`--window-size` (que só fazem sentido
em X11) — a janela existe, só que nunca aparece como uma janela X11
visível. Confirmado isolando a variável: sem forçar a plataforma, o
processo GPU/renderer do Chromium mostrava `--ozone-platform=wayland`
em `ps aux`; adicionando `--ozone-platform=x11` explicitamente, a janela
passou a aparecer no `xwininfo` (`"PEC" ("esus.treslagoas.ms.gov.br__painel"
"Chromium")`) na posição/tamanho corretos, e um screenshot confirmou o
painel renderizando de verdade (tela de pareamento "Conectar painel de
chamadas" do SAÚDE e-SUS Atenção Primária, com código de 4 dígitos).

**Fix aplicado em `exec.sh`**: flag `--ozone-platform=x11` adicionada
à chamada do Chromium, junto com um comentário explicando o motivo
(mesmo padrão do `--gpu-context=x11egl` do mpv, ver item 5).

**Segundo bug, encontrado só no dispositivo REAL (não reproduz nesta
sandbox, onde roda como usuário comum)**: mesmo com
`--ozone-platform=x11`, o Chromium ainda não abria no RK3229 — o
`panel.log` mostrava `[ERROR] Running as root without --no-sandbox is
not supported` em loop infinito (o Armbian do dispositivo loga direto
como `root`, sem usuário separado). **Fix**: adicionada `--no-sandbox`
à chamada do Chromium em `exec.sh`. Isso desativa o sandbox de
isolamento do processo de renderização — normalmente uma perda real de
segurança, mas aqui o dispositivo inteiro já roda como root sem
nenhuma outra camada de isolamento (não tem usuário dedicado nem outro
tipo de sandboxing), então não piora o modelo de ameaça de um
equipamento fechado que só existe pra mostrar um painel institucional.

**Terceiro problema, também só no dispositivo real**: `xrandr` não
estava instalado (`-bash: xrandr: command not found`), então a detecção
de resolução em `exec.sh` sempre caía no fallback fixo (1920x1080,
calculando 1440px de painel) mesmo a tela real sendo outra resolução.
`xrandr` vem do pacote `x11-xserver-utils` (que também traz o `xset`
usado pra desligar blank/DPMS/screensaver) — faltava em `install.sh`,
adicionado agora.

**Confirmado no dispositivo real em 2026-09-27**: com os três fixes
(`--ozone-platform=x11`, `--no-sandbox`, `x11-xserver-utils` instalado)
o Chromium renderiza o painel de verdade no RK3229. Único resquício:
o próprio `--no-sandbox` dispara o aviso padrão do Chromium "You are
using an unsupported command-line flag" — não é um erro, é só um
banner informativo, mas não pode aparecer num kiosk (ver item -1.1
abaixo pro fix).

**O que ainda NÃO foi validado**: o impacto de RAM de rodar Chromium +
nosso app + `mpv` decodificando ao mesmo tempo no orçamento de 2GB
(risco real — Chromium sozinho já costuma passar de 200-300MB) — ver
item de RAM aberto na sessão.

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
