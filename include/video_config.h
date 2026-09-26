#ifndef TVBOX_VIDEO_CONFIG_H
#define TVBOX_VIDEO_CONFIG_H

// Constantes usadas por include/player.h / src/player.cpp. Ficam num
// header separado (sem depender de raylib.h) de propósito: player.cpp
// inclui <X11/Xlib.h>, que faz `typedef XID Font;` — isso colide de
// frente com o `typedef struct Font {...} Font;` do raylib.h se os dois
// forem incluídos na mesma translation unit. Ver include/config.h, que
// inclui raylib.h (pra Color) e por isso não pode ser incluído aqui.
namespace kiosk {

// Seletor de formato passado ao yt-dlp (via o hook interno do mpv, ver
// src/player.cpp). Duas restrições, as duas testadas de verdade em
// 2026-09-26 contra um vídeo real:
//   - `height<=720`: o RK3229 tem VPU/CPU modestos, e as telas de kiosk
//     alvo não costumam passar de 720p — decodificar mais que isso só
//     gasta banda/CPU à toa.
//   - `vcodec^=avc1` (H.264): SEM isso, o seletor abaixo escolhia AV1
//     (confirmado rodando `mpv --ytdl-format=...` de verdade — sem essa
//     restrição, a mesma URL resolvia pra `av01` mesmo capado em
//     height<=720). RK3229 é um chip de 2016; sua VPU Rockchip
//     (rkmpp) decodifica H.264 por hardware, mas quase certamente NÃO
//     tem decode de AV1 por hardware (isso só apareceu em SoCs
//     Rockchip bem mais recentes) — AV1 em software nesse CPU fraco
//     provavelmente não aguenta um kiosk contínuo. H.264 (`avc1`)
//     esteve disponível em toda resolução testada, então forçar essa
//     preferência não deveria fazer faltar stream em vídeos comuns do
//     YouTube.
// Ajustar aqui se o hardware real tiver decode de hardware pra outro
// codec (ex.: `--hwdec=help` no dispositivo mostra o que a VPU aceita).
constexpr const char *kYtdlFormatSelector =
    "bestvideo[vcodec^=avc1][height<=720]+bestaudio/"
    "best[vcodec^=avc1][height<=720]/best[height<=720]/best";

} // namespace kiosk

#endif // TVBOX_VIDEO_CONFIG_H
