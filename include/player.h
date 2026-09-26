#ifndef TVBOX_PLAYER_H
#define TVBOX_PLAYER_H

#include <sys/types.h>

#include <string>

#include "campaign.h"

// Forward declarations pra não vazar Xlib.h em quem só quer chamar o
// player (Xlib define macros genéricas como `Window`, `Display` que
// colidem fácil com outro código).
struct _XDisplay;
typedef struct _XDisplay Display;
typedef unsigned long Window;

namespace kiosk {

// Reproduz vídeos fora do pipeline do Raylib: cria uma janela X11 filha
// posicionada sobre a área do banner e delega a decodificação/desenho a
// um processo `mpv` externo (`--wid=<janela>`), pra aproveitar decode por
// hardware e isolar crashes de decoder do processo principal. Nunca usa
// iframe/webview — para vídeos de YouTube/Instagram/Facebook, resolve
// antes uma URL de stream direta via `yt-dlp -g`.
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;

    // Deve ser chamado uma vez, com o identificador da janela nativa X11
    // do Raylib (obtido via Xlib diretamente sobre o root window na
    // posição/tamanho da janela kiosk — ver apps/tvbox_esus_app.cpp).
    bool Init(int screenX, int screenY, int width, int height);

    // Ajusta posição/tamanho (ex.: se a área do banner mudar).
    void SetGeometry(int screenX, int screenY, int width, int height);

    // Começa a tocar a campanha de vídeo informada. Mata qualquer
    // reprodução anterior antes de iniciar a nova.
    bool Play(const Campaign &campaign);

    // Esconde a janela de vídeo e mata o processo mpv, se houver.
    void Stop();

    bool IsPlaying() const;

private:
    Display *display_ = nullptr;
    Window videoWindow_ = 0;
    pid_t mpvPid_ = -1;

    // Resolve a URL reproduzível pro mpv: passthrough para upload/direto,
    // `yt-dlp -g` para youtube/instagram/facebook.
    std::string ResolveStreamUrl(const Campaign &campaign) const;
};

} // namespace kiosk

#endif // TVBOX_PLAYER_H
