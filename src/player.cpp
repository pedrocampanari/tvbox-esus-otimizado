#include "player.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "video_config.h"

namespace kiosk {

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

bool VideoPlayer::Init(int screenX, int screenY, int width, int height) {
    display_ = XOpenDisplay(nullptr);
    if (!display_) return false;

    int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);

    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(display_, screen);
    attrs.override_redirect = True; // sem gerenciador de janelas no kiosk

    videoWindow_ = XCreateWindow(display_, root, screenX, screenY, width, height, 0,
                                  CopyFromParent, InputOutput, CopyFromParent,
                                  CWBackPixel | CWOverrideRedirect, &attrs);
    XFlush(display_);
    // A janela começa desmapeada (invisível): só aparece quando um slide
    // de vídeo está ativo (ver Play/Stop).
    return true;
}

void VideoPlayer::SetGeometry(int screenX, int screenY, int width, int height) {
    if (!display_ || !videoWindow_) return;
    XMoveResizeWindow(display_, videoWindow_, screenX, screenY, width, height);
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

    XMapRaised(display_, videoWindow_);
    XFlush(display_);

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
        // ytdl_hook do mpv por padrão procura o binário "youtube-dl"; no
        // Armbian normalmente só existe "yt-dlp" no PATH, então apontamos
        // explicitamente (opção documentada do próprio ytdl_hook.lua).
        const char *ytdlPathOpt = "--script-opts=ytdl_hook-ytdl_path=yt-dlp";
        execlp("mpv", "mpv", wid.c_str(), "--loop-file=inf", "--mute=yes", "--no-osc",
               "--no-input-default-bindings", "--really-quiet", "--hwdec=auto", "--ytdl=yes",
               ytdlPathOpt, ytdlFormat.c_str(), streamUrl.c_str(),
               static_cast<char *>(nullptr));
        _exit(127);
    }

    mpvPid_ = pid;
    return true;
}

void VideoPlayer::Stop() {
    if (mpvPid_ > 0) {
        kill(mpvPid_, SIGTERM);
        waitpid(mpvPid_, nullptr, 0);
        mpvPid_ = -1;
    }
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
