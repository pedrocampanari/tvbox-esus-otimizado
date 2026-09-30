#include "procexec.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kiosk {

bool RunCaptureStdout(const std::vector<std::string> &argv, int timeoutSeconds,
                      std::string &outStdout, const std::atomic<bool> *cancel,
                      bool mergeStderr) {
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
        // Grupo de processos próprio (ver procexec.h): permite matar o
        // filho E os netos dele de uma vez com kill(-pid).
        setpgid(0, 0);
        // Mesmo motivo do mpv (src/player.cpp): app morreu, filho morre.
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        // Filho: stdout -> pipe, stdin/stderr descartados pro devnull.
        close(outPipe[0]);
        dup2(outPipe[1], STDOUT_FILENO);
        close(outPipe[1]);

        int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            if (!mergeStderr) dup2(devNull, STDERR_FILENO);
            close(devNull);
        }
        if (mergeStderr) dup2(STDOUT_FILENO, STDERR_FILENO);

        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &s : argv) cargv.push_back(const_cast<char *>(s.c_str()));
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        _exit(127); // execvp falhou
    }

    // Pai. setpgid dos dois lados evita a corrida de matar o grupo antes
    // do filho ter chegado a chamar setpgid ele mesmo.
    setpgid(pid, pid);
    close(outPipe[1]);

    std::string buffer;
    char chunk[4096];
    bool aborted = false;
    long deadlineMs = static_cast<long>(timeoutSeconds) * 1000L;
    long waitedMs = 0;
    const int stepMs = 100;

    struct pollfd pfd;
    pfd.fd = outPipe[0];
    pfd.events = POLLIN;

    for (;;) {
        if (cancel != nullptr && cancel->load()) {
            aborted = true;
            break;
        }
        int rc = poll(&pfd, 1, stepMs);
        if (rc < 0) {
            if (errno == EINTR) continue;
            aborted = true;
            break;
        }
        if (rc == 0) {
            waitedMs += stepMs;
            if (waitedMs >= deadlineMs) {
                aborted = true;
                break;
            }
            continue;
        }
        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(outPipe[0], chunk, sizeof(chunk));
            if (n > 0) {
                buffer.append(chunk, static_cast<size_t>(n));
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break; // EOF (processo fechou stdout) ou erro de leitura
        }
        break; // POLLERR/POLLNVAL: nada mais a ler desse pipe
    }
    close(outPipe[0]);

    if (aborted) {
        kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return false;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    outStdout = buffer;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace kiosk
