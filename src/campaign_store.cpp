#include "campaign_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace kiosk {

namespace {

std::string Trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Troca a sequência literal "\n" (duas chars: barra invertida + n) por
// uma quebra de linha real, pra permitir texto multi-linha num arquivo
// de config de uma linha por campo.
std::string UnescapeNewlines(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out.push_back('\n');
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

CampaignType ParseTipo(const std::string &v) {
    if (v == "video") return CampaignType::Video;
    if (v == "imagem") return CampaignType::Imagem;
    return CampaignType::Texto;
}

VideoOrigin ParseOrigem(const std::string &v) {
    if (v == "youtube") return VideoOrigin::YouTube;
    if (v == "instagram") return VideoOrigin::Instagram;
    if (v == "facebook") return VideoOrigin::Facebook;
    if (v == "direto") return VideoOrigin::Direto;
    if (v == "upload") return VideoOrigin::Upload;
    return VideoOrigin::Desconhecido;
}

const char *TipoName(CampaignType t) {
    if (t == CampaignType::Video) return "video";
    if (t == CampaignType::Imagem) return "imagem";
    return "texto";
}

const char *OrigemName(VideoOrigin o) {
    switch (o) {
        case VideoOrigin::YouTube: return "youtube";
        case VideoOrigin::Instagram: return "instagram";
        case VideoOrigin::Facebook: return "facebook";
        case VideoOrigin::Direto: return "direto";
        case VideoOrigin::Upload: return "upload";
        default: return "";
    }
}

// Inverso de UnescapeNewlines; \r some (o arquivo é por linha).
std::string EscapeNewlines(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c != '\r') out.push_back(c);
    }
    return out;
}

void SyncPath(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return;
    fsync(fd);
    close(fd);
}

} // namespace

std::vector<Campaign> LoadFixedCampaigns(const std::string &path) {
    std::vector<Campaign> campaigns;

    std::ifstream file(path);
    if (!file.is_open()) return campaigns;

    Campaign current;
    bool inBlock = false;
    int ordem = 0;

    auto flush = [&]() {
        if (!inBlock) return;
        if (current.id.empty()) current.id = "fixo:" + std::to_string(ordem);
        current.ordem = ordem++;
        campaigns.push_back(current);
        current = Campaign{};
    };

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed == "[campanha]") {
            flush();
            inBlock = true;
            continue;
        }
        if (!inBlock) continue;

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(trimmed.substr(0, eq));
        std::string value = UnescapeNewlines(Trim(trimmed.substr(eq + 1)));

        if (key == "id") current.id = value;
        else if (key == "tipo") current.tipo = ParseTipo(value);
        else if (key == "titulo") current.titulo = value;
        else if (key == "subtitulo") current.subtitulo = value;
        else if (key == "texto") current.texto = value;
        else if (key == "imagem_url") current.imagem_url = value;
        else if (key == "video_url") current.video_url = value;
        else if (key == "video_origem") current.video_origem = ParseOrigem(value);
        else if (key == "duracao_segundos") {
            try {
                current.duracao_segundos = std::stoi(value);
            } catch (...) {
                current.duracao_segundos = 0;
            }
        }
    }
    flush();

    return campaigns;
}

std::string SerializeCampaigns(const std::vector<Campaign> &campaigns) {
    std::ostringstream out;
    for (const auto &c : campaigns) {
        out << "[campanha]\n"
            << "id=" << EscapeNewlines(c.id) << "\n"
            << "tipo=" << TipoName(c.tipo) << "\n"
            << "titulo=" << EscapeNewlines(c.titulo) << "\n"
            << "subtitulo=" << EscapeNewlines(c.subtitulo) << "\n"
            << "texto=" << EscapeNewlines(c.texto) << "\n"
            << "imagem_url=" << EscapeNewlines(c.imagem_url) << "\n"
            << "video_url=" << EscapeNewlines(c.video_url) << "\n"
            << "video_origem=" << OrigemName(c.video_origem) << "\n"
            << "duracao_segundos=" << c.duracao_segundos << "\n\n";
    }
    return out.str();
}

std::string SerializeTvChrome(const TvChrome &chrome) {
    std::ostringstream out;
    out << "cabecalho_visivel=" << (chrome.headerVisible ? 1 : 0) << "\n"
        << "cabecalho_texto=" << EscapeNewlines(chrome.headerTitle) << "\n"
        << "cabecalho_subtexto=" << EscapeNewlines(chrome.headerSubtitle) << "\n"
        << "cabecalho_cor_fundo=" << chrome.headerBackground << "\n"
        << "cabecalho_cor_texto=" << chrome.headerForeground << "\n"
        << "rodape_visivel=" << (chrome.footerVisible ? 1 : 0) << "\n"
        << "rodape_texto=" << EscapeNewlines(chrome.footerTitle) << "\n"
        << "rodape_subtexto=" << EscapeNewlines(chrome.footerSubtitle) << "\n"
        << "rodape_cor_fundo=" << chrome.footerBackground << "\n"
        << "rodape_cor_texto=" << chrome.footerForeground << "\n";
    return out.str();
}

bool LoadTvChrome(const std::string &path, TvChrome &chrome) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(trimmed.substr(0, eq));
        std::string value = UnescapeNewlines(Trim(trimmed.substr(eq + 1)));
        if (key == "cabecalho_visivel") chrome.headerVisible = value == "1";
        else if (key == "cabecalho_texto") chrome.headerTitle = value;
        else if (key == "cabecalho_subtexto") chrome.headerSubtitle = value;
        else if (key == "cabecalho_cor_fundo") chrome.headerBackground = value;
        else if (key == "cabecalho_cor_texto") chrome.headerForeground = value;
        else if (key == "rodape_visivel") chrome.footerVisible = value == "1";
        else if (key == "rodape_texto") chrome.footerTitle = value;
        else if (key == "rodape_subtexto") chrome.footerSubtitle = value;
        else if (key == "rodape_cor_fundo") chrome.footerBackground = value;
        else if (key == "rodape_cor_texto") chrome.footerForeground = value;
    }
    return true;
}

bool SaveTextFileAtomic(const std::string &path, const std::string &text) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return false;
        out << text;
        if (!out.good()) return false;
    }
    SyncPath(tmp);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    auto slash = path.find_last_of('/');
    SyncPath(slash == std::string::npos ? "." : path.substr(0, slash));
    return true;
}

} // namespace kiosk
