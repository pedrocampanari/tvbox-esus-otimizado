// TV Box e-SUS Otimizado — kiosk player para Armbian/RK3229.
// Réplica (não-iframe) de https://esustv.jfbatl.com.br/display, fixo em
// 25% de largura x 100% de altura da tela. Ver docs/memory/ para o
// desenho completo e limitações conhecidas.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "raylib.h"

#include "campaign.h"
#include "campaign_store.h"
#include "config.h"
#include "native_window.h"
#include "player.h"
#include "procexec.h"
#include "scraper.h"
#include "ui.h"

using namespace kiosk;

namespace {

std::mutex g_campaignsMutex;
std::vector<Campaign> g_campaigns;
std::atomic<bool> g_running{true};

Campaign DefaultWaitingCampaign() {
    Campaign c;
    c.id = "tecnico:aguardando-informacoes";
    c.tipo = CampaignType::Texto;
    c.titulo = "SECRETARIA MUNICIPAL DE SAUDE";
    c.subtitulo = "Aguardando informacoes";
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

int main() {
    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(100, 100, "TV Box e-SUS Otimizado");

    int monitor = GetCurrentMonitor();
    int monitorW = GetMonitorWidth(monitor);
    int monitorH = GetMonitorHeight(monitor);
    int windowW = static_cast<int>(monitorW * kWindowWidthFraction);
    int windowH = static_cast<int>(monitorH * kWindowHeightFraction);
    int windowX = kAnchorWindowToRightEdge ? (monitorW - windowW) : 0;
    SetWindowSize(windowW, windowH);
    SetWindowPosition(windowX, 0);
    SetTargetFPS(30); // slideshow estático na maior parte do tempo: 30fps já sobra e economiza CPU/energia no RK3229.

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
    }

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

    size_t currentIndex = 0;
    double slideStartTime = GetTime();
    std::string activeId;
    CampaignType activeType = CampaignType::Texto;
    Texture2D imageTexture{};
    std::string imageTextureUrl;

    while (g_running.load() && !WindowShouldClose()) {
        std::vector<Campaign> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_campaignsMutex);
            snapshot = g_campaigns;
        }
        if (snapshot.empty()) snapshot.push_back(DefaultWaitingCampaign());
        currentIndex %= snapshot.size();

        const Campaign &active = snapshot[currentIndex];
        int durationSeconds = active.duracao_segundos > 0 ? active.duracao_segundos
                                                            : kDefaultSlideDurationSeconds;

        bool slideChanged = active.id != activeId;
        if (slideChanged) {
            activeId = active.id;
            activeType = active.tipo;
            slideStartTime = GetTime();

            if (activeType == CampaignType::Video && playerReady) {
                if (!player.Play(active)) {
                    // Falha ao resolver/tocar: comporta-se como o app
                    // original (onFalha) e antecipa o avanco de slide.
                    slideStartTime = GetTime() - durationSeconds;
                }
            } else if (playerReady) {
                player.Stop();
            }

            if (activeType == CampaignType::Imagem && active.imagem_url != imageTextureUrl) {
                if (imageTexture.id != 0) UnloadTexture(imageTexture);
                imageTexture = DownloadImageTexture(active.imagem_url);
                imageTextureUrl = active.imagem_url;
            }
        }

        if (GetTime() - slideStartTime >= durationSeconds) {
            currentIndex = (currentIndex + 1) % snapshot.size();
        }

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
        DrawChromeBar(footerRect, kFooterTitle, kFooterSubtitle, kColorFooterBackground,
                      kColorChromeText);

        switch (activeType) {
            case CampaignType::Texto:
                DrawTextSlide(bannerRect, active);
                break;
            case CampaignType::Imagem:
                DrawImageSlide(bannerRect, imageTexture);
                break;
            case CampaignType::Video:
                DrawVideoPlaceholder(bannerRect);
                break;
        }
        EndDrawing();
    }

    g_running.store(false);
    if (scraperThread.joinable()) scraperThread.join();

    if (imageTexture.id != 0) UnloadTexture(imageTexture);
    player.Stop();
    UnloadUiFonts();
    CloseWindow();
    return 0;
}
