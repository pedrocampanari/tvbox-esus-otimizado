#ifndef TVBOX_CAMPAIGN_STORE_H
#define TVBOX_CAMPAIGN_STORE_H

#include <string>
#include <vector>

#include "campaign.h"

namespace kiosk {

// Carrega uma lista FIXA de campanhas a partir de um arquivo de config
// simples (chave=valor por linha, blocos separados por uma linha
// "[campanha]" — ver config/campaigns.conf para o formato e exemplos
// reais). Usado no modo padrão (kUseLiveScraping = false, ver
// include/config.h): a lista é lida uma única vez na inicialização, sem
// nenhuma requisição à URL original.
//
// Retorna lista vazia (e não lança) se o arquivo não existir ou estiver
// mal formado; o chamador decide o que fazer nesse caso (ex.: mostrar o
// placeholder padrão).
std::vector<Campaign> LoadFixedCampaigns(const std::string &path);

} // namespace kiosk

#endif // TVBOX_CAMPAIGN_STORE_H
