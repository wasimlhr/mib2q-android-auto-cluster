/* GPL-3.0-or-later.
 * Native fail-open trampoline for smartphone_integrator.
 *
 * Some MHI2Q smartphone_integrator builds do not successfully enter a shell
 * script when the script itself is configured as children.gal.exec. Keep the
 * supervised child an ARM/QNX ELF, then exec the maintained shell launcher in
 * place. If the shell cannot be entered, fall back to stock GAL so Android
 * Auto is not left in a reconnect loop.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define SHELL "/bin/sh"
#define SCRIPT "/mnt/app/root/sq5_android_auto/gal_startup.sh"
#define STOCK_GAL "/mnt/app/eso/bin/apps/gal"
#define LOG "/tmp/mib2q_aa_launcher.log"
#define MAX_ARGS 64

/*
 * Source the maintained startup script in this shell rather than asking sh to
 * open it as argv[1].  The EXIT trap is installed before the source operation.
 * A successful startup ends in exec(GAL), which replaces the shell and never
 * runs the trap.  A parse/open/runtime failure runs the trap and replaces the
 * same supervised PID with stock GAL, avoiding smartphone_integrator's retry
 * loop.  -x output and shell diagnostics are captured in LOG.
 */
#define SHELL_COMMAND \
    "trap 'rc=$?; set +u; echo launcher-shell-fallback rc=$rc >&2; " \
    "unset LD_PRELOAD SQ5_CLUSTER_PROBE SQ5_AA_UPMIX " \
    "SQ5_AA_UPMIX_MASK SQ5_AA_UPMIX_LEVEL; " \
    "exec " STOCK_GAL " \"$@\"' 0; " \
    ". " SCRIPT

static void log_event(const char *event, int value)
{
    char line[160];
    int fd, length;

    fd = open(LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    length = snprintf(line, sizeof(line), "%s pid=%ld value=%d\n",
                      event, (long)getpid(), value);
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        (void)write(fd, line, count);
    }
    (void)close(fd);
}

int main(int argc, char **argv)
{
    char *shell_argv[MAX_ARGS];
    char *gal_argv[MAX_ARGS];
    int fd, i;

    log_event("launcher-enter", argc);
    if (argc < MAX_ARGS - 4) {
        fd = open(LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            (void)dup2(fd, STDOUT_FILENO);
            (void)dup2(fd, STDERR_FILENO);
            if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
                (void)close(fd);
        }
        shell_argv[0] = (char *)"sh";
        shell_argv[1] = (char *)"-x";
        shell_argv[2] = (char *)"-c";
        shell_argv[3] = (char *)SHELL_COMMAND;
        shell_argv[4] = (char *)"gal_startup";
        for (i = 1; i < argc; ++i)
            shell_argv[i + 4] = argv[i];
        shell_argv[argc + 4] = NULL;
        execv(SHELL, shell_argv);
        log_event("shell-exec-failed", errno);
    } else {
        log_event("too-many-arguments", argc);
    }

    gal_argv[0] = (char *)STOCK_GAL;
    for (i = 1; i < argc && i < MAX_ARGS - 1; ++i)
        gal_argv[i] = argv[i];
    gal_argv[i] = NULL;
    execv(STOCK_GAL, gal_argv);
    log_event("stock-gal-exec-failed", errno);
    return 127;
}
