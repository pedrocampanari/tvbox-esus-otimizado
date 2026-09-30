#ifndef TVBOX_PROCEXEC_H
#define TVBOX_PROCEXEC_H

#include <atomic>
#include <string>
#include <vector>

namespace kiosk {

// Executa um binário (sem shell, argv explícito) e captura seu stdout.
// Usado para `curl` (scraper), `amixer` (volume) e `yt-dlp` (cache de
// vídeos) sem linkar libcurl/libasound ou qualquer SDK das plataformas
// de vídeo — só processos pontuais.
// Retorna false se o processo não pôde ser criado, saiu com código
// diferente de zero, estourou o timeout ou foi cancelado.
//
// O filho roda num grupo de processos próprio: timeout/cancelamento
// matam o grupo inteiro (ex.: `yt-dlp` + o `ffmpeg` que ele dispara pra
// juntar vídeo/áudio), sem deixar neto órfão rodando.
//
// `cancel` (opcional): se virar true enquanto o processo roda, ele é
// morto e a função retorna false em até ~100ms — usado pela thread de
// download do cache pra não segurar o encerramento do app.
//
// `mergeStderr`: captura o stderr junto com o stdout (ex.: filtros do
// ffmpeg como `blackdetect`, que só escrevem no log).
bool RunCaptureStdout(const std::vector<std::string> &argv, int timeoutSeconds,
                      std::string &outStdout, const std::atomic<bool> *cancel = nullptr,
                      bool mergeStderr = false);

} // namespace kiosk

#endif // TVBOX_PROCEXEC_H
