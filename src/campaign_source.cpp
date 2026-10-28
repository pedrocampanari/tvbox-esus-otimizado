#include "campaign_source.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <utility>

#include "config.h"
#include "procexec.h"
#include "raylib.h"

namespace kiosk {

namespace {

// JSON mínimo: só o que a API REST devolve (arrays de objetos rasos).
// Sem dependência externa; as respostas têm poucos KB.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json *Get(const std::string &key) const {
        for (const auto &f : fields) {
            if (f.first == key) return &f.second;
        }
        return nullptr;
    }
    std::string Str(const std::string &key) const {
        const Json *v = Get(key);
        return v && v->kind == Kind::String ? v->text : "";
    }
    double Num(const std::string &key, double fallback) const {
        const Json *v = Get(key);
        return v && v->kind == Kind::Number ? v->number : fallback;
    }
    bool Bool(const std::string &key, bool fallback) const {
        const Json *v = Get(key);
        return v && v->kind == Kind::Bool ? v->boolean : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string &s) : s_(s) {}

    bool Parse(Json &out) {
        if (!Value(out, 0)) return false;
        SkipSpace();
        return pos_ == s_.size();
    }

private:
    void SkipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' ||
                                    s_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool Literal(const char *word) {
        size_t n = std::char_traits<char>::length(word);
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    static void AppendUtf8(std::string &out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool Hex4(unsigned &out) {
        if (pos_ + 4 > s_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool String(std::string &out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) return false;
            char e = s_[pos_++];
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && Literal("\\u")) {
                        unsigned low = 0;
                        if (!Hex4(low)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out.push_back(e); break; // \" \\ \/
            }
        }
        return false;
    }

    bool Value(Json &out, int depth) {
        if (depth > 32) return false;
        SkipSpace();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '"') {
            out.kind = Json::Kind::String;
            return String(out.text);
        }
        if (c == '{') {
            ++pos_;
            out.kind = Json::Kind::Object;
            SkipSpace();
            if (pos_ < s_.size() && s_[pos_] == '}') return ++pos_, true;
            while (true) {
                SkipSpace();
                std::pair<std::string, Json> field;
                if (!String(field.first)) return false;
                SkipSpace();
                if (pos_ >= s_.size() || s_[pos_++] != ':') return false;
                if (!Value(field.second, depth + 1)) return false;
                out.fields.push_back(std::move(field));
                SkipSpace();
                if (pos_ >= s_.size()) return false;
                if (s_[pos_] == ',') { ++pos_; continue; }
                if (s_[pos_] == '}') return ++pos_, true;
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            out.kind = Json::Kind::Array;
            SkipSpace();
            if (pos_ < s_.size() && s_[pos_] == ']') return ++pos_, true;
            while (true) {
                out.items.emplace_back();
                if (!Value(out.items.back(), depth + 1)) return false;
                SkipSpace();
                if (pos_ >= s_.size()) return false;
                if (s_[pos_] == ',') { ++pos_; continue; }
                if (s_[pos_] == ']') return ++pos_, true;
                return false;
            }
        }
        if (Literal("true")) { out.kind = Json::Kind::Bool; out.boolean = true; return true; }
        if (Literal("false")) { out.kind = Json::Kind::Bool; return true; }
        if (Literal("null")) return true;
        char *end = nullptr;
        out.number = std::strtod(s_.c_str() + pos_, &end);
        if (end == s_.c_str() + pos_) return false;
        out.kind = Json::Kind::Number;
        pos_ = static_cast<size_t>(end - s_.c_str());
        return true;
    }

    const std::string &s_;
    size_t pos_ = 0;
};

// Texto entre `prefix` e o próximo `terminator`, ou vazio.
std::string Between(const std::string &s, const std::string &prefix, char terminator) {
    size_t start = s.find(prefix);
    if (start == std::string::npos) return "";
    start += prefix.size();
    size_t end = s.find(terminator, start);
    return end == std::string::npos ? "" : s.substr(start, end - start);
}

// "https://host/..." -> "https://host"
std::string OriginOf(const std::string &url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return url;
    size_t path = url.find('/', scheme + 3);
    return path == std::string::npos ? url : url.substr(0, path);
}

std::string UrlEncode(const std::string &in) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

// ISO 8601 do Postgres ("2026-10-28", "2026-10-28T03:00:00+00:00",
// "...Z", com ou sem fração) -> epoch UTC. Data sem hora = meia-noite
// UTC, como `new Date("2026-10-28")` no navegador.
bool ParseIsoUtc(const std::string &s, time_t &out) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, consumed = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2d%n", &y, &mo, &d, &consumed) != 3) return false;
    size_t pos = static_cast<size_t>(consumed);
    long offset = 0;
    if (pos < s.size() && (s[pos] == 'T' || s[pos] == ' ')) {
        int n = 0;
        if (std::sscanf(s.c_str() + pos + 1, "%2d:%2d:%2d%n", &h, &mi, &sec, &n) != 3) return false;
        pos += 1 + static_cast<size_t>(n);
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
            int oh = 0, om = 0;
            const char *tz = s.c_str() + pos + 1;
            if (std::sscanf(tz, "%2d:%2d", &oh, &om) < 1 && std::sscanf(tz, "%2d%2d", &oh, &om) < 1) {
                return false;
            }
            offset = (oh * 3600L + om * 60L) * (s[pos] == '-' ? -1 : 1);
        }
    }
    struct tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = sec;
    out = timegm(&t) - offset;
    return true;
}

// Sem RTC, o relógio do RK3229 pode subir no passado até o NTP acertar:
// aí o filtro de datas esconderia tudo. Antes de 2025 = relógio errado.
constexpr time_t kClockSaneAfter = 1735689600; // 2025-01-01 UTC

// Mesma regra de R() no bundle do site; data ilegível não restringe.
bool InDateWindow(const Json &row, time_t now) {
    time_t when = 0;
    std::string inicio = row.Str("data_inicio");
    if (!inicio.empty() && ParseIsoUtc(inicio, when) && when > now) return false;
    std::string fim = row.Str("data_fim");
    if (!fim.empty() && ParseIsoUtc(fim, when) && when < now) return false;
    return true;
}

CampaignType TipoFrom(const std::string &v) {
    if (v == "video") return CampaignType::Video;
    if (v == "imagem") return CampaignType::Imagem;
    return CampaignType::Texto;
}

VideoOrigin OrigemFrom(const std::string &v) {
    if (v == "youtube") return VideoOrigin::YouTube;
    if (v == "instagram") return VideoOrigin::Instagram;
    if (v == "facebook") return VideoOrigin::Facebook;
    if (v == "direto") return VideoOrigin::Direto;
    return VideoOrigin::Upload; // null no banco = "upload", como no site
}

// $() do bundle do site: campo ausente ou não-texto cai no padrão; cor
// vazia herda cor_fundo/cor_texto. Sem a linha no banco, tudo padrão.
TvChrome ChromeFrom(const Json *valor) {
    static const Json kEmpty;
    const Json &v = valor && valor->kind == Json::Kind::Object ? *valor : kEmpty;
    auto str = [&v](const char *key, const std::string &fallback) {
        const Json *f = v.Get(key);
        return f && f->kind == Json::Kind::String ? f->text : fallback;
    };
    auto color = [&str](const char *key, const std::string &fallback) {
        std::string c = str(key, fallback);
        return c.empty() ? fallback : c;
    };
    std::string background = color("cor_fundo", "#0d47a1");
    std::string foreground = color("cor_texto", "#ffffff");
    TvChrome chrome;
    chrome.headerVisible = v.Bool("cabecalho_visivel", true);
    chrome.headerTitle = str("cabecalho_texto", "Secretaria Municipal de Saúde");
    chrome.headerSubtitle = str("cabecalho_subtexto", "");
    chrome.headerBackground = color("cabecalho_cor_fundo", background);
    chrome.headerForeground = color("cabecalho_cor_texto", foreground);
    chrome.footerVisible = v.Bool("rodape_visivel", true);
    chrome.footerTitle = str("rodape_texto", "Três Lagoas/MS");
    chrome.footerSubtitle = str("rodape_subtexto", "");
    chrome.footerBackground = color("rodape_cor_fundo", background);
    chrome.footerForeground = color("rodape_cor_texto", foreground);
    return chrome;
}

int DurationFrom(const Json &row) {
    double d = row.Num("duracao_segundos", 0);
    return d > 0 ? static_cast<int>(d) : kDefaultSlideDurationSeconds;
}

} // namespace

std::string NormalizeYouTubeUrl(const std::string &url) {
    if (url.find("youtube.com") == std::string::npos && url.find("youtu.be") == std::string::npos) {
        return url;
    }
    static const char *const kMarkers[] = {"v=", "youtu.be/", "/shorts/", "/embed/", "/live/"};
    for (const char *marker : kMarkers) {
        size_t at = url.find(marker);
        if (at == std::string::npos) continue;
        // "v=" só como parâmetro (?v= ou &v=), não no meio de outro nome.
        if (marker[0] == 'v' && (at == 0 || (url[at - 1] != '?' && url[at - 1] != '&'))) continue;
        size_t start = at + std::char_traits<char>::length(marker);
        size_t end = start;
        while (end < url.size() && (std::isalnum(static_cast<unsigned char>(url[end])) ||
                                    url[end] == '_' || url[end] == '-')) {
            ++end;
        }
        if (end - start >= 6) return "https://www.youtube.com/watch?v=" + url.substr(start, end - start);
    }
    return url;
}

LiveCampaignSource::LiveCampaignSource(std::string displayUrl)
    : displayUrl_(std::move(displayUrl)) {}

bool LiveCampaignSource::Discover() {
    std::string html;
    if (!RunCaptureStdout({"curl", "-s", "-L", "--max-time", std::to_string(kFetchTimeoutSeconds),
                           "-A", "tvbox-esus-otimizado/1.0", displayUrl_},
                          kFetchTimeoutSeconds + 2, html)) {
        return false;
    }
    // <link rel="modulepreload" href="/assets/supabase-<hash>.js"/>
    std::string bundle = Between(html, "/assets/supabase-", '"');
    if (bundle.empty()) {
        TraceLog(LOG_WARNING, "Campanhas: bundle do Supabase nao encontrado em %s", displayUrl_.c_str());
        return false;
    }
    std::string js;
    if (!RunCaptureStdout({"curl", "-s", "-L", "--max-time", std::to_string(kFetchTimeoutSeconds * 3),
                           OriginOf(displayUrl_) + "/assets/supabase-" + bundle},
                          kFetchTimeoutSeconds * 3 + 2, js)) {
        return false;
    }
    std::string url = Between(js, "VITE_SUPABASE_URL:`", '`');
    std::string key = Between(js, "VITE_SUPABASE_ANON_KEY:`", '`');
    if (url.empty() || key.empty()) {
        TraceLog(LOG_WARNING, "Campanhas: URL/chave do Supabase nao encontradas no bundle");
        return false;
    }
    while (!url.empty() && url.back() == '/') url.pop_back();
    apiUrl_ = url;
    apiKey_ = key;
    TraceLog(LOG_WARNING, "Campanhas: backend descoberto (%s)", apiUrl_.c_str());
    return true;
}

bool LiveCampaignSource::Get(const std::string &pathAndQuery, std::string &outBody, int &outHttp) {
    std::string raw;
    // A chave é pública (o site a entrega a todo navegador); no argv do
    // curl ela só fica visível pro root do próprio aparelho.
    if (!RunCaptureStdout({"curl", "-s", "--max-time", std::to_string(kFetchTimeoutSeconds), "-H",
                           "apikey: " + apiKey_, "-w", "\n%{http_code}",
                           apiUrl_ + "/rest/v1/" + pathAndQuery},
                          kFetchTimeoutSeconds + 2, raw)) {
        return false;
    }
    size_t nl = raw.find_last_of('\n');
    if (nl == std::string::npos) return false;
    outHttp = std::atoi(raw.c_str() + nl + 1);
    outBody = raw.substr(0, nl);
    return true;
}

bool LiveCampaignSource::Fetch(const std::string &ubsSlug, std::vector<Campaign> &out,
                               TvChrome &outChrome) {
    if (apiUrl_.empty() && !Discover()) return false;

    // Chave recusada (rotação no site): redescobre uma vez e repete.
    auto getJson = [this](const std::string &pathAndQuery, Json &json) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::string body;
            int http = 0;
            if (!Get(pathAndQuery, body, http)) return false;
            if ((http == 401 || http == 403) && attempt == 0) {
                TraceLog(LOG_WARNING, "Campanhas: chave recusada (HTTP %d), relendo do site", http);
                if (!Discover()) return false;
                continue;
            }
            if (http != 200) {
                TraceLog(LOG_WARNING, "Campanhas: HTTP %d em %s", http, pathAndQuery.c_str());
                return false;
            }
            json = Json{};
            return JsonParser(body).Parse(json) && json.kind == Json::Kind::Array;
        }
        return false;
    };

    Json rows;
    if (!getJson("campanhas?select=*&ativo=eq.true&order=ordem.asc,created_at.asc", rows)) return false;

    Json settings;
    if (!getJson("configuracoes_tv?select=chave,valor&chave=in.(fallback_tv,cabecalho_rodape_tv)",
                 settings)) {
        return false;
    }
    const Json *fallbackValor = nullptr;
    const Json *chromeValor = nullptr;
    for (const Json &row : settings.items) {
        std::string chave = row.Str("chave");
        if (chave == "fallback_tv") fallbackValor = row.Get("valor");
        else if (chave == "cabecalho_rodape_tv") chromeValor = row.Get("valor");
    }

    // UBS: slug -> id -> campanhas vinculadas. Slug inexistente = geral.
    bool filterByUnit = false;
    std::set<std::string> unitCampaigns;
    if (!ubsSlug.empty()) {
        Json unit;
        if (!getJson("unidades?select=id&slug=eq." + UrlEncode(ubsSlug), unit)) return false;
        if (!unit.items.empty()) {
            Json links;
            std::string unitId = unit.items[0].Str("id");
            if (!getJson("campanhas_unidades?select=campanha_id&unidade_id=eq." + UrlEncode(unitId), links)) {
                return false;
            }
            filterByUnit = true;
            for (const Json &link : links.items) unitCampaigns.insert(link.Str("campanha_id"));
        }
    }

    time_t now = std::time(nullptr);
    bool clockSane = now >= kClockSaneAfter;

    std::vector<Campaign> padrao;
    {
        const Json *mensagens = fallbackValor ? fallbackValor->Get("mensagens") : nullptr;
        if (mensagens && mensagens->kind == Json::Kind::Array) {
            int index = 0;
            for (const Json &m : mensagens->items) {
                std::string titulo = m.Str("titulo");
                if (titulo.empty()) continue; // Fe() do site descarta
                int ordem = index++;
                if (!m.Bool("ativo", true)) continue;
                Campaign c;
                c.id = "padrao:" + std::to_string(ordem) + ":" + titulo;
                c.tipo = CampaignType::Texto;
                c.titulo = titulo;
                c.subtitulo = m.Str("subtitulo");
                c.texto = m.Str("texto");
                c.duracao_segundos = DurationFrom(m);
                c.ordem = ordem;
                padrao.push_back(std::move(c));
            }
        }
    }

    std::vector<Campaign> campanhas;
    for (const Json &row : rows.items) {
        std::string id = row.Str("id");
        if (clockSane && !InDateWindow(row, now)) continue;
        if (filterByUnit && !row.Bool("exibir_em_todas", false) && !unitCampaigns.count(id)) continue;
        Campaign c;
        c.id = "campanha:" + id;
        c.tipo = TipoFrom(row.Str("tipo"));
        c.titulo = row.Str("titulo");
        c.subtitulo = row.Str("subtitulo");
        c.texto = row.Str("texto");
        c.imagem_url = row.Str("imagem_url");
        c.video_origem = OrigemFrom(row.Str("video_origem"));
        c.video_url = row.Str("video_url");
        if (c.video_origem == VideoOrigin::YouTube) c.video_url = NormalizeYouTubeUrl(c.video_url);
        c.duracao_segundos = DurationFrom(row);
        c.ordem = static_cast<int>(row.Num("ordem", 0));
        campanhas.push_back(std::move(c));
    }

    // x() do site: cada grupo por ordem e então id; padrão antes.
    auto byOrder = [](const Campaign &a, const Campaign &b) {
        return a.ordem != b.ordem ? a.ordem < b.ordem : a.id < b.id;
    };
    std::stable_sort(padrao.begin(), padrao.end(), byOrder);
    std::stable_sort(campanhas.begin(), campanhas.end(), byOrder);

    outChrome = ChromeFrom(chromeValor);
    out = std::move(padrao);
    out.insert(out.end(), std::make_move_iterator(campanhas.begin()),
               std::make_move_iterator(campanhas.end()));
    return true;
}

} // namespace kiosk
