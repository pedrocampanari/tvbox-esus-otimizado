#ifndef TVBOX_VIDEO_CACHE_H
#define TVBOX_VIDEO_CACHE_H

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "campaign.h"

// Cache local dos vídeos (disco, nunca RAM/tmpfs — só 2GB no RK3229).
//
// Regras (pedido do usuário em 2026-09-29, ver
// docs/memory/architecture.md > "Cache de vídeos"):
//   - Cada vídeo é baixado UMA vez por boot do dispositivo (chave:
//     /proc/sys/kernel/random/boot_id). Reinício só do app (crash +
//     exec.sh) NÃO baixa de novo.
//   - Download é incremental, numa única thread em segundo plano, nunca
//     bloqueia o loop de desenho: enquanto um vídeo toca, baixa o
//     PRÓXIMO da rotação que ainda não está pronto neste boot.
//   - Arquivo de um boot anterior continua sendo usado (tocado do disco)
//     até a versão nova deste boot terminar de baixar — sem rede no
//     boot, o kiosk continua funcionando com o que já tinha.
//   - Vídeo sem nenhum arquivo local ainda cai no streaming direto do
//     mpv (comportamento antigo) — só na primeira volta após instalar.
//   - "Registro": manifest.tsv no diretório do cache (chave, arquivo,
//     boot_id, tamanho, data, url, analisado, fim_util), reescrito
//     atomicamente (fsync + rename) a cada download/análise concluído.
//   - Preto no final: vários vídeos do YouTube terminam com 2-8s+ de
//     tela preta no próprio arquivo. Depois de baixar, a thread mede isso
//     (`blackdetect` do ffmpeg, só no trecho final) e grava o "fim útil";
//     o player para ali (opção `end` do mpv) — sem recodificar nada.
//
// Não inclui raylib.h nem Xlib.h (mesma regra de isolamento de headers
// do player — ver docs/memory/architecture.md).
namespace kiosk {

class VideoCache {
public:
    explicit VideoCache(std::string cacheDir);
    ~VideoCache();

    VideoCache(const VideoCache &) = delete;
    VideoCache &operator=(const VideoCache &) = delete;

    // Define a rotação atual (só as campanhas de vídeo com URL válida
    // importam) e, na primeira chamada, sobe a thread de download.
    // Pode ser chamada de novo se a lista mudar (modo de scraping ao
    // vivo) — arquivos de vídeos que saíram da lista são apagados.
    void SetCampaigns(const std::vector<Campaign> &campaigns);

    // Índice (na mesma lista passada a SetCampaigns) do slide exibido
    // agora. A thread prioriza o próximo vídeo depois deste.
    void SetCurrentIndex(size_t index);

    // Caminho do arquivo local pronto pra tocar (deste boot ou de um
    // anterior), ou string vazia se ainda não houver nenhum.
    std::string LocalPathFor(const std::string &videoUrl) const;

    // Igual a LocalPathFor, e também o "fim útil" em segundos (onde começa
    // o preto final do arquivo), ou 0 = tocar até o fim / ainda não
    // analisado.
    bool LocalFileFor(const std::string &videoUrl, std::string &outPath,
                      double &outPlayEndSeconds) const;

    // Loga que o vídeo foi exibido até o fim (kiosk.log).
    void RecordShown(const Campaign &campaign) const;

private:
    struct Entry {
        std::string url;
        std::string fileName; // relativo a cacheDir_; vazio = nada em disco
        std::string bootId;   // boot em que o arquivo foi baixado
        long long bytes = 0;
        long long downloadedAt = 0;
        bool analyzed = false;    // preto final já medido
        double playEnd = 0;       // segundos; 0 = até o fim do arquivo
        int failures = 0;         // neste boot
        double nextAttemptAt = 0; // relógio monotônico, em segundos
    };

    void WorkerLoop();
    bool DownloadOne(const std::string &key, const std::string &url, std::string &outFileName,
                     long long &outBytes);
    // Próxima chave a processar (ou vazia), em ordem de rotação a partir
    // do slide atual: baixar (não fresca neste boot) ou só analisar o
    // preto final (`outAnalyzeOnly`, arquivo já em disco). Chamar com
    // mutex_ travado.
    std::string PickNextLocked(double now, double &outWaitSeconds, bool &outAnalyzeOnly) const;
    // Fim útil (segundos) do arquivo: onde começa o preto final, ou 0.
    double MeasurePlayEnd(const std::string &path);
    void LoadManifestLocked();
    void SaveManifestLocked() const;
    // Apaga do diretório tudo que não é manifest nem arquivo de uma
    // entrada atual. `includePartials`: também os .dl-* (downloads
    // incompletos).
    void RemoveOrphanFilesLocked(bool includePartials) const;

    std::string cacheDir_;
    std::string bootId_;
    std::string ytdlpPath_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, Entry> entries_; // chave = hash da URL
    std::vector<std::string> order_;       // chave por índice de slide ("" = slide sem vídeo)
    size_t currentIndex_ = 0;

    std::atomic<bool> stop_{false};
    std::thread worker_;
};

} // namespace kiosk

#endif // TVBOX_VIDEO_CACHE_H
