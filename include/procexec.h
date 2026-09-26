#ifndef TVBOX_PROCEXEC_H
#define TVBOX_PROCEXEC_H

#include <string>
#include <vector>

namespace kiosk {

// Executa um binário (sem shell, argv explícito) e captura seu stdout.
// Usado para `curl` (scraper) e `yt-dlp -g` (player) sem linkar libcurl
// ou qualquer SDK das plataformas de vídeo — só processos pontuais.
// Retorna false se o processo não pôde ser criado, saiu com código
// diferente de zero, ou estourou o timeout.
bool RunCaptureStdout(const std::vector<std::string> &argv, int timeoutSeconds,
                      std::string &outStdout);

} // namespace kiosk

#endif // TVBOX_PROCEXEC_H
