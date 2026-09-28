#include "player.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "video_config.h"

namespace kiosk {

namespace {

// Caminho único por chamada de Play() (mesmo dentro do mesmo processo,
// pra não colidir com o socket de uma reprodução anterior que ainda
// esteja sendo limpa). O mpv cria esse arquivo sozinho ao subir; nós só
// escolhemos o nome.
std::string MakeIpcSocketPath() {
    static int counter = 0;
    return "/tmp/tvbox_mpv_" + std::to_string(static_cast<long>(getpid())) + "_" +
           std::to_string(counter++) + ".sock";
}

} // namespace

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    Stop();
    if (videoWindow_ && display_) {
        XDestroyWindow(display_, videoWindow_);
    }
    if (display_) {
        XCloseDisplay(display_);
    }
}

bool VideoPlayer::Init(unsigned long parentWindowId, int x, int y, int width, int height) {
    if (parentWindowId == 0) return false;

    display_ = XOpenDisplay(nullptr);
    if (!display_) return false;

    int screen = DefaultScreen(display_);
    Window parent = static_cast<Window>(parentWindowId);

    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(display_, screen);
    // Sem override_redirect: esta janela é FILHA de verdade da janela do
    // Raylib (não uma segunda top-level). Isso é o que faz a posição
    // ser sempre relativa ao pai e evita o problema de mutter tratando
    // uma segunda janela top-level como independente — ver player.h.
    videoWindow_ = XCreateWindow(display_, parent, x, y, width, height, 0, CopyFromParent,
                                  InputOutput, CopyFromParent, CWBackPixel, &attrs);
    // XSync (não XFlush): precisamos que o servidor X já tenha
    // processado o CreateWindow antes de devolver o controle — Play()
    // pode rodar logo em seguida (ex.: primeiro slide já é vídeo) e
    // spawna o mpv, que abre sua PRÓPRIA conexão X11 e tenta anexar
    // nessa janela por ID. XFlush só garante que o pedido foi enviado,
    // não que o servidor já aplicou; sem esse XSync existe uma corrida
    // real onde o mpv tenta usar uma janela que o servidor ainda não
    // terminou de criar e falha rápido (confirmado: reproduzido de
    // verdade quando o primeiro slide é vídeo logo na inicialização).
    XSync(display_, False);
    // A janela começa desmapeada (invisível): só aparece quando o mpv
    // confirma via IPC que já está de fato tocando (ver
    // IsVideoActuallyPlaying) — enquanto isso, quem chama desenha uma
    // animação de carregamento por cima (ver include/ui.h).
    return true;
}

void VideoPlayer::SetGeometry(int x, int y, int width, int height) {
    if (!display_ || !videoWindow_) return;
    XMoveResizeWindow(display_, videoWindow_, x, y, width, height);
    XFlush(display_);
}

std::string VideoPlayer::ResolveStreamUrl(const Campaign &campaign) const {
    // "TODO"/vazio: placeholder ainda não preenchido em config/campaigns.conf.
    if (campaign.video_url.empty() || campaign.video_url == "TODO") return "";

    // upload/direto: URL de arquivo já reproduzível, passa direto.
    // youtube/instagram/facebook: também passamos a URL ORIGINAL direto
    // pro mpv, sem chamar yt-dlp nós mesmos. Motivo (descoberto testando
    // com um vídeo real em 2026-09-26): `yt-dlp -g` sozinho, sem uma
    // stream progressiva disponível (comum hoje em dia no YouTube),
    // imprime DUAS URLs em linhas separadas (vídeo e áudio sem mux) — um
    // `execlp` com uma string só quebraria nesse caso. O mpv já vem com
    // um hook Lua (`ytdl_hook`) que chama o yt-dlp sozinho e sabe tocar
    // vídeo+áudio separados sem precisar de mux/ffmpeg. Deixar o mpv
    // fazer isso é mais simples E mais robusto do que replicar a lógica
    // aqui. Ver docs/memory/known-issues.md item 5.
    return campaign.video_url;
}

bool VideoPlayer::Play(const Campaign &campaign) {
    Stop();
    if (!display_ || !videoWindow_) return false;

    std::string streamUrl = ResolveStreamUrl(campaign);
    if (streamUrl.empty()) return false;

    videoConfirmedPlaying_ = false;
    ipcSocketPath_ = MakeIpcSocketPath();
    // A janela FICA ESCONDIDA aqui — só é mapeada quando
    // IsVideoActuallyPlaying() confirmar que o mpv já está decodificando
    // de verdade (evita um quadro preto do mpv aparecer por cima da
    // animação de carregamento enquanto ele resolve/bufferiza).

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }
        std::string wid = "--wid=" + std::to_string(static_cast<unsigned long>(videoWindow_));
        std::string ytdlFormat = std::string("--ytdl-format=") + kYtdlFormatSelector;
        std::string ipcOpt = "--input-ipc-server=" + ipcSocketPath_;
        // ytdl_hook do mpv por padrão procura o binário "youtube-dl"; no
        // Armbian normalmente só existe "yt-dlp" no PATH, então apontamos
        // explicitamente (opção documentada do próprio ytdl_hook.lua).
        const char *ytdlPathOpt = "--script-opts=ytdl_hook-ytdl_path=yt-dlp";
        // --gpu-context=x11egl: sem isso, o mpv sempre que encontra um
        // compositor Wayland alcançável (ex.: XWayland) cria sua PRÓPRIA
        // superfície Wayland nativa, ignorando --wid por completo —
        // mesmo com --wid apontando pra uma janela X11 válida e mesmo
        // sendo uma janela filha de verdade da nossa (testado e
        // confirmado ao vivo em 2026-09-27: sem isso o vídeo aparecia
        // numa janela/aba própria, flutuando, em vez de dentro da área
        // reservada — ver docs/memory/known-issues.md item 5). Forçar o
        // contexto X11/EGL garante que o mpv sempre respeite --wid,
        // independente de haver ou não um compositor Wayland por perto
        // (no dispositivo alvo, Xorg puro sem Wayland, isso nem seria um
        // problema — mas forçar explicitamente é mais robusto do que
        // depender de auto-detecção). Bônus: decode por hardware sem
        // cópia extra (`vaapi` em vez de `vaapi-copy`) nesta sandbox.
        const char *gpuContextOpt = "--gpu-context=x11egl";
        execlp("mpv", "mpv", wid.c_str(), gpuContextOpt, ipcOpt.c_str(), "--loop-file=inf",
               "--mute=yes", "--no-osc", "--no-input-default-bindings", "--really-quiet",
               "--hwdec=auto", "--ytdl=yes", ytdlPathOpt, ytdlFormat.c_str(), streamUrl.c_str(),
               static_cast<char *>(nullptr));
        _exit(127);
    }

    mpvPid_ = pid;
    return true;
}

void VideoPlayer::CloseIpcSocket() {
    if (ipcSocketFd_ >= 0) {
        close(ipcSocketFd_);
        ipcSocketFd_ = -1;
    }
    if (!ipcSocketPath_.empty()) {
        unlink(ipcSocketPath_.c_str());
    }
    ipcReadBuffer_.clear();
}

bool VideoPlayer::IsVideoActuallyPlaying() {
    if (videoConfirmedPlaying_) return true;
    if (mpvPid_ <= 0) return false;

    if (ipcSocketFd_ < 0) {
        ipcSocketFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipcSocketFd_ < 0) return false;
        int flags = fcntl(ipcSocketFd_, F_GETFL, 0);
        fcntl(ipcSocketFd_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, ipcSocketPath_.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(ipcSocketFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 &&
            errno != EINPROGRESS) {
            // Normal logo após o fork: o mpv ainda não criou o arquivo
            // do socket. Tenta de novo no próximo frame.
            close(ipcSocketFd_);
            ipcSocketFd_ = -1;
            return false;
        }
    }

    // Detecta playback real esperando o evento "playback-restart" que o
    // próprio mpv manda sozinho (sem precisarmos pedir nada) pra
    // qualquer cliente IPC conectado, documentado como o sinal correto
    // de "o primeiro frame de vídeo/áudio de verdade já foi
    // (re)configurado e está saindo" — é a mesma técnica usada por
    // bibliotecas cliente do mpv (ex.: python-mpv `wait_for_playback`).
    //
    // ANTES disto aqui checávamos a propriedade "time-pos" (não-nula =
    // "tocando"). Trocado depois que o usuário relatou o vídeo ficando
    // preto pra sempre SÓ no RK3229 real, exatamente depois desta
    // confirmação-antes-de-mapear ter sido adicionada (antes, a janela
    // era mapeada sem esperar nada, então sempre "funcionava" ainda que
    // só mostrando preto por alguns segundos). Causa mais provável:
    // "time-pos" reflete o relógio interno de playback e pode começar a
    // avançar antes do primeiro frame decodificado ter sido de fato
    // composto na janela X11 — folga pequena/imperceptível num decode
    // rápido (VAAPI/x86 desta sandbox), mas potencialmente grande num
    // decode por hardware mais lento ou com pipeline diferente
    // (`rkmpp` no Mali-400 do RK3229) — mapeando a janela antes dela
    // ter algo de verdade pra mostrar. "playback-restart" é o sinal que
    // o próprio mpv considera definitivo, sem essa ambiguidade,
    // confirmado ao vivo (`socat` bruto no socket) disparando só depois
    // de "video-reconfig" já ter acontecido.
    char buf[512];
    for (;;) {
        ssize_t n = read(ipcSocketFd_, buf, sizeof(buf));
        if (n <= 0) break;
        ipcReadBuffer_.append(buf, static_cast<size_t>(n));
        if (static_cast<size_t>(n) < sizeof(buf)) break;
    }

    size_t searchFrom = 0;
    for (;;) {
        size_t newlinePos = ipcReadBuffer_.find('\n', searchFrom);
        if (newlinePos == std::string::npos) break;
        std::string line = ipcReadBuffer_.substr(searchFrom, newlinePos - searchFrom);
        searchFrom = newlinePos + 1;

        if (line.find("\"event\":\"playback-restart\"") != std::string::npos) {
            videoConfirmedPlaying_ = true;
        }
    }
    ipcReadBuffer_.erase(0, searchFrom);

    if (videoConfirmedPlaying_) {
        XMapRaised(display_, videoWindow_);
        XFlush(display_);
    }
    return videoConfirmedPlaying_;
}

void VideoPlayer::Stop() {
    if (mpvPid_ > 0) {
        kill(mpvPid_, SIGTERM);
        waitpid(mpvPid_, nullptr, 0);
        mpvPid_ = -1;
    }
    CloseIpcSocket();
    videoConfirmedPlaying_ = false;
    if (display_ && videoWindow_) {
        XUnmapWindow(display_, videoWindow_);
        XFlush(display_);
    }
}

bool VideoPlayer::IsPlaying() const {
    if (mpvPid_ <= 0) return false;
    int status = 0;
    pid_t r = waitpid(mpvPid_, &status, WNOHANG);
    return r == 0; // 0 = ainda rodando
}

} // namespace kiosk
