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
    // A janela começa desmapeada (invisível): só aparece quando um slide
    // de vídeo está ativo (ver Play/Stop).
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

    XMapRaised(display_, videoWindow_);
    // XSync pelo mesmo motivo do Init(): garantir que o servidor já
    // mapeou a janela antes do mpv (processo à parte, conexão X11
    // própria) tentar anexar nela via --wid.
    XSync(display_, False);

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
        execlp("mpv", "mpv", wid.c_str(), gpuContextOpt, "--loop-file=inf", "--mute=yes",
               "--no-osc", "--no-input-default-bindings", "--really-quiet", "--hwdec=auto",
               "--ytdl=yes", ytdlPathOpt, ytdlFormat.c_str(), streamUrl.c_str(),
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
