#ifndef TVBOX_UI_H
#define TVBOX_UI_H

#include "campaign.h"
#include "raylib.h"

namespace kiosk {

// Carrega/libera a tipografia formal (Liberation Sans, ver
// include/config.h) usada por todas as funções de desenho abaixo.
// Init deve ser chamado uma vez depois de InitWindow(); Shutdown antes
// de CloseWindow(). Se o arquivo de fonte não for encontrado, cai de
// volta pra fonte padrão do Raylib (log de aviso, não é fatal).
void LoadUiFonts();
void UnloadUiFonts();

// Desenha a barra de topo/rodapé do "tv-chrome" (fundo colorido + título
// uppercase + subtítulo menor, ambos centralizados), replicando o
// header/footer do site original (que tem cores diferentes entre os
// dois — ver include/config.h).
void DrawChromeBar(Rectangle area, const char *title, const char *subtitle,
                    Color background, Color foreground);

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
