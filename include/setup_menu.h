#ifndef TVBOX_SETUP_MENU_H
#define TVBOX_SETUP_MENU_H

// Menu "Configuracao da BTV" (secoes 3-6 do manual da BTV11, ver
// docs/files/ e docs/memory/known-issues.md item -13): rede cabeada ou
// Wi-Fi, unidade de saude (slug) ou "sem vincular", e "Testar conexao e
// entrar". Abre no primeiro uso, no boot sem rede e no HOME.
//
// Toda operacao de rede roda no helper net-ctl.py (netplan/networkd),
// numa thread de trabalho — o loop de desenho nunca espera um scan ou
// um `netplan apply`. Navegacao: setas/OK/VOLTAR do controle ou do
// teclado USB; texto (senha nova, slug) so pelo teclado USB — o manual
// proibe teclado virtual.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "remote_control.h"
#include "ui.h"

namespace kiosk {

// Estado persistido em kBtvConfigPath (chave=valor). `configured` e o
// marcador de primeiro uso do manual.
struct BtvConfig {
    bool configured = false;
    std::string network = "wired"; // "wired" | "wifi"
    std::string wifiSsid;
    std::string ubsSlug;           // vazio = sem vincular a uma UBS
};

class SetupMenu {
public:
    SetupMenu(std::string configPath, std::string netCtlScript, std::string testUrl);
    ~SetupMenu();

    SetupMenu(const SetupMenu &) = delete;
    SetupMenu &operator=(const SetupMenu &) = delete;

    bool Configured() const { return config_.configured; }
    bool IsOpen() const { return open_; }

    // Boot de BTV ja configurada: espera a rede ate `seconds` em segundo
    // plano e abre o menu sozinho se nenhuma aparecer.
    void StartBootNetworkCheck(int seconds);

    // `reason` aparece como mensagem ao abrir (vazio = nenhuma);
    // `isError` a pinta na cor de erro (ex.: boot sem rede).
    void Open(const std::string &reason, bool isError = false);

    // Uma vez por frame, aberto ou nao (colhe o resultado da espera de
    // boot). `tap` = botao do controle neste frame; o teclado e lido
    // aqui mesmo, direto do Raylib.
    void Update(RemoteButton tap);

    SetupMenuView View() const;

    // Depois de fechar pelo "Testar conexao e entrar": aviso pro rodape.
    bool TakeClosedNotice(std::string &title, std::string &subtitle);

private:
    enum class Screen { kMain, kWifiList, kTextEntry, kTesting };
    enum class Job { kNone, kBootWait, kStatus, kScan, kApply };
    enum class TextTarget { kPassword, kSlug };
    enum class Row { kWired, kWifi, kNoUbs, kUbs, kEnter, kLeave, kRescan, kWifiBack, kNetwork };

    struct WifiNetwork {
        std::string ssid;
        int signal = -100;
        bool saved = false;
    };

    void StartJob(Job job);
    void FinishJob();
    std::vector<Row> SelectableRows() const;
    void Activate(Row row, size_t networkIndex);
    void Back();
    void HandleTextKeys(RemoteButton tap);
    void Close();

    std::string configPath_;
    std::string netCtl_;
    std::string testUrl_;
    BtvConfig config_;

    bool open_ = false;
    Screen screen_ = Screen::kMain;
    size_t cursor_ = 0;
    std::string message_;
    bool messageIsError_ = false;
    double nextStatusRefresh_ = 0.0;

    // Rascunho: so vira config_ depois do teste bem-sucedido.
    std::string draftNetwork_;
    std::string draftSsid_;
    std::string draftPassword_;
    std::string draftSlug_;

    TextTarget textTarget_ = TextTarget::kSlug;
    std::string textValue_;
    std::string textSsid_;

    // Ultimo status lido do net-ctl.py.
    std::string wiredStatus_ = "verificando...";
    std::string wifiStatus_ = "verificando...";
    bool hasWifiInterface_ = false;
    std::vector<WifiNetwork> networks_;
    bool scanned_ = false;

    std::string closedTitle_;
    std::string closedSubtitle_;

    // Thread de trabalho: um job por vez. O worker so escreve em
    // jobOutput_/jobOk_/jobPart2_ e depois publica jobDone_ (release); o
    // loop principal so le depois de ver jobDone_ (acquire).
    Job job_ = Job::kNone;
    // Scan/teste pedido com outro job rodando (ex.: o status de 5 em 5 s):
    // roda assim que ele acabar, em vez de se perder.
    Job pendingJob_ = Job::kNone;
    std::thread worker_;
    std::atomic<bool> jobDone_{false};
    std::atomic<bool> cancel_{false};
    bool jobOk_ = false;
    std::string jobOutput_;
    std::string jobPart2_;
    int bootWaitSeconds_ = 0;
};

// Le/grava o BtvConfig (gravacao atomica com fsync, ver known-issues -12).
bool LoadBtvConfig(const std::string &path, BtvConfig &out);
bool SaveBtvConfig(const std::string &path, const BtvConfig &config);

} // namespace kiosk

#endif // TVBOX_SETUP_MENU_H
