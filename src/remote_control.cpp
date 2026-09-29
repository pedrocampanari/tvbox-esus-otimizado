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

// Nome do primeiro mixer simples disponivel (ex.: "Master", "PCM"), ou
// vazio se nao houver nenhum.
std::string FirstMixerControlName() {
    std::string out;
    if (!RunCaptureStdout({"amixer", "scontrols"}, 3, out)) return "";
    auto firstQuote = out.find('\'');
    if (firstQuote == std::string::npos) return "";
    auto secondQuote = out.find('\'', firstQuote + 1);
    if (secondQuote == std::string::npos) return "";
    return out.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

// Extrai o primeiro "[NN%]" (volume) e o primeiro "[on]"/"[off]" (estado
// de mudo) da saida do `amixer get`.
bool ParseAmixerGet(const std::string &out, int &percent, bool &muted) {
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
        if (ev.type != EV_KEY || ev.value != 1) continue; // so toque inicial, nao repeticao/solta
        switch (ev.code) {
            case KEY_VOLUMEUP:   result = RemoteButton::kVolumeUp;   break;
            case KEY_VOLUMEDOWN: result = RemoteButton::kVolumeDown; break;
            case KEY_MUTE:       result = RemoteButton::kMute;       break;
            default: break;
        }
    }
    return result;
}

bool QueryVolumeState(int &outPercent, bool &outMuted) {
    std::string control = FirstMixerControlName();
    if (control.empty()) return false;
    std::string out;
    if (!RunCaptureStdout({"amixer", "get", control}, 3, out)) return false;
    return ParseAmixerGet(out, outPercent, outMuted);
}

bool AdjustVolume(int deltaPercent, bool toggleMute, int &outPercent, bool &outMuted) {
    std::string control = FirstMixerControlName();
    if (control.empty()) return false;

    std::string ignored;
    if (toggleMute) {
        RunCaptureStdout({"amixer", "-q", "sset", control, "toggle"}, 3, ignored);
    } else if (deltaPercent != 0) {
        std::string step =
            std::to_string(std::abs(deltaPercent)) + "%" + (deltaPercent > 0 ? "+" : "-");
        RunCaptureStdout({"amixer", "-q", "sset", control, step}, 3, ignored);
    }

    std::string getOut;
    if (!RunCaptureStdout({"amixer", "get", control}, 3, getOut)) return false;
    return ParseAmixerGet(getOut, outPercent, outMuted);
}

} // namespace kiosk
