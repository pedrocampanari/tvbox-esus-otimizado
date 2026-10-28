// TV Box e-SUS Otimizado — kiosk player para Armbian/RK3229.
// Réplica (não-iframe) de https://esustv.jfbatl.com.br/display, fixo em
// 25% de largura x 100% de altura da tela. Ver docs/memory/ para o
// desenho completo e limitações conhecidas.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "raylib.h"

#include "campaign.h"
#include "campaign_store.h"
#include "config.h"
#include "native_window.h"
#include "player.h"
#include "procexec.h"
#include "remote_control.h"
#include "scraper.h"
#include "setup_menu.h"
#include "ui.h"
#include "video_cache.h"

using namespace kiosk;

namespace {

std::mutex g_campaignsMutex;
std::vector<Campaign> g_campaigns;
// Incrementado a cada troca de g_campaigns: o loop principal só copia a
// lista quando ela muda de fato, não a cada frame.
std::atomic<unsigned> g_campaignsGeneration{0};
std::atomic<bool> g_running{true};

Campaign DefaultWaitingCampaign() {
    Campaign c;
    c.id = "tecnico:aguardando-informacoes";
    c.tipo = CampaignType::Texto;
    c.titulo = "SECRETARIA MUNICIPAL DE SAÚDE";
    c.subtitulo = "Aguardando informações";
    c.duracao_segundos = 15;
    return c;
}

void ScraperThreadLoop(const std::string &displayUrl) {
    DisplayScraper scraper(displayUrl);
    while (g_running.load()) {
        std::vector<Campaign> fetched;
        if (scraper.Refresh(fetched) && !fetched.empty()) {
            std::lock_guard<std::mutex> lock(g_campaignsMutex);
            g_campaigns = std::move(fetched);
            g_campaignsGeneration.fetch_add(1);
        }
        for (int waited = 0; waited < kScrapePollIntervalSeconds && g_running.load(); ++waited) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

std::string GuessImageFileType(const std::string &url) {
    auto dot = url.find_last_of('.');
    if (dot == std::string::npos) return ".png";
    std::string ext = url.substr(dot);
    auto q = ext.find_first_of("?#");
    if (q != std::string::npos) ext = ext.substr(0, q);
    return ext.empty() ? ".png" : ext;
}

Texture2D DownloadImageTexture(const std::string &url) {
    std::string data;
    bool ok = RunCaptureStdout(
        {"curl", "-s", "-L", "--max-time", std::to_string(kFetchTimeoutSeconds), url},
        kFetchTimeoutSeconds + 2, data);
    if (!ok || data.empty()) return Texture2D{};

    std::string fileType = GuessImageFileType(url);
    Image img = LoadImageFromMemory(fileType.c_str(),
                                     reinterpret_cast<const unsigned char *>(data.data()),
                                     static_cast<int>(data.size()));
    if (img.data == nullptr) return Texture2D{};
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

} // namespace

// Fim do slide de vídeo: o vídeo tocou inteiro (mpv saiu sozinho). Teto
// de segurança caso o mpv nunca termine (stream ao vivo, travamento).
constexpr double kVideoMaxSlideSeconds = 20.0 * 60.0;
// Mínimo que um slide de vídeo com falha fica na tela (spinner) antes
// de avançar — evita piscar slides em sequência quando está tudo
// falhando (sem rede e sem cache).
constexpr double kVideoFailureHoldSeconds = 3.0;
// Com o menu aberto, a janela volta para cima do Chromium a cada tanto:
// no primeiro uso o menu abre antes de o painel subir, e o painel
// mapeado depois cobriria o menu.
constexpr double kMenuRaiseIntervalSeconds = 2.0;

void HandleTerminationSignal(int) { g_running.store(false); }

// Roda o panel-ctl.sh numa thread solta: ele espera o Chromium sair (até
// ~10s), e o loop de desenho não pode congelar esse tempo todo.
void RunPanelCtlDetached(const char *action) {
    std::thread([action] {
        std::string ignored;
        if (!RunCaptureStdout({kPanelCtlScript, action}, 20, ignored)) {
            TraceLog(LOG_WARNING, "Painel: '%s %s' falhou (ver panel.log)", kPanelCtlScript, action);
        }
    }).detach();
}

// Segundos que faltam até `limit`, arredondado pra cima (o "3 s" do aviso
// só vira "2 s" quando de fato passou mais um segundo).
int SecondsLeft(double held, double limit) {
    return std::max(1, static_cast<int>(std::ceil(limit - held)));
}

struct NoticeText {
    std::string title;
    std::string subtitle;
    float progress = -1.0f;
};

// Feedback enquanto um botão de segurar está pressionado (ver
// include/config.h, kPanelResetHoldSeconds e vizinhos). false = nada
// a mostrar (toque curto ainda dentro do tempo de um toque normal).
bool HoldFeedback(const RemoteInput &input, NoticeText &out) {
    if (input.holding == RemoteButton::kNone || input.heldSeconds < kHoldFeedbackSeconds) {
        return false;
    }
    double held = input.heldSeconds;
    if (input.holding == RemoteButton::kKey3) {
        if (held < kPanelResetHoldSeconds) {
            out.title = "RESET DO PEC EM " +
                        std::to_string(SecondsLeft(held, kPanelResetHoldSeconds)) + " s";
            out.subtitle = "Apaga o vínculo do e-SUS. Solte para cancelar";
            out.progress = static_cast<float>(held / kPanelResetHoldSeconds);
        } else {
            out.title = "PAINEL RESETADO";
            out.subtitle = "Reabrindo o e-SUS sem cookies";
            out.progress = 1.0f;
        }
        return true;
    }
    if (input.holding == RemoteButton::kPower) {
        if (held < kPowerRebootHoldSeconds) {
            out.title = "REINICIANDO EM " +
                        std::to_string(SecondsLeft(held, kPowerRebootHoldSeconds)) + " s";
            out.subtitle = "Solte o POWER para cancelar";
            out.progress = static_cast<float>(held / kPowerRebootHoldSeconds);
        } else {
            out.title = "REINICIANDO...";
            out.subtitle = "Aguarde";
            out.progress = 1.0f;
        }
        return true;
    }
    return false;
}

int main() {
    // SIGTERM/SIGINT (exec.sh, systemd, Ctrl+C): sai pelo caminho normal
    // do loop, que para o mpv e o download em andamento antes de fechar.
    std::signal(SIGTERM, HandleTerminationSignal);
    std::signal(SIGINT, HandleTerminationSignal);

    // stdout/stderr vão pro kiosk.log (exec.sh): sem isto, avisos do
    // TraceLog ficam presos no buffer e só aparecem muito depois (ou
    // nunca, num crash).
    setvbuf(stdout, nullptr, _IOLBF, 0);

    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(100, 100, "TV Box e-SUS Otimizado");
    // ESC do teclado USB e do menu de configuracao (voltar), nao "fechar
    // o app" — o padrao do Raylib derrubaria o kiosk.
    SetExitKey(KEY_NULL);

    int monitor = GetCurrentMonitor();
    int monitorW = GetMonitorWidth(monitor);
    int monitorH = GetMonitorHeight(monitor);
    int windowW = static_cast<int>(monitorW * kWindowWidthFraction);
    int windowH = static_cast<int>(monitorH * kWindowHeightFraction);
    int windowX = kAnchorWindowToRightEdge ? (monitorW - windowW) : 0;
    SetWindowSize(windowW, windowH);
    SetWindowPosition(windowX, 0);
    SetTargetFPS(30); // ajustado por frame no loop (ver "FPS dinâmico").

    LoadUiFonts();

    std::thread scraperThread;
    if (kUseLiveScraping) {
        scraperThread = std::thread(ScraperThreadLoop, std::string(kDisplayUrl));
    } else {
        // Modo padrão (ver include/config.h): lista fixa, carregada uma
        // única vez — sem ficar buscando a URL original o tempo todo.
        std::vector<Campaign> fixed = LoadFixedCampaigns(kFixedCampaignsConfigPath);
        std::lock_guard<std::mutex> lock(g_campaignsMutex);
        g_campaigns = std::move(fixed);
        g_campaignsGeneration.fetch_add(1);
    }

    // Cache local dos vídeos (ver include/video_cache.h): baixa em
    // segundo plano, um por vez, sempre o próximo da rotação.
    VideoCache videoCache(kVideoCacheDir);

    unsigned long nativeWindowId = GetNativeX11WindowId(GetWindowHandle());
    if (nativeWindowId == 0) {
        TraceLog(LOG_WARNING,
                  "VideoPlayer: nao foi possivel obter o ID X11 nativo da janela (GLFW nao esta em modo X11?); "
                  "slides de video serao ignorados.");
    }

    VideoPlayer player;
    bool playerReady = nativeWindowId != 0 && player.Init(nativeWindowId, 0, 0, windowW, windowH);
    if (nativeWindowId != 0 && !playerReady) {
        TraceLog(LOG_WARNING, "VideoPlayer: nao foi possivel abrir o display X11; slides de video serao ignorados.");
    }

    // Controle remoto: so mostra o indicador se o receptor IR foi
    // encontrado de verdade (ver include/remote_control.h). O reboot do
    // POWER continua fora do processo, via triggerhappy — aqui só a
    // contagem na tela. Ver docs/memory/known-issues.md item -6.
    RemoteControl remote;
    if (!remote.IsAvailable()) {
        TraceLog(LOG_WARNING,
                  "RemoteControl: receptor IR (driver 'gpio_ir_recv') nao encontrado em "
                  "/proc/bus/input/devices; VOL+/VOL-/MUTE do controle remoto serao ignorados.");
    }
    double volumeOsdUntil = 0.0;
    int volumeOsdPercent = 0;
    bool volumeOsdMuted = false;
    double noticeOsdUntil = 0.0;
    NoticeText notice;
    bool panelResetFired = false;

    // Menu "Configuracao da BTV" (include/setup_menu.h): primeiro uso,
    // boot sem rede e HOME. Aberto, o app ocupa a tela toda por cima do
    // painel e o slide atual para; ao fechar, volta aos 25% e recomeca.
    SetupMenu setupMenu(kBtvConfigPath, kNetCtlScript, kNetworkTestUrl);
    if (!setupMenu.Configured()) {
        setupMenu.Open("Primeira configuração: escolha a rede e a unidade");
    } else {
        setupMenu.StartBootNetworkCheck(kBootNetworkWaitSeconds);
    }
    bool menuWasOpen = false;
    double menuRaiseAt = 0.0;

    size_t currentIndex = 0;
    bool startSlide = true; // força (re)início mesmo se o id não mudou (lista de 1 item)
    double slideStartTime = GetTime();
    double slideDeadline = 0.0;
    std::string activeId;
    CampaignType activeType = CampaignType::Texto;
    Texture2D imageTexture{};
    std::string imageTextureUrl;
    bool videoConfirmedStarted = false;

    std::vector<Campaign> snapshot;
    unsigned snapshotGeneration = ~0u;

    while (g_running.load() && !WindowShouldClose()) {
        RemoteInput remoteInput = remote.Poll(GetTime());
        if (!setupMenu.IsOpen() && remoteInput.tap == RemoteButton::kHome) setupMenu.Open("");
        setupMenu.Update(setupMenu.IsOpen() ? remoteInput.tap : RemoteButton::kNone);

        bool menuOpen = setupMenu.IsOpen();
        if (menuOpen != menuWasOpen) {
            menuWasOpen = menuOpen;
            if (menuOpen) {
                if (playerReady) player.Stop();
                SetWindowSize(monitorW, monitorH);
                SetWindowPosition(0, 0);
                // Sem gerenciador de janelas: XRaiseWindow + foco do
                // teclado (o painel do Chromium fica embaixo).
                SetWindowFocused();
                menuRaiseAt = GetTime() + kMenuRaiseIntervalSeconds;
            } else {
                SetWindowSize(windowW, windowH);
                SetWindowPosition(windowX, 0);
                startSlide = true;
                std::string title;
                std::string subtitle;
                if (setupMenu.TakeClosedNotice(title, subtitle)) {
                    notice = {title, subtitle, -1.0f};
                    noticeOsdUntil = GetTime() + kNoticeOsdDurationSeconds;
                }
            }
        }
        if (menuOpen) {
            if (GetTime() >= menuRaiseAt) {
                SetWindowFocused();
                menuRaiseAt = GetTime() + kMenuRaiseIntervalSeconds;
            }
            // Volume, 1/2/3 do controle ficam para fora do menu; o POWER
            // continua reiniciando (triggerhappy, fora deste processo).
            SetTargetFPS(30);
            BeginDrawing();
            DrawSetupMenu({0, 0, static_cast<float>(GetScreenWidth()),
                           static_cast<float>(GetScreenHeight())},
                          setupMenu.View(), static_cast<float>(GetTime()));
            EndDrawing();
            continue;
        }

        if (g_campaignsGeneration.load() != snapshotGeneration) {
            {
                std::lock_guard<std::mutex> lock(g_campaignsMutex);
                snapshot = g_campaigns;
                snapshotGeneration = g_campaignsGeneration.load();
            }
            if (snapshot.empty()) snapshot.push_back(DefaultWaitingCampaign());
            videoCache.SetCampaigns(snapshot);
        }
        currentIndex %= snapshot.size();

        const Campaign &active = snapshot[currentIndex];
        int durationSeconds = active.duracao_segundos > 0 ? active.duracao_segundos
                                                            : kDefaultSlideDurationSeconds;

        if (startSlide || active.id != activeId) {
            startSlide = false;
            activeId = active.id;
            activeType = active.tipo;
            slideStartTime = GetTime();
            videoConfirmedStarted = false;
            videoCache.SetCurrentIndex(currentIndex);

            if (activeType == CampaignType::Video) {
                // Vídeo: o slide dura o vídeo inteiro (ver HasEnded
                // abaixo); o deadline aqui é só o teto de segurança.
                slideDeadline = slideStartTime + kVideoMaxSlideSeconds;
                if (!playerReady) {
                    slideDeadline = slideStartTime + durationSeconds;
                } else {
                    // Arquivo do cache (se houver) + fim útil, que corta o
                    // preto final que vários vídeos trazem no próprio arquivo.
                    std::string localFile;
                    double playEnd = 0;
                    videoCache.LocalFileFor(active.video_url, localFile, playEnd);
                    if (!player.Play(active, localFile, playEnd)) {
                        // Falha ao iniciar: comporta-se como o onFalha do
                        // app original e avança (depois de um respiro mínimo).
                        slideDeadline = slideStartTime + kVideoFailureHoldSeconds;
                    }
                }
            } else {
                slideDeadline = slideStartTime + durationSeconds;
                if (playerReady) player.Stop();
            }

            if (activeType == CampaignType::Imagem && active.imagem_url != imageTextureUrl) {
                if (imageTexture.id != 0) UnloadTexture(imageTexture);
                imageTexture = DownloadImageTexture(active.imagem_url);
                imageTextureUrl = active.imagem_url;
            }
        }

        // Com o player disponível, só considera o vídeo pronto quando o
        // mpv confirmar via IPC que já está de fato decodificando —
        // enquanto isso, mostra o spinner. Se demorar demais ou o mpv
        // sair antes (URL/arquivo inválido), desiste e avança, igual o
        // onFalha do site original. Depois de confirmado, o slide acaba
        // quando o mpv sai sozinho no fim do arquivo.
        bool videoReady = !playerReady;
        if (activeType == CampaignType::Video && playerReady) {
            double now = GetTime();
            if (!videoConfirmedStarted && player.IsVideoActuallyPlaying()) {
                videoConfirmedStarted = true;
            }
            videoReady = videoConfirmedStarted;
            if (player.HasEnded()) {
                if (videoConfirmedStarted) {
                    videoCache.RecordShown(active);
                    slideDeadline = now;
                } else {
                    slideDeadline = std::max(now, slideStartTime + kVideoFailureHoldSeconds);
                }
            } else if (!videoConfirmedStarted && now - slideStartTime >= kVideoLoadTimeoutSeconds) {
                player.Stop();
                slideDeadline = now;
            }
        }

        if (GetTime() >= slideDeadline) {
            currentIndex = (currentIndex + 1) % snapshot.size();
            startSlide = true;
        }

        bool volumeAction = remoteInput.tap == RemoteButton::kVolumeUp ||
                            remoteInput.tap == RemoteButton::kVolumeDown;
        bool muteAction = remoteInput.tap == RemoteButton::kMute;
        if (remoteInput.tap == RemoteButton::kKey1) {
            // Fecha navegador extra; o loop do exec.sh reabre o painel
            // oficial se ele não estiver de pé.
            TraceLog(LOG_WARNING, "Controle: 1 -> restaurando o modo quiosque");
            RunPanelCtlDetached("kiosk");
            notice = {"MODO QUIOSQUE", "Restaurando as janelas oficiais", -1.0f};
            noticeOsdUntil = GetTime() + kNoticeOsdDurationSeconds;
        }
        if (remoteInput.tap == RemoteButton::kKey2) {
            // exec.sh sobe o app de novo em 2s; o painel não é tocado.
            TraceLog(LOG_WARNING, "Controle: 2 -> recarregando o Display");
            g_running.store(false);
        }
        // 3 dispara uma vez quando completa o tempo, ainda segurado; só
        // rearma depois de soltar.
        if (remoteInput.holding == RemoteButton::kKey3) {
            if (!panelResetFired && remoteInput.heldSeconds >= kPanelResetHoldSeconds) {
                TraceLog(LOG_WARNING, "Controle: 3 segurado %.1fs -> reset do painel",
                         remoteInput.heldSeconds);
                RunPanelCtlDetached("reset");
                panelResetFired = true;
            }
        } else if (panelResetFired) {
            panelResetFired = false;
            notice = {"PAINEL RESETADO", "Reabrindo o e-SUS sem cookies", -1.0f};
            noticeOsdUntil = GetTime() + kNoticeOsdDurationSeconds;
        }

        if (volumeAction || muteAction) {
            int newPercent = 0;
            bool newMuted = false;
            bool adjusted = false;
            if (muteAction) {
                adjusted = AdjustVolume(0, true, newPercent, newMuted);
            } else {
                int delta = remoteInput.tap == RemoteButton::kVolumeUp ? +5 : -5;
                adjusted = AdjustVolume(delta, false, newPercent, newMuted);
            }
            if (adjusted) {
                volumeOsdPercent = newPercent;
                volumeOsdMuted = newMuted;
                volumeOsdUntil = GetTime() + kVolumeOsdDurationSeconds;
            } else {
                // So loga uma vez (nao a cada toque de botao) — motivo
                // mais provavel: nenhum mixer ALSA por software (saida
                // HDMI pura) ou `amixer` ausente do PATH.
                static bool warnedAdjustFailed = false;
                if (!warnedAdjustFailed) {
                    TraceLog(LOG_WARNING,
                              "RemoteControl: tecla de volume detectada, mas AdjustVolume() falhou "
                              "(sem mixer ALSA controlavel, ou 'amixer' nao encontrado?).");
                    warnedAdjustFailed = true;
                }
            }
        }
        NoticeText holdNotice;
        bool showHoldOsd = HoldFeedback(remoteInput, holdNotice);
        bool showNoticeOsd = !showHoldOsd && GetTime() < noticeOsdUntil;
        bool showVolumeOsd = GetTime() < volumeOsdUntil;

        // FPS dinâmico: 30 só quando algo anima (spinner de carregamento,
        // indicador de volume); no resto do tempo a tela é estática (texto,
        // imagem, ou o mpv desenhando o vídeo na janela dele) e 10fps
        // bastam — corta boa parte do CPU do próprio app no RK3229.
        bool animating = showVolumeOsd || showHoldOsd ||
                         (activeType == CampaignType::Video && !videoReady);
        SetTargetFPS(animating ? 30 : 10);

        int screenW = GetScreenWidth();
        int screenH = GetScreenHeight();
        float headerHeight = std::clamp(screenH * 0.08f, 32.0f, 90.0f);
        float footerHeight = std::clamp(screenH * 0.08f, 32.0f, 90.0f);
        Rectangle headerRect = {0, 0, static_cast<float>(screenW), headerHeight};
        Rectangle footerRect = {0, screenH - footerHeight, static_cast<float>(screenW), footerHeight};
        Rectangle bannerRect = {0, headerHeight, static_cast<float>(screenW),
                                 screenH - headerHeight - footerHeight};

        if (playerReady) {
            // Coordenadas relativas à janela do Raylib (janela de vídeo
            // é filha dela, não uma segunda top-level) — sem precisar
            // somar a posição da janela na tela.
            player.SetGeometry(static_cast<int>(bannerRect.x), static_cast<int>(bannerRect.y),
                                static_cast<int>(bannerRect.width),
                                static_cast<int>(bannerRect.height));
        }

        BeginDrawing();
        ClearBackground(kColorAppBackground);
        DrawChromeBar(headerRect, kHeaderTitle, kHeaderSubtitle, kColorHeaderBackground,
                      kColorChromeText);
        if (showHoldOsd || showNoticeOsd) {
            const NoticeText &n = showHoldOsd ? holdNotice : notice;
            DrawNoticeOsd(footerRect, n.title.c_str(), n.subtitle.c_str(), n.progress,
                          kColorFooterBackground, kColorChromeText);
        } else if (showVolumeOsd) {
            DrawVolumeOsd(footerRect, volumeOsdPercent, volumeOsdMuted, kColorFooterBackground,
                          kColorChromeText);
        } else {
            DrawChromeBar(footerRect, kFooterTitle, kFooterSubtitle, kColorFooterBackground,
                          kColorChromeText);
        }

        switch (activeType) {
            case CampaignType::Texto:
                DrawTextSlide(bannerRect, active);
                break;
            case CampaignType::Imagem:
                DrawImageSlide(bannerRect, imageTexture);
                break;
            case CampaignType::Video:
                if (videoReady) {
                    DrawVideoPlaceholder(bannerRect);
                } else {
                    DrawLoadingSlide(bannerRect, active, static_cast<float>(GetTime()));
                }
                break;
        }
        EndDrawing();
    }

    g_running.store(false);
    if (scraperThread.joinable()) scraperThread.join();

    if (imageTexture.id != 0) UnloadTexture(imageTexture);
    player.Shutdown();
    UnloadUiFonts();
    CloseWindow();
    return 0;
}
