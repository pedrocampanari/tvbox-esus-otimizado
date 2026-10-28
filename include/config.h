#ifndef TVBOX_CONFIG_H
#define TVBOX_CONFIG_H

#include "raylib.h"
#include "video_config.h"

// Janela do kiosk: fração da tela do monitor primário.
// Requisito do projeto: fixo em 25% de largura e 100% de altura.
namespace kiosk {

constexpr float kWindowWidthFraction = 0.25f;
constexpr float kWindowHeightFraction = 1.0f;

// Canto de ancoragem horizontal: `true` = canto superior direito
// (pedido do usuário em 2026-09-27, estava no esquerdo); `false` =
// canto superior esquerdo. A altura sempre ocupa 100%, então só a
// ancoragem horizontal importa aqui.
constexpr bool kAnchorWindowToRightEdge = true;

// URL da página que estamos replicando. Dela sai o bundle com o
// endereço e a chave pública do backend (ver include/campaign_source.h).
constexpr const char *kDisplayUrl = "https://esustv.jfbatl.com.br/display";

// De quanto em quanto tempo a lista de campanhas é consultada no backend
// (pedido do usuário em 2026-10-28: 5 min; o site usa 30 s + realtime).
// Enquanto nenhuma consulta deu certo neste boot (rede ainda subindo),
// tenta de novo mais cedo.
constexpr int kCampaignsPollIntervalSeconds = 5 * 60;
constexpr int kCampaignsRetryIntervalSeconds = 30;

// Timeout de rede para o processo curl, em segundos.
constexpr int kFetchTimeoutSeconds = 10;

// Duração padrão de slide quando a campanha não define uma (mesma regra
// do app original: usa 10s se duracao_segundos <= 0).
constexpr int kDefaultSlideDurationSeconds = 10;

// Quanto tempo esperar o mpv confirmar playback real (via IPC, ver
// include/player.h::IsVideoActuallyPlaying) antes de desistir e tratar
// como falha. NÃO é o mesmo caso do timeout de iframe do site original
// (`h=8e3` no bundle JS) — aquele só media o carregamento de uma PÁGINA
// já hospedada pelo YouTube; o nosso mpv precisa rodar o `yt-dlp`
// (Python) pra resolver a URL do zero, o que no CPU fraco do RK3229
// pode facilmente passar de 8s (confirmado: 8s causava timeout em TODO
// vídeo no dispositivo real, deixando só o spinner aparecendo pra
// sempre). 30s dá folga de verdade pro `yt-dlp` + buffering inicial
// numa Cortex-A7 fraca sem travar a experiência por tempo demais numa
// falha real.
constexpr int kVideoLoadTimeoutSeconds = 30;

// Quanto tempo o indicador de volume (ver include/remote_control.h e
// include/ui.h::DrawVolumeOsd) fica substituindo o rodapé depois de
// apertar VOL+/VOL-/MUTE no controle remoto, antes de voltar sozinho ao
// conteúdo normal.
constexpr float kVolumeOsdDurationSeconds = 2.0f;

// Botoes de segurar do controle remoto (ver include/remote_control.h e a
// secao 7 do manual da BTV11). 3 segurado kPanelResetHoldSeconds =
// reseta o painel (apaga o perfil do Chromium: cookies, localStorage,
// vinculo do e-SUS) assim que o tempo completa; soltar antes cancela. O
// feedback no rodape aparece a partir de kHoldFeedbackSeconds, pra um
// toque acidental nao piscar nada na tela.
constexpr double kHoldFeedbackSeconds = 1.0;
constexpr double kPanelResetHoldSeconds = 10.0;
// POWER segurado reinicia o aparelho. O valor que vale de verdade e o
// POWER_HOLD_SECONDS do script do triggerhappy (install.sh); este aqui
// so alimenta a contagem regressiva na tela — manter os dois iguais.
constexpr double kPowerRebootHoldSeconds = 5.0;
// Quanto tempo o aviso "PAINEL RESETADO" fica no rodape.
constexpr float kNoticeOsdDurationSeconds = 5.0f;
// Script que reabre (reload) ou reseta o Chromium do painel — ver
// panel-ctl.sh. Relativo ao cwd (raiz do projeto, garantido pelo exec.sh).
constexpr const char *kPanelCtlScript = "./panel-ctl.sh";

// Menu "Configuracao da BTV" (include/setup_menu.h, secoes 3-6 do manual
// da BTV11). Helper de rede (netplan/networkd) e arquivo com o estado da
// configuracao (marcador de primeiro uso, rede escolhida, slug da UBS),
// ambos relativos ao cwd como o panel-ctl.sh. O arquivo fica fora do git.
constexpr const char *kNetCtlScript = "./net-ctl.py";
constexpr const char *kBtvConfigPath = "btv.conf";
// Boot ja configurado e sem rede: espera isto e entao abre o menu.
constexpr int kBootNetworkWaitSeconds = 15;
// Alvo do "Testar conexao e entrar" — manter igual ao PANEL_URL do exec.sh.
// So informativo: servidor fora do ar com a rede ok ainda entra (o
// watchdog do exec.sh reabre o painel quando ele voltar).
constexpr const char *kNetworkTestUrl = "https://esus.treslagoas.ms.gov.br/painel";

// Fonte de conteúdo (autorizado pelo dono do sistema em 2026-10-28):
// a lista vem do backend do site, consultada a cada
// kCampaignsPollIntervalSeconds — trocou no site, troca aqui. A última
// lista recebida fica em kLiveCampaignsCachePath e é a usada no boot
// (sem rede, o kiosk segue com ela). config/campaigns.conf (lista fixa
// de 2026-09-26) só vale até a primeira consulta dar certo.
// `kUseLiveCampaigns = false` volta ao modo só-lista-fixa.
constexpr bool kUseLiveCampaigns = true;
constexpr const char *kFixedCampaignsConfigPath = "config/campaigns.conf";
constexpr const char *kLiveCampaignsCachePath = "cache/campaigns-live.conf";
// Idem para cabeçalho/rodapé (TvChrome, include/campaign.h).
constexpr const char *kLiveTvChromeCachePath = "cache/tv-chrome-live.conf";

// Paleta replicada dos tokens CSS reais do site (ver
// docs/memory/frontend-contract.md). Header e footer têm cores
// diferentes na instância real observada (header azul, footer
// vermelho) — confirmado via inspeção ao vivo em 2026-09-26.
constexpr Color kColorAppBackground = {0x0b, 0x1c, 0x33, 0xff};
constexpr Color kColorHeaderBackground = {0x0d, 0x47, 0xa1, 0xff};
constexpr Color kColorFooterBackground = {0xf4, 0x15, 0x15, 0xff};
constexpr Color kColorChromeText = {0xff, 0xff, 0xff, 0xff};
constexpr Color kColorMediaBackground = {0x06, 0x12, 0x1f, 0xff};
constexpr Color kColorTextFg = {0xff, 0xff, 0xff, 0xff};
constexpr Color kColorTextAccent = {0xbf, 0xdb, 0xfe, 0xff};

// Textos de header/footer confirmados ao vivo em 2026-09-26 (painel real
// em produção, não o fallback técnico genérico). Ver
// docs/memory/frontend-contract.md. Desde 2026-10-28 textos e cores vêm
// do site (configuracoes_tv.cabecalho_rodape_tv): estes e as cores acima
// só valem até a primeira consulta dar certo (ou kUseLiveCampaigns=false).
// UTF-8 com acentuação correta (revisão ortográfica de 2026-09-29): a
// fonte é carregada com os glifos de PT-BR (ver src/ui.cpp).
constexpr const char *kHeaderTitle = "PREFEITURA MUNICIPAL";
constexpr const char *kHeaderSubtitle = "Secretaria Municipal de Saúde";
constexpr const char *kFooterTitle = "TRÊS LAGOAS/MS";
constexpr const char *kFooterSubtitle = "Cada dia melhor";

// kYtdlFormatSelector mora em include/video_config.h (motivo: não pode
// depender de raylib.h — ver comentário lá) e é reexportado aqui via o
// include acima, pra quem só conhece config.h continuar enxergando
// kiosk::kYtdlFormatSelector normalmente.

// Tipografia: Liberation Sans (SIL OFL, metric-compatible com Arial) no
// lugar da fonte bitmap padrão do Raylib — bem mais formal, condizente
// com um painel institucional de governo. Ver assets/fonts/.
constexpr const char *kFontRegularPath = "assets/fonts/LiberationSans-Regular.ttf";
constexpr const char *kFontBoldPath = "assets/fonts/LiberationSans-Bold.ttf";

} // namespace kiosk

#endif // TVBOX_CONFIG_H
