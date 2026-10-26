#include "setup_menu.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "raylib.h"

#include "procexec.h"

namespace kiosk {

namespace {

constexpr double kStatusRefreshSeconds = 5.0;
// "•" em UTF-8: senha mascarada no campo de texto.
constexpr const char *kPasswordDot = "\xE2\x80\xA2";

std::vector<std::string> SplitTabs(const std::string &line) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(line);
    while (std::getline(in, part, '\t')) parts.push_back(part);
    return parts;
}

std::vector<std::string> Lines(const std::string &text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Mensagem de erro do net-ctl.py (stderr junto do stdout): a ultima
// linha que nao e um registro TAB do protocolo.
std::string ErrorText(const std::string &output, const char *fallback) {
    std::string found;
    for (const auto &line : Lines(output)) {
        if (line.find('\t') == std::string::npos) found = line;
    }
    return found.empty() ? fallback : found;
}

size_t Utf8Length(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

void PopUtf8(std::string &s) {
    while (!s.empty()) {
        unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

const char *SignalText(int dbm) {
    if (dbm >= -60) return "sinal forte";
    if (dbm >= -72) return "sinal médio";
    return "sinal fraco";
}

// Teclado USB -> mesmos botoes do controle (so navegacao).
RemoteButton KeyboardButton() {
    auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (pressed(KEY_UP)) return RemoteButton::kUp;
    if (pressed(KEY_DOWN)) return RemoteButton::kDown;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) return RemoteButton::kOk;
    if (IsKeyPressed(KEY_ESCAPE)) return RemoteButton::kBack;
    return RemoteButton::kNone;
}

} // namespace

bool LoadBtvConfig(const std::string &path, BtvConfig &out) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    BtvConfig config;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "configured") config.configured = value == "1";
        else if (key == "network") config.network = value == "wifi" ? "wifi" : "wired";
        else if (key == "wifi_ssid") config.wifiSsid = value;
        else if (key == "ubs_slug") config.ubsSlug = value;
    }
    out = config;
    return true;
}

bool SaveBtvConfig(const std::string &path, const BtvConfig &config) {
    std::string text = std::string("configured=") + (config.configured ? "1" : "0") + "\n" +
                       "network=" + config.network + "\n" + "wifi_ssid=" + config.wifiSsid + "\n" +
                       "ubs_slug=" + config.ubsSlug + "\n";
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) &&
              fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) return false;
    // fsync do diretorio: o rename tambem precisa chegar na flash.
    auto slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    int dfd = open(dir.c_str(), O_RDONLY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return true;
}

SetupMenu::SetupMenu(std::string configPath, std::string netCtlScript, std::string testUrl)
    : configPath_(std::move(configPath)), netCtl_(std::move(netCtlScript)),
      testUrl_(std::move(testUrl)) {
    LoadBtvConfig(configPath_, config_);
}

SetupMenu::~SetupMenu() {
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
}

void SetupMenu::StartBootNetworkCheck(int seconds) {
    bootWaitSeconds_ = seconds;
    StartJob(Job::kBootWait);
}

void SetupMenu::Open(const std::string &reason, bool isError) {
    if (open_) return;
    open_ = true;
    screen_ = Screen::kMain;
    cursor_ = 0;
    message_ = reason;
    messageIsError_ = isError;
    draftNetwork_ = config_.network;
    draftSsid_ = config_.wifiSsid;
    draftPassword_.clear();
    draftSlug_ = config_.ubsSlug;
    nextStatusRefresh_ = 0.0;
    TraceLog(LOG_WARNING, "Menu: aberto (%s)", reason.empty() ? "HOME" : reason.c_str());
}

void SetupMenu::Close() {
    open_ = false;
    pendingJob_ = Job::kNone;
    draftPassword_.clear();
    textValue_.clear();
    TraceLog(LOG_WARNING, "Menu: fechado");
}

bool SetupMenu::TakeClosedNotice(std::string &title, std::string &subtitle) {
    if (closedTitle_.empty()) return false;
    title = closedTitle_;
    subtitle = closedSubtitle_;
    closedTitle_.clear();
    closedSubtitle_.clear();
    return true;
}

void SetupMenu::StartJob(Job job) {
    if (job_ != Job::kNone) {
        if (job == Job::kScan || job == Job::kApply) pendingJob_ = job;
        return;
    }
    if (worker_.joinable()) worker_.join();
    job_ = job;
    jobDone_.store(false);
    jobOk_ = false;
    jobOutput_.clear();
    jobPart2_.clear();

    std::vector<std::string> first;
    std::vector<std::string> second;
    int firstTimeout = 15;
    switch (job) {
        case Job::kBootWait:
            first = {netCtl_, "wait-online", std::to_string(bootWaitSeconds_)};
            firstTimeout = bootWaitSeconds_ + 5;
            break;
        case Job::kStatus:
            first = {netCtl_, "status"};
            break;
        case Job::kScan:
            first = {netCtl_, "scan"};
            firstTimeout = 45;
            break;
        case Job::kApply:
            // Wi-Fi: grava a rede no netplan antes do teste (senha vazia =
            // a salva). Cabo: o netplan do Armbian ja cuida, so testa.
            if (draftNetwork_ == "wifi") {
                first = {netCtl_, "wifi-connect", draftSsid_};
                if (!draftPassword_.empty()) first.push_back(draftPassword_);
                firstTimeout = 90;
            }
            second = {netCtl_, "test", testUrl_};
            break;
        case Job::kNone:
            return;
    }

    worker_ = std::thread([this, first, second, firstTimeout] {
        bool ok = true;
        if (!first.empty()) {
            ok = RunCaptureStdout(first, firstTimeout, jobOutput_, &cancel_, true);
        }
        if (ok && !second.empty()) {
            ok = RunCaptureStdout(second, 45, jobPart2_, &cancel_, false);
        }
        jobOk_ = ok;
        jobDone_.store(true, std::memory_order_release);
    });
}

void SetupMenu::FinishJob() {
    Job finished = job_;
    job_ = Job::kNone;
    worker_.join();

    switch (finished) {
        case Job::kBootWait:
            if (!jobOk_ && !open_) {
                Open("Nenhuma rede conectada depois de " + std::to_string(bootWaitSeconds_) + " s",
                     true);
            }
            break;
        case Job::kStatus: {
            std::string wired;
            std::string wifi;
            hasWifiInterface_ = false;
            for (const auto &line : Lines(jobOutput_)) {
                auto f = SplitTabs(line);
                if (f.size() == 4 && f[0] == "wired" && wired.empty()) {
                    if (f[2] == "1" && f[3] != "-") wired = f[1] + ": conectada (" + f[3] + ")";
                    else if (f[2] == "1") wired = f[1] + ": cabo conectado, sem endereço";
                    else wired = f[1] + ": cabo desconectado";
                } else if (f.size() == 4 && f[0] == "wifi" && !hasWifiInterface_) {
                    hasWifiInterface_ = true;
                    wifi = f[2] != "-" ? "Conectada em \"" + f[2] + "\" (" + f[3] + ")"
                                       : f[1] + ": sem rede";
                }
            }
            wiredStatus_ = wired.empty() ? "Nenhuma porta cabeada" : wired;
            wifiStatus_ = hasWifiInterface_ ? wifi : "Nenhuma interface Wi-Fi (conecte o dongle USB)";
            break;
        }
        case Job::kScan:
            networks_.clear();
            scanned_ = true;
            for (const auto &line : Lines(jobOutput_)) {
                auto f = SplitTabs(line);
                if (f.size() == 4 && f[0] == "net") {
                    networks_.push_back({f[3], std::atoi(f[1].c_str()), f[2] == "1"});
                }
            }
            if (!jobOk_) {
                message_ = ErrorText(jobOutput_, "Falha ao procurar redes");
                messageIsError_ = true;
            } else if (networks_.empty()) {
                message_ = "Nenhuma rede Wi-Fi encontrada";
                messageIsError_ = true;
            } else {
                message_.clear();
            }
            break;
        case Job::kApply: {
            bool link = jobPart2_.find("link\tok") != std::string::npos;
            if (!jobOk_ || !link) {
                screen_ = Screen::kMain;
                cursor_ = 0;
                messageIsError_ = true;
                if (!jobOutput_.empty() && jobPart2_.empty()) {
                    message_ = ErrorText(jobOutput_, "Falha ao configurar o Wi-Fi");
                } else {
                    message_ = "Sem rede: confira o cabo ou a rede Wi-Fi e tente de novo";
                }
                TraceLog(LOG_WARNING, "Menu: teste de conexao falhou (%s)", message_.c_str());
                break;
            }
            int http = 0;
            auto pos = jobPart2_.find("http\t");
            if (pos != std::string::npos) http = std::atoi(jobPart2_.c_str() + pos + 5);

            config_.configured = true;
            config_.network = draftNetwork_;
            config_.wifiSsid = draftNetwork_ == "wifi" ? draftSsid_ : "";
            config_.ubsSlug = draftSlug_;
            if (!SaveBtvConfig(configPath_, config_)) {
                TraceLog(LOG_WARNING, "Menu: nao foi possivel gravar %s", configPath_.c_str());
            }
            TraceLog(LOG_WARNING, "Menu: configurado (rede=%s ssid='%s' ubs='%s' http=%d)",
                     config_.network.c_str(), config_.wifiSsid.c_str(), config_.ubsSlug.c_str(), http);
            closedTitle_ = "BTV CONECTADA";
            closedSubtitle_ = (http >= 100 && http < 500)
                                  ? "Servidor do e-SUS respondendo"
                                  : "Servidor inacessível; o painel tenta de novo sozinho";
            Close();
            break;
        }
        case Job::kNone:
            break;
    }

    if (pendingJob_ != Job::kNone && open_) {
        Job next = pendingJob_;
        pendingJob_ = Job::kNone;
        StartJob(next);
    }
}

std::vector<SetupMenu::Row> SetupMenu::SelectableRows() const {
    if (screen_ == Screen::kWifiList) {
        std::vector<Row> rows(networks_.size(), Row::kNetwork);
        rows.push_back(Row::kRescan);
        rows.push_back(Row::kWifiBack);
        return rows;
    }
    std::vector<Row> rows = {Row::kWired, Row::kWifi, Row::kNoUbs, Row::kUbs, Row::kEnter};
    if (config_.configured) rows.push_back(Row::kLeave);
    return rows;
}

void SetupMenu::Activate(Row row, size_t networkIndex) {
    static const char *const kRowNames[] = {"cabo", "wifi", "sem-ubs", "ubs", "testar-e-entrar",
                                            "sair-sem-alterar", "procurar", "voltar", "rede"};
    TraceLog(LOG_WARNING, "Menu: OK em '%s'", kRowNames[static_cast<int>(row)]);
    messageIsError_ = false;
    switch (row) {
        case Row::kWired:
            draftNetwork_ = "wired";
            message_ = "Rede cabeada escolhida";
            break;
        case Row::kWifi:
            if (!hasWifiInterface_) {
                message_ = "Nenhuma interface Wi-Fi: conecte o dongle USB";
                messageIsError_ = true;
                break;
            }
            screen_ = Screen::kWifiList;
            cursor_ = 0;
            message_.clear();
            StartJob(Job::kScan);
            break;
        case Row::kNoUbs:
            draftSlug_.clear();
            message_ = "Display geral, sem vincular a uma UBS";
            break;
        case Row::kUbs:
            textTarget_ = TextTarget::kSlug;
            textValue_ = draftSlug_;
            screen_ = Screen::kTextEntry;
            message_.clear();
            break;
        case Row::kEnter:
            screen_ = Screen::kTesting;
            message_.clear();
            StartJob(Job::kApply);
            break;
        case Row::kLeave:
            Close();
            break;
        case Row::kNetwork: {
            const WifiNetwork &net = networks_[networkIndex];
            if (net.saved) {
                draftNetwork_ = "wifi";
                draftSsid_ = net.ssid;
                draftPassword_.clear();
                screen_ = Screen::kMain;
                cursor_ = 1;
                message_ = "Wi-Fi \"" + net.ssid + "\" (salva) escolhida";
            } else {
                textTarget_ = TextTarget::kPassword;
                textSsid_ = net.ssid;
                textValue_.clear();
                screen_ = Screen::kTextEntry;
                message_.clear();
            }
            break;
        }
        case Row::kRescan:
            message_.clear();
            StartJob(Job::kScan);
            break;
        case Row::kWifiBack:
            Back();
            break;
    }
}

void SetupMenu::Back() {
    TraceLog(LOG_WARNING, "Menu: VOLTAR");
    messageIsError_ = false;
    switch (screen_) {
        case Screen::kTextEntry:
            screen_ = textTarget_ == TextTarget::kPassword ? Screen::kWifiList : Screen::kMain;
            cursor_ = textTarget_ == TextTarget::kPassword ? 0 : 3;
            textValue_.clear();
            message_.clear();
            break;
        case Screen::kWifiList:
            screen_ = Screen::kMain;
            cursor_ = 1;
            message_.clear();
            break;
        case Screen::kMain:
            if (config_.configured) {
                Close();
            } else {
                message_ = "Primeira configuração: escolha a rede e use \"Testar conexão e entrar\"";
                messageIsError_ = true;
            }
            break;
        case Screen::kTesting:
            break; // o teste termina sozinho (timeout no net-ctl.py)
    }
}

void SetupMenu::HandleTextKeys(RemoteButton tap) {
    int cp = 0;
    while ((cp = GetCharPressed()) != 0) {
        if (cp < 0x20 || Utf8Length(textValue_) >= 63) continue;
        int bytes = 0;
        const char *utf8 = CodepointToUTF8(cp, &bytes);
        textValue_.append(utf8, static_cast<size_t>(bytes));
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) PopUtf8(textValue_);

    if (tap == RemoteButton::kBack) {
        Back();
        return;
    }
    if (tap != RemoteButton::kOk) return;

    if (textTarget_ == TextTarget::kPassword) {
        if (textValue_.size() < 8) {
            // WPA exige 8+ caracteres; redes abertas nao sao suportadas.
            message_ = "A senha do Wi-Fi tem pelo menos 8 caracteres";
            messageIsError_ = true;
            return;
        }
        draftNetwork_ = "wifi";
        draftSsid_ = textSsid_;
        draftPassword_ = textValue_;
        screen_ = Screen::kMain;
        cursor_ = 1;
        message_ = "Wi-Fi \"" + textSsid_ + "\" escolhida (senha nova)";
        messageIsError_ = false;
    } else {
        std::string slug;
        for (char c : textValue_) {
            unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || c == '-') {
                slug.push_back(static_cast<char>(std::tolower(u)));
            } else {
                message_ = "Use só letras sem acento, números e hífen (ex.: ubs-centro)";
                messageIsError_ = true;
                return;
            }
        }
        draftSlug_ = slug;
        screen_ = Screen::kMain;
        cursor_ = 3;
        message_ = slug.empty() ? "Display geral, sem vincular a uma UBS" : "Unidade: " + slug;
        messageIsError_ = false;
    }
    textValue_.clear();
}

void SetupMenu::Update(RemoteButton tap) {
    if (job_ != Job::kNone && jobDone_.load(std::memory_order_acquire)) FinishJob();

    if (!open_) {
        // A fila de caracteres do Raylib acumula mesmo com o menu fechado.
        while (GetCharPressed() != 0) {
        }
        return;
    }

    RemoteButton key = tap != RemoteButton::kNone ? tap : KeyboardButton();

    if (screen_ == Screen::kTextEntry) {
        HandleTextKeys(key);
        return;
    }
    while (GetCharPressed() != 0) {
    }

    if (screen_ == Screen::kMain && job_ == Job::kNone && GetTime() >= nextStatusRefresh_) {
        nextStatusRefresh_ = GetTime() + kStatusRefreshSeconds;
        StartJob(Job::kStatus);
    }
    if (screen_ == Screen::kTesting) return;

    std::vector<Row> rows = SelectableRows();
    if (cursor_ >= rows.size()) cursor_ = rows.size() - 1;
    switch (key) {
        case RemoteButton::kUp:
            cursor_ = cursor_ == 0 ? rows.size() - 1 : cursor_ - 1;
            break;
        case RemoteButton::kDown:
            cursor_ = (cursor_ + 1) % rows.size();
            break;
        case RemoteButton::kOk:
            // Scan em andamento: a lista ainda vai mudar.
            if (!(screen_ == Screen::kWifiList && job_ == Job::kScan)) Activate(rows[cursor_], cursor_);
            break;
        case RemoteButton::kBack:
            Back();
            break;
        default:
            break;
    }
}

SetupMenuView SetupMenu::View() const {
    using Kind = MenuRowView::Kind;
    SetupMenuView view;
    view.message = message_;
    view.messageIsError = messageIsError_;

    if (screen_ == Screen::kTextEntry) {
        view.textEntry = true;
        if (textTarget_ == TextTarget::kPassword) {
            view.subtitle = "Senha do Wi-Fi";
            view.textLabel = "Senha da rede \"" + textSsid_ + "\"";
            for (size_t i = 0; i < Utf8Length(textValue_); ++i) view.textValue += kPasswordDot;
        } else {
            view.subtitle = "Unidade de saúde";
            view.textLabel = "Slug da unidade (ex.: ubs-centro). Vazio = sem vincular";
            view.textValue = textValue_;
        }
        view.hint = "Digite no teclado USB · ENTER ou OK confirma · ESC ou VOLTAR cancela";
        return view;
    }

    if (screen_ == Screen::kTesting) {
        view.subtitle = "Testando conexão";
        view.busy = true;
        view.message = draftNetwork_ == "wifi" ? "Conectando ao Wi-Fi e testando a rede..."
                                               : "Testando a rede...";
        view.messageIsError = false;
        view.hint = "Aguarde";
        view.rows.push_back({Kind::kHeading, "REDE", "", false, false});
        view.rows.push_back({Kind::kOption,
                             draftNetwork_ == "wifi" ? "Wi-Fi \"" + draftSsid_ + "\"" : "Rede cabeada",
                             "", false, true});
        return view;
    }

    size_t selectable = 0;
    auto add = [&](Kind kind, std::string label, std::string detail, bool checked) {
        bool isSelectable = kind != Kind::kHeading;
        view.rows.push_back({kind, std::move(label), std::move(detail),
                             isSelectable && selectable == cursor_, checked});
        if (isSelectable) ++selectable;
    };

    if (screen_ == Screen::kWifiList) {
        view.subtitle = "Wi-Fi: escolha a rede";
        view.hint = "CIMA/BAIXO navegar · OK escolher · VOLTAR volta";
        if (job_ == Job::kScan) {
            view.busy = true;
            view.message = "Procurando redes Wi-Fi...";
            view.messageIsError = false;
        }
        add(Kind::kHeading, scanned_ ? "REDES ENCONTRADAS" : "REDES", "", false);
        for (const auto &net : networks_) {
            std::string detail = std::string(net.saved ? "Salva · " : "Nova · senha pelo teclado · ") +
                                 SignalText(net.signal);
            add(Kind::kOption, net.ssid, detail,
                draftNetwork_ == "wifi" && draftSsid_ == net.ssid);
        }
        add(Kind::kButton, "Procurar de novo", "", false);
        add(Kind::kButton, "Voltar", "", false);
        return view;
    }

    view.subtitle = config_.configured ? "Rede e unidade de saúde" : "Primeira configuração desta BTV";
    view.hint = config_.configured ? "CIMA/BAIXO navegar · OK selecionar · VOLTAR sai sem alterar"
                                   : "CIMA/BAIXO navegar · OK selecionar";
    add(Kind::kHeading, "REDE", "", false);
    add(Kind::kOption, "Usar rede cabeada", wiredStatus_, draftNetwork_ == "wired");
    std::string wifiDetail = wifiStatus_;
    if (draftNetwork_ == "wifi") {
        wifiDetail = "Rede escolhida: \"" + draftSsid_ + "\"" +
                     (draftPassword_.empty() ? " (senha salva)" : " (senha nova)");
    }
    add(Kind::kOption, "Configurar Wi-Fi", wifiDetail, draftNetwork_ == "wifi");
    add(Kind::kHeading, "UNIDADE DE SAÚDE", "", false);
    add(Kind::kOption, "Entrar sem vincular a uma UBS", "Display geral (/display)", draftSlug_.empty());
    add(Kind::kOption, "Unidade de saúde (slug)",
        draftSlug_.empty() ? "Digite o slug no teclado USB" : "/display/" + draftSlug_,
        !draftSlug_.empty());
    add(Kind::kHeading, "", "", false);
    add(Kind::kButton, "Testar conexão e entrar", "Valida a rede, grava a configuração e libera o quiosque",
        false);
    if (config_.configured) {
        add(Kind::kButton, "Sair sem alterar", "Volta ao quiosque com a configuração atual", false);
    }
    return view;
}

} // namespace kiosk
