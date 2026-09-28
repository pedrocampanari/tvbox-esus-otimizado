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

// Reproduz vídeos fora do pipeline do Raylib: cria uma janela X11
// **filha de verdade** da janela principal do Raylib (via
// XCreateWindow com o ID nativo do Raylib como pai — ver
// include/native_window.h) e delega a decodificação/desenho a um
// processo `mpv` externo (`--wid=<janela>`), pra aproveitar decode por
// hardware e isolar crashes de decoder do processo principal. Nunca usa
// iframe/webview — para vídeos de YouTube/Instagram/Facebook, a URL
// original é passada direto pro mpv, que resolve via seu hook interno
// (`ytdl_hook`, que por sua vez chama `yt-dlp`), em vez de nós mesmos
// chamarmos `yt-dlp -g` e tentarmos montar a URL de stream. Ver
// docs/memory/known-issues.md item 5 pro porquê dessa escolha (yt-dlp
// sozinho, sem stream progressiva disponível, imprime vídeo e áudio em
// URLs separadas — só o mpv sabe tocar isso sem precisar de mux).
//
// Por que janela FILHA e não uma segunda janela top-level
// `override-redirect` posicionada manualmente (jeito antigo): sob
// GNOME/mutter (Wayland + XWayland), uma segunda janela top-level de um
// cliente Xlib cru não é tratada como "sobreposição sem gerência" —
// mutter a exibe como uma janela própria e independente, ignorando a
// posição pedida (confirmado ao vivo: o vídeo aparecia numa aba
// separada, flutuando, em vez de encaixado). Uma janela FILHA (dentro
// da árvore X11 da janela do Raylib) não tem esse problema: ela nunca
// vira uma superfície Wayland própria — é sempre composta como parte da
// janela pai, com posição sempre relativa a ele. Ver [[known-issues]].
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;

    // `parentWindowId` é o ID de janela X11 nativa da janela do Raylib
    // (ver include/native_window.h::GetNativeX11WindowId). `x`/`y` são
    // relativos ao canto superior esquerdo dessa janela pai (não
    // coordenadas absolutas de tela).
    bool Init(unsigned long parentWindowId, int x, int y, int width, int height);

    // Ajusta posição/tamanho (ex.: se a área do banner mudar). x/y
    // continuam relativos à janela pai.
    void SetGeometry(int x, int y, int width, int height);

    // Começa a tocar a campanha de vídeo informada. Mata qualquer
    // reprodução anterior antes de iniciar a nova. A janela de vídeo
    // fica ESCONDIDA até a primeira confirmação real de playback via
    // `IsVideoActuallyPlaying()` — enquanto isso, quem chamou deve
    // desenhar uma animação de carregamento por cima (ver
    // include/ui.h::DrawLoadingSlide).
    bool Play(const Campaign &campaign);

    // Esconde a janela de vídeo e mata o processo mpv, se houver.
    void Stop();

    bool IsPlaying() const;

    // Detecta playback real via o evento "playback-restart" que o mpv
    // manda sozinho (sem precisar perguntar nada) pelo seu socket IPC
    // JSON assim que o primeiro frame de verdade foi (re)configurado e
    // está saindo — não é mais baseado em polling de propriedade (ver
    // src/player.cpp pro histórico do porquê). Retorna `false` até a
    // primeira confirmação; a partir daí sempre `true` (até o próximo
    // `Play()`/`Stop()`), e nesse instante exato a janela de vídeo é
    // mapeada/exibida pela primeira vez — antes disso ela fica
    // escondida, pra dar tempo da animação de carregamento (desenhada
    // por fora, no Raylib) aparecer sem um quadro preto do mpv por
    // cima. Chamar isso todo frame enquanto o slide de vídeo estiver
    // ativo e ainda não confirmado.
    bool IsVideoActuallyPlaying();

private:
    Display *display_ = nullptr;
    Window videoWindow_ = 0;
    pid_t mpvPid_ = -1;

    int ipcSocketFd_ = -1;
    std::string ipcSocketPath_;
    std::string ipcReadBuffer_;
    bool videoConfirmedPlaying_ = false;

    // Valida a URL da campanha (trata "TODO"/vazio) e a repassa como
    // está — o mpv que resolve internamente se não for um arquivo direto.
    std::string ResolveStreamUrl(const Campaign &campaign) const;

    void CloseIpcSocket();
};

} // namespace kiosk

#endif // TVBOX_PLAYER_H
