#include "ui.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include "config.h"

namespace kiosk {

namespace {

// Emula o clamp() do CSS: valor mínimo/preferencial(relativo à largura
// da área)/máximo.
float Clamp(float minVal, float preferredFractionOfWidth, float maxVal, float width) {
    return std::clamp(preferredFractionOfWidth * width, minVal, maxVal);
}

std::vector<std::string> WrapText(const std::string &text, int fontSize, float maxWidth) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;

    while (words >> word) {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (MeasureText(candidate.c_str(), fontSize) > maxWidth && !line.empty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void DrawCenteredText(const char *text, int fontSize, Color color, float centerX, float y) {
    int width = MeasureText(text, fontSize);
    DrawText(text, static_cast<int>(centerX - width / 2.0f), static_cast<int>(y), fontSize, color);
}

} // namespace

void DrawChromeBar(Rectangle area, const char *text, Color background, Color foreground) {
    DrawRectangleRec(area, background);
    int fontSize = static_cast<int>(Clamp(14.0f, 0.045f, 22.0f, area.width));
    DrawCenteredText(text, fontSize, foreground, area.x + area.width / 2.0f,
                      area.y + (area.height - fontSize) / 2.0f);
}

void DrawTextSlide(Rectangle area, const Campaign &campaign) {
    DrawRectangleRec(area, kColorMediaBackground);

    float centerX = area.x + area.width / 2.0f;
    float y = area.y + area.height * 0.12f;
    float padding = area.width * 0.08f;
    float maxTextWidth = area.width - 2 * padding;

    if (!campaign.titulo.empty()) {
        int titleSize = static_cast<int>(Clamp(20.0f, 0.11f, 48.0f, area.width));
        for (const auto &line : WrapText(campaign.titulo, titleSize, maxTextWidth)) {
            DrawCenteredText(line.c_str(), titleSize, kColorTextFg, centerX, y);
            y += titleSize * 1.2f;
        }
        y += titleSize * 0.15f;
    }

    if (!campaign.subtitulo.empty()) {
        int subSize = static_cast<int>(Clamp(14.0f, 0.075f, 32.0f, area.width));
        for (const auto &line : WrapText(campaign.subtitulo, subSize, maxTextWidth)) {
            DrawCenteredText(line.c_str(), subSize, kColorTextAccent, centerX, y);
            y += subSize * 1.25f;
        }
    }

    // Separador fino, igual .text-campaign-separator.
    float sepWidth = std::clamp(area.width * 0.32f, 90.0f, 240.0f);
    y += area.height * 0.02f;
    DrawRectangle(static_cast<int>(centerX - sepWidth / 2.0f), static_cast<int>(y),
                  static_cast<int>(sepWidth), 1, Fade(kColorTextFg, 0.35f));
    y += area.height * 0.04f;

    if (!campaign.texto.empty()) {
        int bodySize = static_cast<int>(Clamp(14.0f, 0.06f, 26.0f, area.width));
        for (const auto &line : WrapText(campaign.texto, bodySize, maxTextWidth)) {
            DrawCenteredText(line.c_str(), bodySize, kColorTextFg, centerX, y);
            y += bodySize * 1.45f;
        }
    }
}

void DrawImageSlide(Rectangle area, Texture2D texture) {
    DrawRectangleRec(area, kColorMediaBackground);
    if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) return;

    float scale = std::min(area.width / static_cast<float>(texture.width),
                            area.height / static_cast<float>(texture.height));
    float drawWidth = texture.width * scale;
    float drawHeight = texture.height * scale;
    Rectangle dest = {area.x + (area.width - drawWidth) / 2.0f,
                       area.y + (area.height - drawHeight) / 2.0f, drawWidth, drawHeight};
    Rectangle src = {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, src, dest, {0, 0}, 0.0f, WHITE);
}

void DrawVideoPlaceholder(Rectangle area) {
    // O mpv desenha por cima desta área via a janela X11 dedicada; isto
    // só evita um "flash" de outra cor por baixo antes do mpv subir.
    DrawRectangleRec(area, kColorMediaBackground);
}

} // namespace kiosk
