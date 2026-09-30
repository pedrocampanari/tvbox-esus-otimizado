#include "remote_control.h"

#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <sstream>
#include <string>
#include <unistd.h>

#include "procexec.h"

namespace kiosk {

namespace {

// Nome do driver do receptor IR embutido no hardware, confirmado via
// `dmesg`/`ir-keytable` num RK3229 real (ver
// docs/memory/known-issues.md item -6). Usado pra achar o
// /dev/input/eventN certo em vez de assumir um numero fixo, que muda
// conforme a ordem de enumeracao dos dispositivos a cada boot.
constexpr const char *kRemoteDriverName = "gpio_ir_recv";

// Varre /proc/bus/input/devices procurando o bloco cujo "N: Name="
// contem kRemoteDriverName, e devolve o /dev/input/eventN do
// "H: Handlers=" desse mesmo bloco. String vazia se nao encontrar (ex.:
// hardware sem esse receptor, ou driver com outro nome).
std::string FindRemoteEventDevice() {
    std::ifstream devices("/proc/bus/input/devices");
    if (!devices.is_open()) return "";

    std::string line;
    bool inTargetBlock = false;
    while (std::getline(devices, line)) {
        if (line.empty()) {
            inTargetBlock = false;
            continue;
        }
        if (line.rfind("N: Name=", 0) == 0) {
            inTargetBlock = line.find(kRemoteDriverName) != std::string::npos;
            continue;
        }
        if (inTargetBlock && line.rfind("H: Handlers=", 0) == 0) {
            std::istringstream tokens(line);
            std::string token;
            while (tokens >> token) {
                if (token.rfind("event", 0) == 0) return "/dev/input/" + token;
            }
        }
    }
    return "";
}

// Nome do mixer simples a usar, ou vazio se nao houver nenhum.
// Prefere "Master" (o controle softvol que install.sh cria em
// /etc/alsa/conf.d — o HDMI do RK3229 nao tem volume em hardware, ver
// docs/memory/known-issues.md item -8), depois "PCM", depois o
// primeiro que aparecer. Resultado guardado depois da primeira
// resposta nao vazia: evita um fork de `amixer scontrols` a cada toque.
// O softvol so passa a existir depois que algum processo abriu o PCM
// padrao uma vez — por isso uma resposta vazia nao e guardada.
std::string MixerControlName() {
    static std::string cached;
    if (!cached.empty()) return cached;

    std::string out;
    if (!RunCaptureStdout({"amixer", "scontrols"}, 3, out)) return "";
    std::string first;
    std::string pcm;
    size_t pos = 0;
    while ((pos = out.find('\'', pos)) != std::string::npos) {
        auto end = out.find('\'', pos + 1);
        if (end == std::string::npos) break;
        std::string name = out.substr(pos + 1, end - pos - 1);
        pos = out.find('\n', end);
        if (name == "Master") {
            cached = name;
            return cached;
        }
        if (name == "PCM" && pcm.empty()) pcm = name;
        if (first.empty()) first = name;
        if (pos == std::string::npos) break;
    }
    cached = !pcm.empty() ? pcm : first;
    return cached;
}

// Extrai o primeiro "[NN%]" (volume) e o primeiro "[on]"/"[off]" (estado
// de mudo) da saida do `amixer get`.
bool ParseAmixerGet(const std::string &out, int &percent, bool &muted, bool &hasSwitch) {
    bool foundPercent = false;
    bool foundState = false;
    size_t pos = 0;
    while ((pos = out.find('[', pos)) != std::string::npos) {
        auto end = out.find(']', pos);
        if (end == std::string::npos) break;
        std::string token = out.substr(pos + 1, end - pos - 1);
        if (!foundPercent && !token.empty() && token.back() == '%') {
            percent = std::atoi(token.c_str());
            foundPercent = true;
        } else if (!foundState && (token == "on" || token == "off")) {
            hasSwitch = true;
            muted = (token == "off");
            foundState = true;
        }
        pos = end + 1;
        if (foundPercent && foundState) break;
    }
    return foundPercent;
}

} // namespace

RemoteControl::RemoteControl() {
    std::string path = FindRemoteEventDevice();
    if (path.empty()) return;
    fd_ = open(path.c_str(), O_RDONLY | O_NONBLOCK);
}

RemoteControl::~RemoteControl() {
    if (fd_ >= 0) close(fd_);
}

RemoteButton RemoteControl::PollButtonPress() {
    if (fd_ < 0) return RemoteButton::kNone;

    RemoteButton result = RemoteButton::kNone;
    struct input_event ev;
    // Drena todos os eventos pendentes neste frame, nao so o primeiro —
    // evita acumular atraso se o loop principal ficar momentaneamente
    // ocupado (ex.: spawn do mpv). Se vieram varios num unico frame, o
    // ultimo botao pressionado "vence" (efeito esperado pelo usuario).
    while (read(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type != EV_KEY || ev.value == 0) continue; // ignora soltura
        // value 2 = repeticao automatica (botao segurado): vale pra VOL+/
        // VOL-, que sobem/descem continuamente. MUTE so no toque inicial,
        // senao alternaria sem parar enquanto segurado.
        switch (ev.code) {
            case KEY_VOLUMEUP:   result = RemoteButton::kVolumeUp;   break;
            case KEY_VOLUMEDOWN: result = RemoteButton::kVolumeDown; break;
            case KEY_MUTE:
                if (ev.value == 1) result = RemoteButton::kMute;
                break;
            default: break;
        }
    }
    return result;
}

// Volume salvo antes de um MUTE emulado (mixer sem chave on/off, caso
// do softvol). -1 = nao esta mudo por emulacao.
static int g_volumeBeforeSoftMute = -1;

static bool GetVolume(const std::string &control, int &outPercent, bool &outMuted, bool &outHasSwitch) {
    std::string out;
    if (!RunCaptureStdout({"amixer", "get", control}, 3, out)) return false;
    outMuted = false;
    outHasSwitch = false;
    if (!ParseAmixerGet(out, outPercent, outMuted, outHasSwitch)) return false;
    if (!outHasSwitch) outMuted = outPercent == 0;
    return true;
}

bool QueryVolumeState(int &outPercent, bool &outMuted) {
    std::string control = MixerControlName();
    if (control.empty()) return false;
    bool hasSwitch = false;
    return GetVolume(control, outPercent, outMuted, hasSwitch);
}

bool AdjustVolume(int deltaPercent, bool toggleMute, int &outPercent, bool &outMuted) {
    std::string control = MixerControlName();
    if (control.empty()) return false;

    int current = 0;
    bool muted = false;
    bool hasSwitch = false;
    if (!GetVolume(control, current, muted, hasSwitch)) return false;

    std::string ignored;
    if (toggleMute && hasSwitch) {
        RunCaptureStdout({"amixer", "-q", "sset", control, "toggle"}, 3, ignored);
    } else if (toggleMute) {
        // Sem chave de mudo (softvol): MUTE = volume 0, e o proximo MUTE
        // devolve o volume de antes.
        int target = 0;
        if (current == 0) {
            target = g_volumeBeforeSoftMute > 0 ? g_volumeBeforeSoftMute : 50;
            g_volumeBeforeSoftMute = -1;
        } else {
            g_volumeBeforeSoftMute = current;
        }
        RunCaptureStdout({"amixer", "-q", "sset", control, std::to_string(target) + "%"}, 3,
                         ignored);
    } else if (deltaPercent != 0) {
        g_volumeBeforeSoftMute = -1;
        std::string step =
            std::to_string(std::abs(deltaPercent)) + "%" + (deltaPercent > 0 ? "+" : "-");
        RunCaptureStdout({"amixer", "-q", "sset", control, step}, 3, ignored);
    }

    if (!GetVolume(control, outPercent, outMuted, hasSwitch)) return false;
    // Persiste o volume na hora: o kiosk costuma ser desligado tirando da
    // tomada, sem passar pelo `alsactl store` do shutdown — exec.sh
    // restaura este estado a cada início.
    RunCaptureStdout({"alsactl", "store"}, 3, ignored);
    return true;
}

} // namespace kiosk
