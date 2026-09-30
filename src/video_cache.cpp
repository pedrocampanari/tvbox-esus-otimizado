#include "video_cache.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "procexec.h"
#include "video_config.h"

namespace kiosk {

namespace {

double MonotonicSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Nome de arquivo estável por URL (FNV-1a 64 bits) — std::hash não
// serve: não é garantido igual entre builds, e o manifest sobrevive a
// atualizações do binário.
std::string KeyForUrl(const std::string &url) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : url) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string ReadBootId() {
    std::ifstream in("/proc/sys/kernel/random/boot_id");
    std::string id;
    std::getline(in, id);
    return id.empty() ? "unknown-boot" : id;
}

// Arquivo regular e NÃO vazio: depois de um corte de energia, arquivos
// gravados pouco antes podem voltar com 0 byte (visto no RK3229 em
// 2026-09-30 — a eMMC ainda não tinha recebido os dados).
bool FileExists(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

// Força os dados de `path` (arquivo ou diretório) até a flash.
void SyncPath(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return;
    fsync(fd);
    close(fd);
}

long long FileSize(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : 0;
}

void MakeDirs(const std::string &path) {
    std::string partial;
    std::istringstream parts(path);
    std::string part;
    if (!path.empty() && path[0] == '/') partial = "/";
    while (std::getline(parts, part, '/')) {
        if (part.empty()) continue;
        partial += part + "/";
        mkdir(partial.c_str(), 0755);
    }
}

long long FreeBytes(const std::string &dir) {
    struct statvfs vfs{};
    if (statvfs(dir.c_str(), &vfs) != 0) return -1;
    return static_cast<long long>(vfs.f_bavail) * static_cast<long long>(vfs.f_frsize);
}

std::string Basename(const std::string &path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Trim(const std::string &s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// yt-dlp NUNCA vem do apt (ver install.sh): prefere o do pip --user,
// que pode não estar no PATH herdado pelo X/exec.sh.
std::string FindYtdlp() {
    const char *home = std::getenv("HOME");
    if (home != nullptr) {
        std::string local = std::string(home) + "/.local/bin/yt-dlp";
        if (access(local.c_str(), X_OK) == 0) return local;
    }
    return "yt-dlp";
}

void Log(const char *fmt, const std::string &a, const std::string &b = "") {
    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(stderr, "%s VideoCache: ", ts);
    std::fprintf(stderr, fmt, a.c_str(), b.c_str());
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

constexpr const char *kManifestName = "manifest.tsv";
constexpr const char *kPartialPrefix = ".dl-";

} // namespace

VideoCache::VideoCache(std::string cacheDir)
    : cacheDir_(std::move(cacheDir)), bootId_(ReadBootId()), ytdlpPath_(FindYtdlp()) {
    MakeDirs(cacheDir_);
    std::lock_guard<std::mutex> lock(mutex_);
    LoadManifestLocked();
}

VideoCache::~VideoCache() {
    stop_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void VideoCache::SetCampaigns(const std::vector<Campaign> &campaigns) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        order_.clear();
        std::set<std::string> wanted;
        for (const auto &c : campaigns) {
            bool isVideo = c.tipo == CampaignType::Video && !c.video_url.empty() &&
                           c.video_url != "TODO";
            if (!isVideo) {
                order_.push_back("");
                continue;
            }
            std::string key = KeyForUrl(c.video_url);
            order_.push_back(key);
            wanted.insert(key);
            Entry &e = entries_[key];
            e.url = c.video_url;
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            it = wanted.count(it->first) ? std::next(it) : entries_.erase(it);
        }
        if (currentIndex_ >= order_.size()) currentIndex_ = 0;
        // Com a thread já rodando, um .dl-* pode ser o download em
        // andamento — só a própria thread limpa restos de tentativa.
        RemoveOrphanFilesLocked(!worker_.joinable());
        SaveManifestLocked();
    }
    if (!worker_.joinable()) {
        worker_ = std::thread(&VideoCache::WorkerLoop, this);
    }
    wake_.notify_all();
}

void VideoCache::SetCurrentIndex(size_t index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (currentIndex_ == index) return;
        currentIndex_ = index;
    }
    wake_.notify_all();
}

std::string VideoCache::LocalPathFor(const std::string &videoUrl) const {
    std::string path;
    double playEnd = 0;
    return LocalFileFor(videoUrl, path, playEnd) ? path : "";
}

bool VideoCache::LocalFileFor(const std::string &videoUrl, std::string &outPath,
                              double &outPlayEndSeconds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(KeyForUrl(videoUrl));
    if (it == entries_.end() || it->second.fileName.empty()) return false;
    std::string path = cacheDir_ + "/" + it->second.fileName;
    if (!FileExists(path)) return false;
    outPath = path;
    outPlayEndSeconds = it->second.analyzed ? it->second.playEnd : 0.0;
    return true;
}

void VideoCache::RecordShown(const Campaign &campaign) const {
    bool local = !LocalPathFor(campaign.video_url).empty();
    Log("exibido ate o fim: \"%s\" (%s)", campaign.titulo, local ? "cache local" : "streaming");
}

std::string VideoCache::PickNextLocked(double now, double &outWaitSeconds,
                                       bool &outAnalyzeOnly) const {
    outWaitSeconds = 60.0; // nada pendente: só acorda por SetCampaigns/SetCurrentIndex
    outAnalyzeOnly = false;
    size_t n = order_.size();
    for (size_t step = 1; step <= n; ++step) {
        const std::string &key = order_[(currentIndex_ + step) % n];
        if (key.empty()) continue;
        auto it = entries_.find(key);
        if (it == entries_.end()) continue;
        const Entry &e = it->second;
        if (!e.fileName.empty() && e.bootId == bootId_) {
            // Já fresco neste boot; falta só medir o preto final?
            if (!e.analyzed && e.nextAttemptAt <= now) {
                outAnalyzeOnly = true;
                return key;
            }
            continue;
        }
        if (e.nextAttemptAt > now) {
            outWaitSeconds = std::min(outWaitSeconds, e.nextAttemptAt - now);
            continue;
        }
        return key;
    }
    return "";
}

void VideoCache::WorkerLoop() {
    bool warnedLowDisk = false;
    while (!stop_.load()) {
        std::string key;
        std::string url;
        std::string existingFile;
        bool analyzeOnly = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            double wait = 0;
            key = PickNextLocked(MonotonicSeconds(), wait, analyzeOnly);
            if (key.empty()) {
                wake_.wait_for(lock, std::chrono::duration<double>(wait),
                               [this] { return stop_.load(); });
                continue;
            }
            url = entries_[key].url;
            existingFile = entries_[key].fileName;
        }

        if (analyzeOnly) {
            // Arquivo já em disco (ex.: baixado antes desta versão): só
            // mede o preto final, sem baixar de novo.
            double playEnd = MeasurePlayEnd(cacheDir_ + "/" + existingFile);
            if (stop_.load()) break;
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(key);
            if (it != entries_.end() && it->second.fileName == existingFile) {
                it->second.analyzed = true;
                it->second.playEnd = playEnd;
                SaveManifestLocked();
                Log("analisado: %s (%s)", url,
                    playEnd > 0 ? "preto final cortado em " + std::to_string(playEnd) + "s"
                                : std::string("sem preto final"));
            }
            continue;
        }

        long long freeBytes = FreeBytes(cacheDir_);
        if (freeBytes >= 0 && freeBytes < kVideoCacheMinFreeBytes) {
            if (!warnedLowDisk) {
                Log("pouco espaco livre em %s; downloads pausados (tentando de novo a cada 10min)",
                    cacheDir_);
                warnedLowDisk = true;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(key);
            if (it != entries_.end()) it->second.nextAttemptAt = MonotonicSeconds() + 600.0;
            continue;
        }
        warnedLowDisk = false;

        Log("baixando %s", url);
        std::string fileName;
        long long bytes = 0;
        bool ok = DownloadOne(key, url, fileName, bytes);
        if (stop_.load()) break;
        double playEnd = ok ? MeasurePlayEnd(cacheDir_ + "/" + fileName) : 0.0;
        if (stop_.load()) break;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            // Saiu da lista durante o download: descarta o arquivo novo.
            if (ok) unlink((cacheDir_ + "/" + fileName).c_str());
            continue;
        }
        Entry &e = it->second;
        if (ok) {
            if (!e.fileName.empty() && e.fileName != fileName) {
                // Se o mpv estiver tocando o arquivo antigo, ele segue
                // com o inode aberto até terminar — unlink é seguro.
                unlink((cacheDir_ + "/" + e.fileName).c_str());
            }
            e.fileName = fileName;
            e.bootId = bootId_;
            e.bytes = bytes;
            e.downloadedAt = static_cast<long long>(std::time(nullptr));
            e.analyzed = true;
            e.playEnd = playEnd;
            e.failures = 0;
            e.nextAttemptAt = 0;
            SaveManifestLocked();
            Log("pronto: %s (%s)", url,
                std::to_string(bytes) + " bytes" +
                    (playEnd > 0 ? ", preto final cortado em " + std::to_string(playEnd) + "s"
                                 : ""));
        } else {
            e.failures++;
            // 30s, 60s, 120s... até 10min: rede fora no boot não pode
            // virar um loop de yt-dlp consumindo CPU sem parar.
            double backoff = std::min(600.0, 30.0 * (1 << std::min(e.failures - 1, 5)));
            e.nextAttemptAt = MonotonicSeconds() + backoff;
            Log("falha ao baixar %s (tentando de novo em %ss)", url,
                std::to_string(static_cast<int>(backoff)));
        }
    }
}

bool VideoCache::DownloadOne(const std::string &key, const std::string &url,
                             std::string &outFileName, long long &outBytes) {
    const std::string partialBase = cacheDir_ + "/" + kPartialPrefix + key;

    std::vector<std::string> argv;
    // Prioridade mínima de CPU e de disco: o download nunca pode
    // disputar com o mpv decodificando nem com o Chromium.
    if (access("/usr/bin/nice", X_OK) == 0) argv.insert(argv.end(), {"nice", "-n", "19"});
    if (access("/usr/bin/ionice", X_OK) == 0) argv.insert(argv.end(), {"ionice", "-c", "3"});
    argv.insert(argv.end(), {
        ytdlpPath_,
        "-f", kYtdlFormatSelector, // mesmo seletor do streaming: H.264 <=720p, sem perda de qualidade
        "--merge-output-format", "mp4",
        "--no-playlist",
        "--no-mtime",
        "--no-progress",
        "--no-warnings",
        "--socket-timeout", "30",
        "--retries", "5",
        "-o", partialBase + ".%(ext)s",
        "--no-simulate",
        "--print", "after_move:filepath",
        url,
    });

    std::string out;
    bool ok = RunCaptureStdout(argv, kVideoDownloadTimeoutSeconds, out, &stop_);

    std::string printed;
    std::istringstream lines(out);
    for (std::string line; std::getline(lines, line);) {
        line = Trim(line);
        if (!line.empty()) printed = line;
    }

    std::string downloadedName = Basename(printed);
    std::string downloadedPath = cacheDir_ + "/" + downloadedName;
    if (!ok || downloadedName.rfind(kPartialPrefix, 0) != 0 || !FileExists(downloadedPath)) {
        std::lock_guard<std::mutex> lock(mutex_);
        RemoveOrphanFilesLocked(true); // limpa .part/.ytdl/restos desta tentativa
        return false;
    }

    auto dot = downloadedName.find_last_of('.');
    std::string ext = dot == std::string::npos ? ".mp4" : downloadedName.substr(dot);
    // Nome final muda a cada download (sufixo = boot atual): o arquivo
    // antigo pode estar aberto pelo mpv e é apagado só depois.
    outFileName = key + "-" + bootId_.substr(0, 8) + ext;
    if (rename(downloadedPath.c_str(), (cacheDir_ + "/" + outFileName).c_str()) != 0) {
        unlink(downloadedPath.c_str());
        return false;
    }
    // Dados + entrada de diretório na flash ANTES de o manifest apontar
    // pra este arquivo (ver FileExists: corte de energia).
    SyncPath(cacheDir_ + "/" + outFileName);
    SyncPath(cacheDir_);
    outBytes = FileSize(cacheDir_ + "/" + outFileName);
    return outBytes > 0;
}

double VideoCache::MeasurePlayEnd(const std::string &path) {
    std::string out;
    if (!RunCaptureStdout({"ffprobe", "-v", "error", "-show_entries", "format=duration", "-of",
                           "csv=p=0", path},
                          60, out, &stop_)) {
        return 0.0;
    }
    double duration = std::atof(out.c_str());
    if (duration <= 0) return 0.0;

    // Só o trecho final é decodificado (~30s; alguns vídeos têm preto
    // mais longo — aí amplia a janela e mede de novo). Prioridade mínima
    // de CPU, igual ao download.
    for (double window = 30.0;; window *= 4) {
        window = std::min(window, duration);
        double windowStart = duration - window;
        std::vector<std::string> argv;
        if (access("/usr/bin/nice", X_OK) == 0) argv.insert(argv.end(), {"nice", "-n", "19"});
        argv.insert(argv.end(), {"ffmpeg", "-hide_banner", "-nostats", "-ss",
                                 std::to_string(windowStart), "-i", path, "-an", "-sn", "-vf",
                                 "blackdetect=d=0.5:pix_th=0.10", "-f", "null", "-"});
        if (!RunCaptureStdout(argv, 300, out, &stop_, /*mergeStderr=*/true)) return 0.0;

        // Último intervalo preto informado (tempos relativos ao -ss).
        double blackStart = -1;
        double blackEnd = -1;
        size_t pos = 0;
        while ((pos = out.find("black_start:", pos)) != std::string::npos) {
            blackStart = std::atof(out.c_str() + pos + 12);
            auto endPos = out.find("black_end:", pos);
            blackEnd = endPos == std::string::npos ? -1 : std::atof(out.c_str() + endPos + 10);
            pos += 12;
        }
        if (blackStart < 0 || blackEnd < 0) return 0.0;
        // Preto que não vai até o fim do arquivo não é "preto final".
        if (windowStart + blackEnd < duration - 0.5) return 0.0;
        // Preto cobrindo a janela inteira: começa antes dela — amplia.
        if (blackStart < 0.5 && window < duration) continue;
        // +0,2s: deixa o finalzinho do fade-out, sem cortar seco.
        double playEnd = windowStart + blackStart + 0.2;
        return playEnd >= 3.0 ? playEnd : 0.0; // vídeo "todo preto": não mexe
    }
}

void VideoCache::LoadManifestLocked() {
    std::ifstream in(cacheDir_ + "/" + kManifestName);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::istringstream fields(line);
        for (std::string col; std::getline(fields, col, '\t');) cols.push_back(col);
        if (cols.size() < 6) continue;
        // FileExists também descarta arquivo de 0 byte (corte de energia).
        if (!FileExists(cacheDir_ + "/" + cols[1])) continue;
        Entry e;
        e.fileName = cols[1];
        e.bootId = cols[2];
        e.bytes = std::atoll(cols[3].c_str());
        e.downloadedAt = std::atoll(cols[4].c_str());
        e.url = cols[5];
        if (cols.size() >= 8) { // colunas novas (2026-09-30); antigas: analisa depois
            e.analyzed = cols[6] == "1";
            e.playEnd = std::atof(cols[7].c_str());
        }
        entries_[cols[0]] = e;
    }
}

void VideoCache::SaveManifestLocked() const {
    const std::string path = cacheDir_ + "/" + kManifestName;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        out << "# chave\tarquivo\tboot_id\tbytes\tbaixado_em_unix\turl\tanalisado\tfim_util_s\n";
        for (const auto &kv : entries_) {
            const Entry &e = kv.second;
            if (e.fileName.empty()) continue;
            out << kv.first << '\t' << e.fileName << '\t' << e.bootId << '\t' << e.bytes << '\t'
                << e.downloadedAt << '\t' << e.url << '\t' << (e.analyzed ? 1 : 0) << '\t'
                << e.playEnd << '\n';
        }
    }
    // fsync antes do rename: sem isso um corte de energia pode deixar o
    // manifest novo com 0 byte (a flash não recebeu os dados ainda).
    SyncPath(tmp);
    rename(tmp.c_str(), path.c_str());
    SyncPath(cacheDir_);
}

void VideoCache::RemoveOrphanFilesLocked(bool includePartials) const {
    std::set<std::string> keep = {kManifestName};
    for (const auto &kv : entries_) {
        if (!kv.second.fileName.empty()) keep.insert(kv.second.fileName);
    }
    DIR *dir = opendir(cacheDir_.c_str());
    if (dir == nullptr) return;
    while (dirent *ent = readdir(dir)) {
        std::string name = ent->d_name;
        if (name == "." || name == ".." || keep.count(name)) continue;
        if (!includePartials && name.rfind(kPartialPrefix, 0) == 0) continue;
        unlink((cacheDir_ + "/" + name).c_str());
    }
    closedir(dir);
}

} // namespace kiosk
