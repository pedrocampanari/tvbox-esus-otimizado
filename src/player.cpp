#include "player.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "video_config.h"

namespace kiosk {

namespace {

// Caminho único por processo mpv lançado (se o mpv cair e for relançado,
// não colide com o socket do anterior). O mpv cria esse arquivo sozinho
// ao subir; nós só escolhemos o nome.
std::string MakeIpcSocketPath() {
    static int counter = 0;
    return "/tmp/tvbox_mpv_" + std::to_string(static_cast<long>(getpid())) + "_" +
           std::to_string(counter++) + ".sock";
}

// Escapa uma string pra dentro de um literal JSON (caminho/URL do
// comando `loadfile`).
std::string JsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Valor inteiro depois de `"chave":` numa linha JSON do mpv, ou -1.
long JsonIntField(const std::string &line, const char *key) {
    std::string needle = std::string("\"") + key + "\":";
    auto pos = line.find(needle);
    if (pos == std::string::npos) return -1;
    return std::strtol(line.c_str() + pos + needle.size(), nullptr, 10);
}

// Valor string depois de `"chave":"`, ou vazio.
std::string JsonStringField(const std::string &line, const char *key) {
    std::string needle = std::string("\"") + key + "\":\"";
    auto pos = line.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    auto end = line.find('"', pos);
    return end == std::string::npos ? "" : line.substr(pos, end - pos);
}

} // namespace

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() { Shutdown(); }

void VideoPlayer::Shutdown() {
    Stop();
    KillProcess();
    if (videoWindow_ && display_) {
        XDestroyWindow(display_, videoWindow_);
        videoWindow_ = 0;
    }
    if (display_) {
        XCloseDisplay(display_);
        display_ = nullptr;
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

bool VideoPlayer::EnsureProcess() {
    if (mpvPid_ > 0) {
        if (waitpid(mpvPid_, nullptr, WNOHANG) == 0) return true; // vivo
        mpvPid_ = -1; // morreu (crash): relança abaixo
        CloseIpcSocket();
    }

    ipcSocketPath_ = MakeIpcSocketPath();
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Se o app morrer (crash, SIGKILL), o kernel mata o mpv junto —
        // sem isto ele ficava órfão decodificando em segundo plano (100%
        // de um núcleo no RK3229) e o exec.sh subia outro por cima a cada
        // reinício (confirmado testando em 2026-09-29).
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() == 1) _exit(1); // pai já morreu antes do prctl
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
        // compositor Wayland alcançável cria sua PRÓPRIA superfície
        // Wayland, ignorando --wid por completo (ver
        // docs/memory/known-issues.md item 5).
        const char *gpuContextOpt = "--gpu-context=x11egl";
        // UM mpv persistente pra todos os vídeos (--idle + --force-window):
        // no RK3229 só criar o contexto EGL (Mesa/lima) leva ~3s, e um mpv
        // novo por vídeo deixava ~11s de "carregando" entre vídeos mesmo
        // com arquivo local (medido em 2026-09-30). --force-window mantém
        // o VO/contexto vivo enquanto o mpv está ocioso entre vídeos.
        // --keep-open=no + --loop-file=no: no fim do arquivo o mpv volta
        // a ficar ocioso e avisa com `end-file` (reason "eof") — o slide
        // de vídeo dura o vídeo inteiro (ver HasEnded()).
        // --no-audio: o vídeo sempre foi mudo (igual ao `mute=1` do site);
        // sem trilha de áudio o mpv nem decodifica/abre o ALSA.
        execlp("mpv", "mpv", wid.c_str(), gpuContextOpt, ipcOpt.c_str(), "--idle=yes",
               "--force-window=yes", "--keep-open=no", "--loop-file=no", "--no-audio",
               "--no-osc", "--no-input-default-bindings", "--really-quiet", "--hwdec=auto",
               "--ytdl=yes", ytdlPathOpt, ytdlFormat.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    mpvPid_ = pid;
    return true;
}

void VideoPlayer::KillProcess() {
    if (mpvPid_ > 0) {
        // SIGTERM e espera no máximo ~1s: um mpv travado (driver de vídeo,
        // rede) não pode congelar o loop de desenho do kiosk.
        kill(mpvPid_, SIGTERM);
        bool reaped = false;
        for (int i = 0; i < 20 && !reaped; ++i) {
            reaped = waitpid(mpvPid_, nullptr, WNOHANG) != 0;
            if (!reaped) usleep(50 * 1000);
        }
        if (!reaped) {
            kill(mpvPid_, SIGKILL);
            waitpid(mpvPid_, nullptr, 0);
        }
        mpvPid_ = -1;
    }
    CloseIpcSocket();
}

bool VideoPlayer::Play(const Campaign &campaign, const std::string &localFile,
                       double playEndSeconds) {
    if (!display_ || !videoWindow_) return false;

    // Arquivo do cache local (include/video_cache.h) tem prioridade:
    // sem rede, sem yt-dlp, sem buffering. Sem ele, streaming direto.
    std::string source = localFile.empty() ? ResolveStreamUrl(campaign) : localFile;
    if (source.empty()) return false;
    if (!EnsureProcess()) return false;

    // A janela fica ESCONDIDA até o `playback-restart` deste arquivo:
    // nada de quadro preto/antigo do mpv por cima da animação de
    // carregamento (desenhada pelo Raylib).
    HideWindow();
    active_ = true;
    videoConfirmedPlaying_ = false;
    ended_ = false;
    endReported_ = false;
    currentEntryId_ = -1;
    currentStarted_ = false;
    loadRequestId_ = ++requestCounter_;
    // `end` é lido quando o arquivo começa: setar antes do loadfile vale
    // só pra este arquivo (e "none" desfaz o corte do anterior). Opção
    // global em vez de opção por-arquivo do loadfile porque a sintaxe
    // desta mudou entre versões do mpv (0.38 ganhou o argumento de índice).
    std::string end = (localFile.empty() || playEndSeconds <= 0) ? "none"
                                                                  : std::to_string(playEndSeconds);
    pendingCommand_ = "{\"command\":[\"set_property\",\"end\",\"" + end + "\"]}\n";
    pendingCommand_ += "{\"command\":[\"loadfile\",\"" + JsonEscape(source) +
                       "\",\"replace\"],\"request_id\":" + std::to_string(loadRequestId_) +
                       "}\n";
    Pump();
    return true;
}

void VideoPlayer::Stop() {
    HideWindow();
    if (active_) {
        // Mantém o processo (e o contexto EGL) vivo: só para o arquivo.
        pendingCommand_ = "{\"command\":[\"stop\"]}\n";
        Pump();
    }
    active_ = false;
    videoConfirmedPlaying_ = false;
    ended_ = false;
    endReported_ = false;
    currentEntryId_ = -1;
    currentStarted_ = false;
}

void VideoPlayer::HideWindow() {
    if (display_ && videoWindow_ && windowMapped_) {
        XUnmapWindow(display_, videoWindow_);
        XFlush(display_);
    }
    windowMapped_ = false;
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

void VideoPlayer::Pump() {
    if (mpvPid_ > 0 && waitpid(mpvPid_, nullptr, WNOHANG) != 0) {
        // mpv morreu (crash/sinal): o vídeo atual acabou com falha; o
        // próximo Play() relança o processo.
        mpvPid_ = -1;
        CloseIpcSocket();
        pendingCommand_.clear();
        if (active_) ended_ = true;
        HideWindow();
        return;
    }
    if (mpvPid_ <= 0) return;

    if (ipcSocketFd_ < 0) {
        ipcSocketFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipcSocketFd_ < 0) return;
        int flags = fcntl(ipcSocketFd_, F_GETFL, 0);
        fcntl(ipcSocketFd_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, ipcSocketPath_.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(ipcSocketFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            // Normal logo após o fork: o mpv ainda não criou o socket.
            // Tenta de novo no próximo frame (o comando fica pendente).
            close(ipcSocketFd_);
            ipcSocketFd_ = -1;
            return;
        }
    }

    if (!pendingCommand_.empty()) {
        ssize_t n = write(ipcSocketFd_, pendingCommand_.data(), pendingCommand_.size());
        if (n == static_cast<ssize_t>(pendingCommand_.size())) pendingCommand_.clear();
    }

    char buf[1024];
    for (;;) {
        ssize_t n = read(ipcSocketFd_, buf, sizeof(buf));
        if (n <= 0) break;
        ipcReadBuffer_.append(buf, static_cast<size_t>(n));
    }

    size_t searchFrom = 0;
    for (;;) {
        size_t newlinePos = ipcReadBuffer_.find('\n', searchFrom);
        if (newlinePos == std::string::npos) break;
        HandleIpcLine(ipcReadBuffer_.substr(searchFrom, newlinePos - searchFrom));
        searchFrom = newlinePos + 1;
    }
    ipcReadBuffer_.erase(0, searchFrom);
}

void VideoPlayer::HandleIpcLine(const std::string &line) {
    if (!active_) return;

    // Resposta do nosso `loadfile`: id da entrada da playlist que este
    // Play() criou. Eventos de outras entradas (o vídeo anterior sendo
    // interrompido, por exemplo) são ignorados por esse id.
    if (JsonIntField(line, "request_id") == loadRequestId_) {
        currentEntryId_ = JsonIntField(line, "playlist_entry_id");
        if (currentEntryId_ >= 0 && currentEntryId_ == lastStartedEntryId_) currentStarted_ = true;
        if (currentEntryId_ < 0) ended_ = true; // loadfile recusado
        return;
    }

    std::string event = JsonStringField(line, "event");
    if (event == "start-file") {
        lastStartedEntryId_ = JsonIntField(line, "playlist_entry_id");
        if (lastStartedEntryId_ == currentEntryId_) currentStarted_ = true;
    } else if (event == "playback-restart") {
        // "Primeiro quadro de verdade saindo" — sinal que o próprio mpv
        // documenta como definitivo (não `time-pos`, que podia avançar
        // antes do quadro estar na tela no RK3229: ver known-issues -5).
        if (currentStarted_ && !ended_ && !videoConfirmedPlaying_) {
            videoConfirmedPlaying_ = true;
            XMapRaised(display_, videoWindow_);
            XFlush(display_);
            windowMapped_ = true;
        }
    } else if (event == "end-file") {
        if (JsonIntField(line, "playlist_entry_id") != currentEntryId_) return;
        std::string reason = JsonStringField(line, "reason");
        if (reason == "eof" || reason == "error") {
            // Esconde NA HORA: antes a janela (fundo preto) ficava na tela
            // entre o fim do vídeo e a troca de slide — a "tela preta ao
            // final" relatada em 2026-09-30.
            ended_ = true;
            HideWindow();
        }
    }
}

bool VideoPlayer::IsVideoActuallyPlaying() {
    Pump();
    return videoConfirmedPlaying_;
}

bool VideoPlayer::HasEnded() {
    Pump();
    if (!ended_ || endReported_) return false;
    endReported_ = true;
    return true;
}

} // namespace kiosk
