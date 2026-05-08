#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LOG_PATH "/tmp/myinit.log"
#define PID_PATH "/tmp/myinit.pid"

typedef struct
{
    char **argv;
    int argc;
    char *stdin_path;
    char *stdout_path;
    pid_t pid;
} ProcessEntry;

static ProcessEntry *g_entries = NULL;
static size_t g_entry_count = 0;
static char *g_config_path = NULL;
static int g_log_fd = -1;

static volatile sig_atomic_t g_got_sigchld = 0;
static volatile sig_atomic_t g_got_sighup = 0;
static volatile sig_atomic_t g_got_sigterm = 0;

static int g_reloading = 0;
static int g_shutting_down = 0;

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static int is_absolute_path(const char *path)
{
    return path != NULL && path[0] == '/';
}

static void signal_handler(int sig)
{
    if (sig == SIGCHLD)
    {
        g_got_sigchld = 1;
    }
    else if (sig == SIGHUP)
    {
        g_got_sighup = 1;
    }
    else if (sig == SIGTERM || sig == SIGINT)
    {
        g_got_sigterm = 1;
    }
}

static void write_log(const char *fmt, ...)
{
    if (g_log_fd < 0)
    {
        return;
    }

    char timebuf[64];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);

    dprintf(g_log_fd, "[%s] ", timebuf);

    va_list ap;
    va_start(ap, fmt);
    vdprintf(g_log_fd, fmt, ap);
    va_end(ap);

    dprintf(g_log_fd, "\n");
    fsync(g_log_fd);
}

static void free_entries(ProcessEntry *entries, size_t count)
{
    if (entries == NULL)
    {
        return;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].argv != NULL)
        {
            for (int j = 0; j < entries[i].argc; ++j)
            {
                free(entries[i].argv[j]);
            }
            free(entries[i].argv);
        }
        free(entries[i].stdin_path);
        free(entries[i].stdout_path);
    }

    free(entries);
}

static char *dup_string(const char *s)
{
    char *copy = strdup(s);
    if (copy == NULL)
    {
        die("strdup");
    }
    return copy;
}

static ProcessEntry parse_line(char *line, size_t lineno)
{
    ProcessEntry entry;
    memset(&entry, 0, sizeof(entry));

    char *tokens[256];
    int token_count = 0;

    char *saveptr = NULL;
    char *tok = strtok_r(line, " \t\r\n", &saveptr);
    while (tok != NULL)
    {
        if (token_count >= 256)
        {
            fprintf(stderr, "Too many tokens in config line %zu\n", lineno);
            exit(EXIT_FAILURE);
        }
        tokens[token_count++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    if (token_count < 3)
    {
        fprintf(stderr, "Invalid config line %zu: expected command, stdin and stdout\n", lineno);
        exit(EXIT_FAILURE);
    }

    int cmd_argc = token_count - 2;
    const char *stdin_path = tokens[token_count - 2];
    const char *stdout_path = tokens[token_count - 1];

    if (!is_absolute_path(tokens[0]) ||
        !is_absolute_path(stdin_path) ||
        !is_absolute_path(stdout_path))
    {
        fprintf(stderr, "All paths must be absolute in config line %zu\n", lineno);
        exit(EXIT_FAILURE);
    }

    entry.argv = calloc((size_t)cmd_argc + 1, sizeof(char *));
    if (entry.argv == NULL)
    {
        die("calloc");
    }

    entry.argc = cmd_argc;
    for (int i = 0; i < cmd_argc; ++i)
    {
        entry.argv[i] = dup_string(tokens[i]);
    }
    entry.argv[cmd_argc] = NULL;

    entry.stdin_path = dup_string(stdin_path);
    entry.stdout_path = dup_string(stdout_path);
    entry.pid = 0;

    return entry;
}

static void load_config(const char *config_path, ProcessEntry **entries_out, size_t *count_out)
{
    FILE *fp = fopen(config_path, "r");
    if (fp == NULL)
    {
        die("fopen config");
    }

    ProcessEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    size_t lineno = 0;

    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        lineno++;

        if (linelen == 0)
        {
            continue;
        }

        char *p = line;
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }

        if (*p == '\0' || *p == '\n' || *p == '#')
        {
            continue;
        }

        if (count == capacity)
        {
            size_t new_capacity = (capacity == 0) ? 4 : capacity * 2;
            ProcessEntry *tmp = realloc(entries, new_capacity * sizeof(ProcessEntry));
            if (tmp == NULL)
            {
                die("realloc");
            }
            entries = tmp;
            capacity = new_capacity;
        }

        entries[count++] = parse_line(p, lineno);
    }

    free(line);
    fclose(fp);

    *entries_out = entries;
    *count_out = count;
}

static void reopen_log(void)
{
    if (g_log_fd >= 0)
    {
        close(g_log_fd);
    }

    g_log_fd = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (g_log_fd < 0)
    {
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(g_log_fd, F_GETFD);
    if (flags >= 0)
    {
        fcntl(g_log_fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

static void write_pidfile(void)
{
    int fd = open(PID_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
    {
        die("open pidfile");
    }

    dprintf(fd, "%ld\n", (long)getpid());

    if (close(fd) < 0)
    {
        die("close pidfile");
    }
}

static void daemonize_process(void)
{
    pid_t pid;

    if (getppid() != 1)
    {
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);

        pid = fork();
        if (pid < 0)
        {
            die("fork");
        }
        if (pid > 0)
        {
            exit(EXIT_SUCCESS);
        }

        if (setsid() < 0)
        {
            die("setsid");
        }
    }

    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) < 0)
    {
        die("getrlimit");
    }

    for (int fd = 0; fd < (int)lim.rlim_max; ++fd)
    {
        close(fd);
    }

    if (chdir("/") < 0)
    {
        die("chdir");
    }

    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0)
    {
        exit(EXIT_FAILURE);
    }

    if (dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0)
    {
        exit(EXIT_FAILURE);
    }

    if (null_fd > STDERR_FILENO)
    {
        close(null_fd);
    }

    reopen_log();
    write_pidfile();
}

static pid_t spawn_child(size_t index)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        write_log("fork failed for entry %zu: %s", index, strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        int in_fd = open(g_entries[index].stdin_path, O_RDONLY);
        if (in_fd < 0)
        {
            _exit(127);
        }

        int out_fd = open(g_entries[index].stdout_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (out_fd < 0)
        {
            close(in_fd);
            _exit(127);
        }

        if (dup2(in_fd, STDIN_FILENO) < 0 ||
            dup2(out_fd, STDOUT_FILENO) < 0 ||
            dup2(out_fd, STDERR_FILENO) < 0)
        {
            close(in_fd);
            close(out_fd);
            _exit(127);
        }

        if (in_fd > STDERR_FILENO)
        {
            close(in_fd);
        }
        if (out_fd > STDERR_FILENO)
        {
            close(out_fd);
        }

        if (g_log_fd >= 0)
        {
            close(g_log_fd);
        }

        execv(g_entries[index].argv[0], g_entries[index].argv);
        _exit(127);
    }

    g_entries[index].pid = pid;
    write_log("started child index=%zu pid=%ld cmd=%s", index, (long)pid, g_entries[index].argv[0]);
    return pid;
}

static void start_all_children(void)
{
    for (size_t i = 0; i < g_entry_count; ++i)
    {
        spawn_child(i);
    }
}

static ssize_t find_entry_by_pid(pid_t pid)
{
    for (size_t i = 0; i < g_entry_count; ++i)
    {
        if (g_entries[i].pid == pid)
        {
            return (ssize_t)i;
        }
    }
    return -1;
}

static void reap_children_and_maybe_restart(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        ssize_t idx = find_entry_by_pid(pid);
        if (idx >= 0)
        {
            g_entries[idx].pid = 0;

            if (WIFEXITED(status))
            {
                write_log("child index=%zd pid=%ld exited with code=%d",
                          idx, (long)pid, WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status))
            {
                write_log("child index=%zd pid=%ld terminated by signal=%d",
                          idx, (long)pid, WTERMSIG(status));
            }
            else
            {
                write_log("child index=%zd pid=%ld finished", idx, (long)pid);
            }

            if (!g_reloading && !g_shutting_down)
            {
                write_log("restarting child index=%zd", idx);
                spawn_child((size_t)idx);
            }
        }
        else
        {
            write_log("reaped unknown child pid=%ld", (long)pid);
        }
    }
}

static void terminate_all_children(void)
{
    for (size_t i = 0; i < g_entry_count; ++i)
    {
        if (g_entries[i].pid > 0)
        {
            kill(g_entries[i].pid, SIGTERM);
        }
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        reap_children_and_maybe_restart();

        int alive = 0;
        for (size_t i = 0; i < g_entry_count; ++i)
        {
            if (g_entries[i].pid > 0)
            {
                alive = 1;
                break;
            }
        }

        if (!alive)
        {
            return;
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000L;
        nanosleep(&ts, NULL);
    }

    for (size_t i = 0; i < g_entry_count; ++i)
    {
        if (g_entries[i].pid > 0)
        {
            kill(g_entries[i].pid, SIGKILL);
        }
    }

    while (waitpid(-1, NULL, 0) > 0)
    {
    }

    for (size_t i = 0; i < g_entry_count; ++i)
    {
        g_entries[i].pid = 0;
    }
}

static void reload_config_and_restart_children(void)
{
    g_reloading = 1;
    write_log("received SIGHUP, reloading config");

    terminate_all_children();

    free_entries(g_entries, g_entry_count);
    g_entries = NULL;
    g_entry_count = 0;

    load_config(g_config_path, &g_entries, &g_entry_count);
    write_log("loaded %zu config entries", g_entry_count);

    start_all_children();
    g_reloading = 0;
}

static void install_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = signal_handler;

    if (sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        die("sigaction SIGCHLD");
    }
    if (sigaction(SIGHUP, &sa, NULL) < 0)
    {
        die("sigaction SIGHUP");
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0)
    {
        die("sigaction SIGTERM");
    }
    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        die("sigaction SIGINT");
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s /absolute/path/to/config\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!is_absolute_path(argv[1]))
    {
        die_msg("Config path must be absolute");
    }

    g_config_path = dup_string(argv[1]);

    install_handlers();
    daemonize_process();

    write_log("myinit started");
    write_log("using config: %s", g_config_path);

    load_config(g_config_path, &g_entries, &g_entry_count);
    write_log("loaded %zu config entries", g_entry_count);

    start_all_children();

    for (;;)
    {
        pause();

        if (g_got_sigterm)
        {
            g_got_sigterm = 0;
            g_shutting_down = 1;
            write_log("received termination signal, shutting down");
            terminate_all_children();
            unlink(PID_PATH);
            write_log("myinit stopped");
            close(g_log_fd);
            free_entries(g_entries, g_entry_count);
            free(g_config_path);
            return EXIT_SUCCESS;
        }

        if (g_got_sighup)
        {
            g_got_sighup = 0;
            reload_config_and_restart_children();
        }

        if (g_got_sigchld)
        {
            g_got_sigchld = 0;
            reap_children_and_maybe_restart();
        }
    }
}
