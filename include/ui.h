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

// Desenha título/subtítulo da campanha + um spinner animado, usado
// enquanto o vídeo ainda está resolvendo a URL/bufferizando (o mpv
// ainda não confirmou playback real — ver
// include/player.h::IsVideoActuallyPlaying). `elapsedSeconds` é só pra
// animar o spinner (ex.: `GetTime()`), não precisa ser um cronômetro
// próprio.
void DrawLoadingSlide(Rectangle area, const Campaign &campaign, float elapsedSeconds);

// Desenha o indicador de volume (percentual + barra, ou "MUDO"), usado
// ao reagir a VOL+/VOL-/MUTE do controle remoto (ver
// include/remote_control.h). Pensado pra substituir temporariamente o
// conteúdo de `DrawChromeBar` (header/footer) durante
// kVolumeOsdDurationSeconds — nunca a área do banner, que fica coberta
// pela janela X11 do mpv sempre que um vídeo está tocando (ver
// include/player.h), o que esconderia o indicador na maior parte do
// tempo.
void DrawVolumeOsd(Rectangle area, int percent, bool muted, Color background, Color foreground);

} // namespace kiosk

#endif // TVBOX_UI_H
