#include "scraper.h"

#include <cstdlib>
#include <regex>

#include "config.h"
#include "procexec.h"

namespace kiosk {

namespace {

std::string UrlDecode(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            std::string hex = in.substr(i + 1, 2);
            char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out.push_back(decoded);
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

// Espelha a lógica de detecção de arquivo direto do app original
// (`/\.(mp4|webm)(\?.*)?$/i`).
bool LooksLikeDirectVideoFile(const std::string &url) {
    static const std::regex direct(R"RX(\.(mp4|webm)(\?.*)?$)RX",
                                    std::regex::icase);
    return std::regex_search(url, direct);
}

// Reconstrói a URL original a partir de uma URL de embed conhecida, para
// que o player possa passá-la ao yt-dlp (nunca reproduzimos via iframe).
struct ResolvedEmbed {
    VideoOrigin origem = VideoOrigin::Desconhecido;
    std::string urlOriginal;
};

ResolvedEmbed ResolveEmbedSrc(const std::string &iframeSrc) {
    ResolvedEmbed result;

    {
        static const std::regex yt(R"(youtube-nocookie\.com/embed/([A-Za-z0-9_-]{6,}))");
        std::smatch m;
        if (std::regex_search(iframeSrc, m, yt)) {
            result.origem = VideoOrigin::YouTube;
            result.urlOriginal = "https://www.youtube.com/watch?v=" + m[1].str();
            return result;
        }
    }
    {
        static const std::regex ig(
            R"(instagram\.com/(p|reel|reels|tv)/([A-Za-z0-9_-]+)/embed)");
        std::smatch m;
        if (std::regex_search(iframeSrc, m, ig)) {
            std::string tipo = m[1].str();
            if (tipo == "reels") tipo = "reel";
            result.origem = VideoOrigin::Instagram;
            result.urlOriginal = "https://www.instagram.com/" + tipo + "/" + m[2].str() + "/";
            return result;
        }
    }
    {
        static const std::regex fb(R"(facebook\.com/plugins/video\.php\?.*[?&]href=([^&]+))");
        std::smatch m;
        if (std::regex_search(iframeSrc, m, fb)) {
            result.origem = VideoOrigin::Facebook;
            result.urlOriginal = UrlDecode(m[1].str());
            return result;
        }
    }
    return result;
}

} // namespace

DisplayScraper::DisplayScraper(std::string displayUrl)
    : displayUrl_(std::move(displayUrl)) {}

bool DisplayScraper::Refresh(std::vector<Campaign> &out) const {
    std::string html;
    bool ok = RunCaptureStdout(
        {"curl", "-s", "-L", "--max-time", std::to_string(kFetchTimeoutSeconds),
         "-A", "tvbox-esus-otimizado/1.0", displayUrl_},
        kFetchTimeoutSeconds + 2, html);
    if (!ok || html.empty()) return false;

    out = ParseHtml(html);
    return true;
}

std::vector<Campaign> DisplayScraper::ParseHtml(const std::string &html) {
    std::vector<Campaign> campaigns;
    int ordem = 0;

    // <video ... src="...">
    {
        static const std::regex videoTag(R"RX(<video\b[^>]*\bsrc="([^"]+)"[^>]*>)RX");
        for (auto it = std::sregex_iterator(html.begin(), html.end(), videoTag);
             it != std::sregex_iterator(); ++it) {
            Campaign c;
            c.id = "scraped-video:" + std::to_string(ordem);
            c.tipo = CampaignType::Video;
            c.video_url = (*it)[1].str();
            c.video_origem = LooksLikeDirectVideoFile(c.video_url) ? VideoOrigin::Direto
                                                                    : VideoOrigin::Upload;
            c.duracao_segundos = kDefaultSlideDurationSeconds;
            c.ordem = ordem++;
            campaigns.push_back(std::move(c));
        }
    }

    // <iframe ... src="..."> (youtube/instagram/facebook embeds)
    {
        static const std::regex iframeTag(R"RX(<iframe\b[^>]*\bsrc="([^"]+)"[^>]*>)RX");
        for (auto it = std::sregex_iterator(html.begin(), html.end(), iframeTag);
             it != std::sregex_iterator(); ++it) {
            std::string src = (*it)[1].str();
            ResolvedEmbed resolved = ResolveEmbedSrc(src);
            if (resolved.origem == VideoOrigin::Desconhecido) continue;

            Campaign c;
            c.id = "scraped-embed:" + std::to_string(ordem);
            c.tipo = CampaignType::Video;
            c.video_url = resolved.urlOriginal;
            c.video_origem = resolved.origem;
            c.duracao_segundos = kDefaultSlideDurationSeconds;
            c.ordem = ordem++;
            campaigns.push_back(std::move(c));
        }
    }

    // <img class="institutional-media" src="..." alt="...">
    {
        static const std::regex imgTag(
            R"RX(<img\b[^>]*\bclass="institutional-media"[^>]*\bsrc="([^"]+)"[^>]*?(?:\balt="([^"]*)")?[^>]*>)RX");
        for (auto it = std::sregex_iterator(html.begin(), html.end(), imgTag);
             it != std::sregex_iterator(); ++it) {
            Campaign c;
            c.id = "scraped-imagem:" + std::to_string(ordem);
            c.tipo = CampaignType::Imagem;
            c.imagem_url = (*it)[1].str();
            c.titulo = (*it)[2].matched ? (*it)[2].str() : "";
            c.duracao_segundos = kDefaultSlideDurationSeconds;
            c.ordem = ordem++;
            campaigns.push_back(std::move(c));
        }
    }

    return campaigns;
}

} // namespace kiosk
