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
        0xAA, 0xBA, 0xB0, // ª º ° (ex.: "1º", "38°C")
        0xB7, 0x2022,     // · • (dicas e senha mascarada do menu de configuração)
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

void DrawNoticeOsd(Rectangle area, const char *title, const char *subtitle, float progress,
                   Color background, Color foreground) {
    DrawRectangleRec(area, background);

    float titleSize = Clamp(13.0f, 0.040f, 20.0f, area.width);
    float subtitleSize = Clamp(11.0f, 0.030f, 16.0f, area.width);
    float blockHeight = titleSize * 1.3f + subtitleSize;
    float y = area.y + (area.height - blockHeight) / 2.0f;
    float centerX = area.x + area.width / 2.0f;

    DrawCenteredText(BoldFont(), title, titleSize, foreground, centerX, y);
    DrawCenteredText(RegularFont(), subtitle, subtitleSize, foreground, centerX,
                     y + titleSize * 1.3f);

    if (progress >= 0.0f) {
        float barHeight = Clamp(3.0f, 0.010f, 6.0f, area.width);
        Rectangle bar = {area.x, area.y + area.height - barHeight, area.width, barHeight};
        DrawRectangleRec(bar, Fade(foreground, 0.3f));
        bar.width *= std::clamp(progress, 0.0f, 1.0f);
        DrawRectangleRec(bar, foreground);
    }
}

void DrawSetupMenu(Rectangle area, const SetupMenuView &view, float elapsedSeconds) {
    DrawRectangleRec(area, kColorAppBackground);

    // Tela cheia, lida de longe: tamanhos pela ALTURA (a largura varia
    // muito entre TVs 16:9 e esta 1366x768).
    float unit = area.height / 100.0f;
    float barHeight = std::clamp(unit * 11.0f, 50.0f, 120.0f);
    Rectangle header = {area.x, area.y, area.width, barHeight};
    Rectangle footer = {area.x, area.y + area.height - barHeight * 0.7f, area.width,
                        barHeight * 0.7f};
    float centerX = area.x + area.width / 2.0f;

    DrawRectangleRec(header, kColorHeaderBackground);
    float titleSize = std::clamp(unit * 4.6f, 22.0f, 52.0f);
    float subSize = std::clamp(unit * 2.6f, 13.0f, 30.0f);
    float hy = header.y + (header.height - titleSize - subSize * 1.2f) / 2.0f;
    DrawCenteredText(BoldFont(), "CONFIGURAÇÃO DA BTV", titleSize, kColorChromeText, centerX, hy);
    DrawCenteredText(RegularFont(), view.subtitle.c_str(), subSize,
                     Fade(kColorChromeText, 0.85f), centerX, hy + titleSize * 1.15f);

    DrawRectangleRec(footer, kColorFooterBackground);
    float hintSize = std::clamp(unit * 2.4f, 12.0f, 28.0f);
    DrawCenteredText(RegularFont(), view.hint.c_str(), hintSize, kColorChromeText, centerX,
                     footer.y + (footer.height - hintSize) / 2.0f);

    float contentW = std::min(area.width - 2.0f * unit * 4.0f, unit * 120.0f);
    float left = centerX - contentW / 2.0f;
    float top = header.y + header.height + unit * 3.0f;
    float messageSize = std::clamp(unit * 2.6f, 13.0f, 30.0f);
    float bottom = footer.y - unit * 2.0f - (view.message.empty() ? 0.0f : messageSize * 2.0f);

    float labelSize = std::clamp(unit * 3.2f, 15.0f, 38.0f);
    float detailSize = std::clamp(unit * 2.4f, 12.0f, 28.0f);

    if (view.textEntry) {
        float y = top + unit * 4.0f;
        DrawCenteredText(BoldFont(), view.textLabel.c_str(), labelSize, kColorTextFg, centerX, y);
        y += labelSize * 1.8f;
        Rectangle field = {left, y, contentW, labelSize * 2.2f};
        DrawRectangleRounded(field, 0.2f, 8, kColorMediaBackground);
        DrawRectangleRoundedLinesEx(field, 0.2f, 8, 2.0f, kColorTextAccent);
        // Cursor piscando no fim do texto.
        std::string shown = view.textValue;
        if (std::fmod(elapsedSeconds, 1.0f) < 0.6f) shown += "|";
        DrawTextEx(RegularFont(), shown.c_str(),
                   {field.x + labelSize * 0.6f, field.y + (field.height - labelSize) / 2.0f},
                   labelSize, 0.0f, kColorTextFg);
    } else {
        float rowH = labelSize + detailSize * 1.5f + unit * 1.6f;
        float headingH = labelSize * 1.6f;
        int selectedIndex = 0;
        for (size_t i = 0; i < view.rows.size(); ++i) {
            if (view.rows[i].selected) selectedIndex = static_cast<int>(i);
        }
        // Rolagem: a partir de qual linha desenhar pra a selecionada caber.
        auto heightOf = [&](const MenuRowView &r) {
            return r.kind == MenuRowView::Kind::kHeading ? headingH : rowH;
        };
        size_t first = 0;
        for (;;) {
            float used = 0.0f;
            for (int i = static_cast<int>(first); i <= selectedIndex && i < static_cast<int>(view.rows.size()); ++i) {
                used += heightOf(view.rows[i]);
            }
            if (used <= bottom - top || static_cast<int>(first) >= selectedIndex) break;
            ++first;
        }

        float y = top;
        for (size_t i = first; i < view.rows.size(); ++i) {
            const MenuRowView &row = view.rows[i];
            float h = heightOf(row);
            if (y + h > bottom) break;
            if (row.kind == MenuRowView::Kind::kHeading) {
                DrawTextEx(BoldFont(), row.label.c_str(), {left, y + headingH - labelSize * 1.1f},
                           labelSize * 0.8f, 0.0f, kColorTextAccent);
                y += h;
                continue;
            }
            Rectangle box = {left, y, contentW, h - unit * 1.0f};
            if (row.selected) {
                DrawRectangleRounded(box, 0.15f, 8, kColorHeaderBackground);
                DrawRectangleRoundedLinesEx(box, 0.15f, 8, 3.0f, kColorChromeText);
            } else {
                DrawRectangleRounded(box, 0.15f, 8, kColorMediaBackground);
            }
            float textX = box.x + labelSize * 0.7f;
            if (row.kind == MenuRowView::Kind::kOption) {
                // Marcador de escolha desenhado (a fonte não tem os glifos ●/○).
                float r = labelSize * 0.38f;
                Vector2 c = {box.x + labelSize * 0.9f, box.y + box.height / 2.0f};
                DrawRing(c, r * 0.78f, r, 0.0f, 360.0f, 24, kColorChromeText);
                if (row.checked) DrawCircleV(c, r * 0.5f, kColorChromeText);
                textX = c.x + labelSize * 0.9f;
            }
            float ty = box.y + (box.height - labelSize - (row.detail.empty() ? 0 : detailSize * 1.25f)) / 2.0f;
            DrawTextEx(BoldFont(), row.label.c_str(), {textX, ty}, labelSize, 0.0f, kColorTextFg);
            if (!row.detail.empty()) {
                DrawTextEx(RegularFont(), row.detail.c_str(), {textX, ty + labelSize * 1.2f}, detailSize,
                           0.0f, Fade(kColorTextFg, 0.75f));
            }
            y += h;
        }
    }

    if (!view.message.empty()) {
        float my = footer.y - unit * 1.5f - messageSize * 1.4f;
        Color color = view.messageIsError ? Color{0xff, 0x8a, 0x80, 0xff} : kColorTextAccent;
        float textW = MeasureTextEx(RegularFont(), view.message.c_str(), messageSize, 0.0f).x;
        float spinnerR = view.busy ? messageSize * 0.5f : 0.0f;
        float startX = centerX - (textW + spinnerR * 3.0f) / 2.0f;
        if (view.busy) {
            float a = std::fmod(elapsedSeconds * 220.0f, 360.0f);
            DrawRing({startX + spinnerR, my + messageSize / 2.0f}, spinnerR * 0.6f, spinnerR, a,
                     a + 270.0f, 24, kColorTextAccent);
        }
        DrawTextEx(RegularFont(), view.message.c_str(), {startX + spinnerR * 3.0f, my}, messageSize,
                   0.0f, color);
    }
}

} // namespace kiosk
