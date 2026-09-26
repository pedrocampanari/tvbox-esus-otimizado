#include "procexec.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kiosk {

bool RunCaptureStdout(const std::vector<std::string> &argv, int timeoutSeconds,
                      std::string &outStdout) {
    outStdout.clear();
    if (argv.empty()) return false;

    int outPipe[2];
    if (pipe(outPipe) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return false;
    }

    if (pid == 0) {
        // Filho: stdout -> pipe, stdin/stderr descartados pro devnull.
        close(outPipe[0]);
        dup2(outPipe[1], STDOUT_FILENO);
        close(outPipe[1]);

        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &s : argv) cargv.push_back(const_cast<char *>(s.c_str()));
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        _exit(127); // execvp falhou
    }

    // Pai.
    close(outPipe[1]);

    std::string buffer;
    char chunk[4096];
    bool timedOut = false;
    long deadlineMs = static_cast<long>(timeoutSeconds) * 1000L;
    long waitedMs = 0;
    const int stepMs = 100;

    struct pollfd pfd;
    pfd.fd = outPipe[0];
    pfd.events = POLLIN;

    for (;;) {
        int rc = poll(&pfd, 1, stepMs);
        if (rc > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(outPipe[0], chunk, sizeof(chunk));
            if (n > 0) {
                buffer.append(chunk, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) break; // EOF: processo fechou stdout
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) {
            waitedMs += stepMs;
            if (waitedMs >= deadlineMs) {
                timedOut = true;
                break;
            }
        }
    }
    close(outPipe[0]);

    if (timedOut) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return false;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    outStdout = buffer;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace kiosk
