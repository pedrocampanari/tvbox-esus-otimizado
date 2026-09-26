#ifndef TVBOX_SCRAPER_H
#define TVBOX_SCRAPER_H

#include <string>
#include <vector>

#include "campaign.h"

namespace kiosk {

// Busca a página de display e extrai candidatos de mídia (vídeo/imagem)
// do HTML, sem nunca usar iframe pra reprodução (requisito do projeto).
//
// LIMITAÇÃO CONHECIDA (ver docs/memory/known-issues.md): o conteúdo real
// da página é injetado client-side depois de um fetch para um backend;
// o HTML puro que este scraper enxerga normalmente só contém o estado
// "Aguardando informações". Esta classe faz a extração best-effort sobre
// o HTML que receber, para já cobrir o caso de a marcação mudar para
// incluir os dados (ex.: uma rota com SSR de dados), mas não deve ser
// considerada uma fonte confiável de conteúdo real até essa lacuna ser
// fechada.
class DisplayScraper {
public:
    explicit DisplayScraper(std::string displayUrl);

    // Busca a URL configurada e substitui `out` pela lista de campanhas
    // encontradas. Retorna false em caso de falha de rede/timeout (nesse
    // caso `out` não é modificado, para o chamador manter o conteúdo
    // anterior na tela em vez de piscar pra vazio).
    bool Refresh(std::vector<Campaign> &out) const;

    // Exposto para testes: interpreta um HTML já em memória.
    static std::vector<Campaign> ParseHtml(const std::string &html);

private:
    std::string displayUrl_;
};

} // namespace kiosk

#endif // TVBOX_SCRAPER_H
