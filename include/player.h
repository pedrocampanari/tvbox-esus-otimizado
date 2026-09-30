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
    // `localFile`: caminho do cache local (include/video_cache.h); se
    // vazio, faz streaming da URL da campanha via ytdl_hook do mpv.
    // Toca UMA vez (sem loop) — ver HasEnded(). Usa sempre o MESMO
    // processo mpv (lançado no primeiro Play(), relançado se morrer) e
    // troca de arquivo via IPC (`loadfile`): o contexto EGL do RK3229
    // (~3s pra criar) é pago uma única vez. A janela de vídeo fica
    // ESCONDIDA até a confirmação de playback (IsVideoActuallyPlaying) —
    // enquanto isso, quem chamou desenha a animação de carregamento.
    // `playEndSeconds` > 0: para nesse ponto (fim útil medido pelo cache
    // — corta o preto final do arquivo); 0 = até o fim.
    bool Play(const Campaign &campaign, const std::string &localFile, double playEndSeconds = 0);

    // Esconde a janela de vídeo e para o arquivo atual (o processo mpv
    // continua vivo, ocioso, pro próximo Play()).
    void Stop();

    // Stop() + mata o mpv, destrói a janela de vídeo e fecha a conexão
    // X11. Chamar ANTES do CloseWindow() do Raylib: a janela de vídeo é
    // filha da dele e morre junto; mexer nela depois gera BadWindow, que
    // o Xlib trata abortando o processo (exit 1). Idempotente; o
    // destrutor também chama.
    void Shutdown();

    // true uma única vez quando o vídeo do último Play() acabou: fim do
    // arquivo (sucesso), erro ao abrir/resolver, ou o mpv morreu. A
    // janela já é escondida no mesmo instante. Chamar todo frame
    // enquanto o slide de vídeo estiver ativo.
    bool HasEnded();

    // Detecta playback real pelo evento "playback-restart" que o mpv
    // manda pelo socket IPC assim que o primeiro quadro de verdade do
    // arquivo atual está saindo — e mapeia a janela de vídeo nesse
    // instante. `false` até lá. Chamar todo frame enquanto o slide de
    // vídeo estiver ativo e ainda não confirmado.
    bool IsVideoActuallyPlaying();

private:
    Display *display_ = nullptr;
    Window videoWindow_ = 0;
    bool windowMapped_ = false;
    pid_t mpvPid_ = -1;

    int ipcSocketFd_ = -1;
    std::string ipcSocketPath_;
    std::string ipcReadBuffer_;
    std::string pendingCommand_; // enviado assim que o socket conectar

    // Estado do arquivo pedido pelo último Play().
    bool active_ = false;
    bool videoConfirmedPlaying_ = false;
    bool ended_ = false;
    bool endReported_ = false;
    long requestCounter_ = 0;
    long loadRequestId_ = -1;
    long currentEntryId_ = -1;
    long lastStartedEntryId_ = -1;
    bool currentStarted_ = false;

    // Valida a URL da campanha (trata "TODO"/vazio) e a repassa como
    // está — o mpv que resolve internamente se não for um arquivo direto.
    std::string ResolveStreamUrl(const Campaign &campaign) const;

    bool EnsureProcess();
    void KillProcess();
    void HideWindow();
    // Conecta no socket (se preciso), envia o comando pendente e trata
    // as linhas JSON recebidas. Também detecta o mpv morrendo.
    void Pump();
    void HandleIpcLine(const std::string &line);
    void CloseIpcSocket();
};

} // namespace kiosk

#endif // TVBOX_PLAYER_H
