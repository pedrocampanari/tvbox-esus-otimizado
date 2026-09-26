#include "campaign_store.h"

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

} // namespace kiosk
