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

// URL da página que estamos replicando/raspando.
constexpr const char *kDisplayUrl = "https://esustv.jfbatl.com.br/display";

// Intervalo de novo scraping, em segundos, usado apenas se o modo de
// scraping ao vivo (kUseLiveScraping) for reativado. Mesmo valor do
// polling de segurança usado pelo app original (refetchInterval: 30000ms).
constexpr int kScrapePollIntervalSeconds = 30;

// Timeout de rede para o processo curl, em segundos.
constexpr int kFetchTimeoutSeconds = 10;

// Duração padrão de slide quando a campanha não define uma (mesma regra
// do app original: usa 10s se duracao_segundos <= 0).
constexpr int kDefaultSlideDurationSeconds = 10;

// Fonte de conteúdo: por autorização do dono do sistema (2026-09-26),
// v1 usa uma lista FIXA de campanhas/vídeos (config/campaigns.conf),
// carregada uma única vez na inicialização — sem ficar buscando a URL
// original o tempo todo. `kUseLiveScraping = true` liga de volta o
// modo antigo (thread de polling em DisplayScraper), caso essa decisão
// mude no futuro. Ver docs/memory/known-issues.md.
constexpr bool kUseLiveScraping = false;
constexpr const char *kFixedCampaignsConfigPath = "config/campaigns.conf";

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
// docs/memory/frontend-contract.md. Sem leitura de configuracoes_tv
// neste modo (ver known-issues.md item 5), então ficam fixos aqui.
constexpr const char *kHeaderTitle = "PREFEITURA MUNICIPAL";
constexpr const char *kHeaderSubtitle = "Secretaria Municipal de Saude";
constexpr const char *kFooterTitle = "TRES LAGOAS/MS";
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
