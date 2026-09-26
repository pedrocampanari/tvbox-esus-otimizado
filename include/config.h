#ifndef TVBOX_CONFIG_H
#define TVBOX_CONFIG_H

#include "raylib.h"

// Janela do kiosk: fração da tela do monitor primário.
// Requisito do projeto: fixo em 25% de largura e 100% de altura,
// ancorado no canto superior esquerdo.
namespace kiosk {

constexpr float kWindowWidthFraction = 0.25f;
constexpr float kWindowHeightFraction = 1.0f;

// URL da página que estamos replicando/raspando.
constexpr const char *kDisplayUrl = "https://esustv.jfbatl.com.br/display";

// Intervalo de novo scraping, em segundos. Mesmo valor do polling de
// segurança usado pelo app original (refetchInterval: 30000ms).
constexpr int kScrapePollIntervalSeconds = 30;

// Timeout de rede para o processo curl, em segundos.
constexpr int kFetchTimeoutSeconds = 10;

// Duração padrão de slide quando a campanha não define uma (mesma regra
// do app original: usa 10s se duracao_segundos <= 0).
constexpr int kDefaultSlideDurationSeconds = 10;

// Paleta replicada dos tokens CSS reais do site (ver
// docs/memory/frontend-contract.md).
constexpr Color kColorAppBackground = {0x0b, 0x1c, 0x33, 0xff};
constexpr Color kColorChromeBackground = {0x0d, 0x47, 0xa1, 0xff};
constexpr Color kColorChromeText = {0xff, 0xff, 0xff, 0xff};
constexpr Color kColorMediaBackground = {0x06, 0x12, 0x1f, 0xff};
constexpr Color kColorTextFg = {0xff, 0xff, 0xff, 0xff};
constexpr Color kColorTextAccent = {0xbf, 0xdb, 0xfe, 0xff};

} // namespace kiosk

#endif // TVBOX_CONFIG_H
