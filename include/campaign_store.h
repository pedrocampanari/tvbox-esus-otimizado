#ifndef TVBOX_CAMPAIGN_STORE_H
#define TVBOX_CAMPAIGN_STORE_H

#include <string>
#include <vector>

#include "campaign.h"

namespace kiosk {

// Carrega uma lista de campanhas a partir de um arquivo de config
// simples (chave=valor por linha, blocos separados por uma linha
// "[campanha]" — ver config/campaigns.conf para o formato e exemplos
// reais). Lê a lista fixa (config/campaigns.conf) e a última lista
// recebida do site (kLiveCampaignsCachePath, ver include/config.h).
//
// Retorna lista vazia (e não lança) se o arquivo não existir ou estiver
// mal formado; o chamador decide o que fazer nesse caso (ex.: mostrar o
// placeholder padrão).
std::vector<Campaign> LoadFixedCampaigns(const std::string &path);

// Mesma lista no formato acima (lido de volta por LoadFixedCampaigns).
// Também serve pra comparar duas listas: texto igual = nada mudou.
std::string SerializeCampaigns(const std::vector<Campaign> &campaigns);

// Cabeçalho/rodapé em chave=valor (mesmo papel das duas acima). Load
// só sobrescreve os campos presentes no arquivo; false = sem arquivo.
std::string SerializeTvChrome(const TvChrome &chrome);
bool LoadTvChrome(const std::string &path, TvChrome &chrome);

// Grava `text` em `path` de forma atômica (fsync + rename): um corte de
// energia não deixa o arquivo pela metade.
bool SaveTextFileAtomic(const std::string &path, const std::string &text);

} // namespace kiosk

#endif // TVBOX_CAMPAIGN_STORE_H
