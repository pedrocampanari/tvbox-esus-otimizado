#ifndef TVBOX_CAMPAIGN_H
#define TVBOX_CAMPAIGN_H

#include <string>

namespace kiosk {

enum class CampaignType { Texto, Imagem, Video };

// Espelha video_origem do app original: define como resolver a URL
// reproduzível (ver docs/memory/frontend-contract.md).
enum class VideoOrigin { Upload, Direto, YouTube, Instagram, Facebook, Desconhecido };

struct Campaign {
    std::string id;
    CampaignType tipo = CampaignType::Texto;
    std::string titulo;
    std::string subtitulo;
    std::string texto;
    std::string imagem_url;
    std::string video_url;
    VideoOrigin video_origem = VideoOrigin::Desconhecido;
    int duracao_segundos = 10;
    int ordem = 0;
};

} // namespace kiosk

#endif // TVBOX_CAMPAIGN_H
