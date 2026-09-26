#ifndef TVBOX_UI_H
#define TVBOX_UI_H

#include "campaign.h"
#include "raylib.h"

namespace kiosk {

// Desenha a barra de topo/rodapé do "tv-chrome" (fundo colorido + texto
// uppercase centralizado), replicando o header/footer do site original.
void DrawChromeBar(Rectangle area, const char *text, Color background, Color foreground);

// Desenha um slide de texto (título/subtítulo/corpo), replicando as
// classes .text-campaign* do CSS original dentro da área do banner.
void DrawTextSlide(Rectangle area, const Campaign &campaign);

// Desenha uma imagem já carregada, com "letterbox" (object-fit: contain)
// igual ao `.institutional-media` do site original.
void DrawImageSlide(Rectangle area, Texture2D texture);

// Desenha o fundo neutro usado enquanto um slide de vídeo está tocando
// (o vídeo em si é desenhado por fora do Raylib, pelo mpv, numa janela
// X11 sobreposta — ver include/player.h).
void DrawVideoPlaceholder(Rectangle area);

} // namespace kiosk

#endif // TVBOX_UI_H
