#ifndef TVBOX_CAMPAIGN_SOURCE_H
#define TVBOX_CAMPAIGN_SOURCE_H

#include <string>
#include <vector>

#include "campaign.h"

namespace kiosk {

// Lista de campanhas direto do backend do site original (Supabase, API
// REST), montada com as mesmas regras do `/display` (ver
// docs/memory/frontend-contract.md > "Fonte ao vivo"):
//   - textos "padrão" ativos de configuracoes_tv.fallback_tv primeiro;
//   - depois campanhas com ativo = true e dentro de data_inicio/data_fim,
//     ordenadas por `ordem` e então pelo id;
//   - com UBS (slug do menu): só as campanhas com exibir_em_todas ou
//     vinculadas à unidade em campanhas_unidades. Slug que não existe no
//     servidor cai na lista geral, como no site.
// Junto vêm cabeçalho/rodapé (configuracoes_tv.cabecalho_rodape_tv), com
// os mesmos padrões do site para campos ausentes.
//
// URL e chave pública (publishable, só leitura — a mesma que o site
// entrega a todo navegador) NÃO ficam no código: são lidas do bundle
// público do site na primeira consulta e de novo se o servidor recusar
// a chave (rotação). Rede só via processo `curl` (procexec), sem libcurl.
//
// Não é thread-safe: um objeto por thread.
class LiveCampaignSource {
public:
    // `displayUrl`: página /display do site (de onde sai o bundle).
    explicit LiveCampaignSource(std::string displayUrl);

    // false = falha de rede/servidor: o chamador mantém o que já tem.
    // true com `out` vazio = servidor sem nenhuma campanha ativa.
    bool Fetch(const std::string &ubsSlug, std::vector<Campaign> &out, TvChrome &outChrome);

private:
    bool Discover();
    // GET em <apiUrl_>/rest/v1/<pathAndQuery>; `outHttp` = status HTTP.
    bool Get(const std::string &pathAndQuery, std::string &outBody, int &outHttp);

    std::string displayUrl_;
    std::string apiUrl_; // sem barra no fim; vazio = ainda não descoberto
    std::string apiKey_;
};

// YouTube em qualquer formato (shorts/, youtu.be/, embed/, live/, v=)
// vira https://www.youtube.com/watch?v=<id> — a mesma chave de cache
// para o mesmo vídeo, venha de onde vier. Outras URLs voltam iguais.
std::string NormalizeYouTubeUrl(const std::string &url);

} // namespace kiosk

#endif // TVBOX_CAMPAIGN_SOURCE_H
