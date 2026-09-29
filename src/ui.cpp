#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"

namespace kiosk {

namespace {

Font g_fontRegular{};
Font g_fontBold{};
bool g_fontsLoaded = false;

// Conjunto de codepoints suficiente pra PT-BR (ASCII + acentos/cedilha
// usados em português) — evita carregar o glyph set default (só
// latin-1 básico) e mantém o footprint da textura da fonte pequeno.
std::vector<int> BuildPortugueseCodepoints() {
    std::vector<int> cps;
    for (int c = 0x20; c <= 0x7e; ++c) cps.push_back(c); // ASCII imprimível
    static const int extra[] = {
        0xC0, 0xC1, 0xC2, 0xC3, 0xC7, 0xC8, 0xC9, 0xCA, 0xCD, 0xD3, 0xD4,
        0xD5, 0xDA, 0xDC, 0xE0, 0xE1, 0xE2, 0xE3, 0xE7, 0xE8, 0xE9, 0xEA,
        0xED, 0xF3, 0xF4, 0xF5, 0xFA, 0xFC,
    };
    for (int c : extra) cps.push_back(c);
    return cps;
}

// Font é uma struct leve (ids/ponteiros); retornar por valor evita
// pendurar uma referência num temporário quando cai no fallback
// GetFontDefault() (que retorna por valor).
Font RegularFont() { return g_fontsLoaded ? g_fontRegular : GetFontDefault(); }
Font BoldFont() { return g_fontsLoaded ? g_fontBold : GetFontDefault(); }

// Emula o clamp() do CSS: valor mínimo/preferencial(relativo à largura
// da área)/máximo.
float Clamp(float minVal, float preferredFractionOfWidth, float maxVal, float width) {
    return std::clamp(preferredFractionOfWidth * width, minVal, maxVal);
}

// Tamanho em bytes do codepoint UTF-8 que começa em `lead`. Usado pra
// quebrar palavras longas sem cortar um caractere acentuado (á, ã, ç...)
// ao meio.
size_t Utf8CharLen(unsigned char lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; // byte de continuação isolado/inválido: avança 1 pra não travar
}

// Quebra uma única "palavra" (sem espaços) que sozinha já é mais larga
// que a coluna disponível, em pedaços que cabem — último recurso do
// WrapText, equivalente ao `overflow-wrap: break-word` do CSS original.
std::vector<std::string> BreakOversizedWord(const Font &font, const std::string &word,
                                              float fontSize, float maxWidth) {
    std::vector<std::string> chunks;
    std::string current;
    size_t i = 0;
    while (i < word.size()) {
        size_t charLen = std::min(Utf8CharLen(static_cast<unsigned char>(word[i])), word.size() - i);
        std::string candidate = current + word.substr(i, charLen);
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x > maxWidth && !current.empty()) {
            chunks.push_back(current);
            current = word.substr(i, charLen);
        } else {
            current = candidate;
        }
        i += charLen;
    }
    if (!current.empty()) chunks.push_back(current);
    return chunks;
}

std::vector<std::string> WrapText(const Font &font, const std::string &text, float fontSize,
                                   float maxWidth) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;

    while (words >> word) {
        float wordWidth = MeasureTextEx(font, word.c_str(), fontSize, 0.0f).x;
        if (wordWidth > maxWidth) {
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            std::vector<std::string> chunks = BreakOversizedWord(font, word, fontSize, maxWidth);
            for (size_t idx = 0; idx + 1 < chunks.size(); ++idx) lines.push_back(chunks[idx]);
            if (!chunks.empty()) line = chunks.back(); // pode ainda combinar com a próxima palavra
            continue;
        }

        std::string candidate = line.empty() ? word : line + " " + word;
        float w = MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x;
        if (w > maxWidth && !line.empty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void DrawCenteredText(const Font &font, const char *text, float fontSize, Color color,
                      float centerX, float y) {
    float width = MeasureTextEx(font, text, fontSize, 0.0f).x;
    DrawTextEx(font, text, {centerX - width / 2.0f, y}, fontSize, 0.0f, color);
}

} // namespace

void LoadUiFonts() {
    std::vector<int> codepoints = BuildPortugueseCodepoints();
    g_fontRegular = LoadFontEx(kFontRegularPath, 64, codepoints.data(),
                                static_cast<int>(codepoints.size()));
    g_fontBold = LoadFontEx(kFontBoldPath, 64, codepoints.data(),
                              static_cast<int>(codepoints.size()));

    if (g_fontRegular.texture.id == 0 || g_fontBold.texture.id == 0) {
        TraceLog(LOG_WARNING,
                  "UI: nao foi possivel carregar Liberation Sans (%s / %s); usando fonte padrao do Raylib.",
                  kFontRegularPath, kFontBoldPath);
        g_fontsLoaded = false;
        return;
    }
    SetTextureFilter(g_fontRegular.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g_fontBold.texture, TEXTURE_FILTER_BILINEAR);
    g_fontsLoaded = true;
}

void UnloadUiFonts() {
    if (!g_fontsLoaded) return;
    UnloadFont(g_fontRegular);
    UnloadFont(g_fontBold);
    g_fontsLoaded = false;
}

void DrawChromeBar(Rectangle area, const char *title, const char *subtitle, Color background,
                    Color foreground) {
    DrawRectangleRec(area, background);

    float titleSize = Clamp(14.0f, 0.045f, 22.0f, area.width);
    float subSize = Clamp(11.0f, 0.03f, 15.0f, area.width);
    bool hasSubtitle = subtitle != nullptr && subtitle[0] != '\0';

    float blockHeight = titleSize + (hasSubtitle ? subSize * 1.3f : 0.0f);
    float y = area.y + (area.height - blockHeight) / 2.0f;

    DrawCenteredText(BoldFont(), title, titleSize, foreground, area.x + area.width / 2.0f, y);
    if (hasSubtitle) {
        y += titleSize * 1.3f;
        DrawCenteredText(RegularFont(), subtitle, subSize, Fade(foreground, 0.85f),
                          area.x + area.width / 2.0f, y);
    }
}

void DrawTextSlide(Rectangle area, const Campaign &campaign) {
    DrawRectangleRec(area, kColorMediaBackground);

    float centerX = area.x + area.width / 2.0f;
    float y = area.y + area.height * 0.12f;
    float padding = area.width * 0.08f;
    float maxTextWidth = area.width - 2 * padding;

    if (!campaign.titulo.empty()) {
        float titleSize = Clamp(20.0f, 0.11f, 48.0f, area.width);
        for (const auto &line : WrapText(BoldFont(), campaign.titulo, titleSize, maxTextWidth)) {
            DrawCenteredText(BoldFont(), line.c_str(), titleSize, kColorTextFg, centerX, y);
            y += titleSize * 1.2f;
        }
        y += titleSize * 0.15f;
    }

    if (!campaign.subtitulo.empty()) {
        float subSize = Clamp(14.0f, 0.075f, 32.0f, area.width);
        for (const auto &line : WrapText(RegularFont(), campaign.subtitulo, subSize, maxTextWidth)) {
            DrawCenteredText(RegularFont(), line.c_str(), subSize, kColorTextAccent, centerX, y);
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
        float bodySize = Clamp(14.0f, 0.06f, 26.0f, area.width);
        for (const auto &line : WrapText(RegularFont(), campaign.texto, bodySize, maxTextWidth)) {
            DrawCenteredText(RegularFont(), line.c_str(), bodySize, kColorTextFg, centerX, y);
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

void DrawLoadingSlide(Rectangle area, const Campaign &campaign, float elapsedSeconds) {
    DrawRectangleRec(area, kColorMediaBackground);

    float centerX = area.x + area.width / 2.0f;
    float padding = area.width * 0.08f;
    float maxTextWidth = area.width - 2 * padding;
    float y = area.y + area.height * 0.12f;

    if (!campaign.titulo.empty()) {
        float titleSize = Clamp(18.0f, 0.09f, 40.0f, area.width);
        for (const auto &line : WrapText(BoldFont(), campaign.titulo, titleSize, maxTextWidth)) {
            DrawCenteredText(BoldFont(), line.c_str(), titleSize, kColorTextFg, centerX, y);
            y += titleSize * 1.2f;
        }
        y += titleSize * 0.15f;
    }

    if (!campaign.subtitulo.empty()) {
        float subSize = Clamp(12.0f, 0.06f, 26.0f, area.width);
        for (const auto &line : WrapText(RegularFont(), campaign.subtitulo, subSize, maxTextWidth)) {
            DrawCenteredText(RegularFont(), line.c_str(), subSize, kColorTextAccent, centerX, y);
            y += subSize * 1.25f;
        }
    }

    // Spinner: arco de 270° girando continuamente, mesma ideia do
    // indicador de carregamento do site original enquanto resolve o
    // embed de vídeo. Usa o espaço restante entre o texto e o rodapé
    // da área do banner.
    float spaceBelow = (area.y + area.height) - y;
    float spinnerRadius = std::clamp(std::min(area.width, spaceBelow) * 0.18f, 14.0f, 40.0f);
    float spinnerCenterY = y + spaceBelow / 2.0f;
    if (spinnerCenterY + spinnerRadius > area.y + area.height) {
        spinnerCenterY = area.y + area.height - spinnerRadius - 8.0f;
    }

    float startAngle = std::fmod(elapsedSeconds * 220.0f, 360.0f);
    float endAngle = startAngle + 270.0f;
    DrawRing({centerX, spinnerCenterY}, spinnerRadius * 0.7f, spinnerRadius, startAngle, endAngle,
             32, kColorTextAccent);
}

void DrawVolumeOsd(Rectangle area, int percent, bool muted, Color background, Color foreground) {
    DrawRectangleRec(area, background);

    std::string label = muted ? "VOLUME: MUDO" : ("VOLUME: " + std::to_string(percent) + "%");
    float labelSize = Clamp(14.0f, 0.045f, 22.0f, area.width);
    float barHeight = Clamp(5.0f, 0.018f, 9.0f, area.width);
    float barWidth = std::clamp(area.width * 0.55f, 80.0f, 220.0f);

    float blockHeight = labelSize + barHeight * 1.8f;
    float y = area.y + (area.height - blockHeight) / 2.0f;
    float centerX = area.x + area.width / 2.0f;

    DrawCenteredText(BoldFont(), label.c_str(), labelSize, foreground, centerX, y);
    y += labelSize * 1.35f;

    Rectangle barBg = {centerX - barWidth / 2.0f, y, barWidth, barHeight};
    DrawRectangleRounded(barBg, 0.5f, 6, Fade(foreground, 0.3f));
    if (!muted && percent > 0) {
        float filled = barWidth * std::clamp(percent / 100.0f, 0.0f, 1.0f);
        DrawRectangleRounded({barBg.x, barBg.y, filled, barHeight}, 0.5f, 6, foreground);
    }
}

} // namespace kiosk
