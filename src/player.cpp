#include "player.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include <cstdio>

#include "procexec.h"

namespace kiosk {

namespace {

// Remove espaços/quebras de linha no fim da saída de um processo
// (yt-dlp -g imprime a URL seguida de \n).
std::string TrimTrailingWhitespace(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

bool IsDirectPlayable(VideoOrigin origem) {
    return origem == VideoOrigin::Upload || origem == VideoOrigin::Direto;
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
    if (IsDirectPlayable(campaign.video_origem)) {
        return campaign.video_url;
    }

    // youtube / instagram / facebook: resolve pra URL de stream direta
    // via yt-dlp, pra nunca precisar do player/iframe oficial da
    // plataforma.
    std::string streamUrl;
    bool ok = RunCaptureStdout(
        {"yt-dlp", "-g", "-f", "best[ext=mp4]/best", "--no-warnings",
         campaign.video_url},
        20, streamUrl);
    if (!ok || streamUrl.empty()) return "";
    return TrimTrailingWhitespace(streamUrl);
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
        std::string wid = std::to_string(static_cast<unsigned long>(videoWindow_));
        execlp("mpv", "mpv", ("--wid=" + wid).c_str(), "--loop-file=inf",
               "--mute=yes", "--no-osc", "--no-input-default-bindings",
               "--really-quiet", "--hwdec=auto", streamUrl.c_str(),
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
