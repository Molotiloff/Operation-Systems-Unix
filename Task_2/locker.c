#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    stop = 1;
}

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void sleep_random_us(useconds_t min_us, useconds_t max_us)
{
    useconds_t delay;

    if (max_us <= min_us)
    {
        delay = min_us;
    }
    else
    {
        delay = min_us + (useconds_t)(rand() % (int)(max_us - min_us + 1));
    }

    struct timespec ts;
    ts.tv_sec = delay / 1000000;
    ts.tv_nsec = (delay % 1000000) * 1000;

    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        die("sigaction");
    }

    const char *file = argv[1];

    char lockfile[512];
    if (snprintf(lockfile, sizeof(lockfile), "%s.lck", file) >= (int)sizeof(lockfile))
    {
        fprintf(stderr, "Lock file name is too long\n");
        return EXIT_FAILURE;
    }

    pid_t pid = getpid();
    unsigned int seed = (unsigned int)(time(NULL) ^ pid);
    srand(seed);

    unsigned long success_count = 0;

    while (!stop)
    {
        int fd;

        while (1)
        {
            fd = open(lockfile, O_CREAT | O_EXCL | O_WRONLY, 0644);

            if (fd >= 0)
            {
                break;
            }

            if (errno != EEXIST)
            {
                die("open lockfile");
            }

            if (stop)
            {
                break;
            }

            sleep_random_us(5000, 20000);
        }

        if (stop)
        {
            break;
        }

        char pid_buf[64];
        int len = snprintf(pid_buf, sizeof(pid_buf), "%ld\n", (long)pid);
        if (len < 0 || len >= (int)sizeof(pid_buf))
        {
            close(fd);
            fprintf(stderr, "Failed to format PID\n");
            return EXIT_FAILURE;
        }

        ssize_t written = write(fd, pid_buf, (size_t)len);
        if (written != len)
        {
            close(fd);
            die("write pid");
        }

        if (close(fd) < 0)
        {
            die("close lockfile");
        }

        sleep(1);

        int check_fd = open(lockfile, O_RDONLY);
        if (check_fd < 0)
        {
            fprintf(stderr, "ERROR: lock file disappeared unexpectedly\n");
            return EXIT_FAILURE;
        }

        char owner_buf[64];
        ssize_t rd = read(check_fd, owner_buf, sizeof(owner_buf) - 1);
        if (rd < 0)
        {
            close(check_fd);
            die("read lockfile");
        }

        owner_buf[rd] = '\0';

        if (close(check_fd) < 0)
        {
            die("close check_fd");
        }

        long owner_pid = strtol(owner_buf, NULL, 10);
        if (owner_pid != (long)pid)
        {
            fprintf(stderr, "ERROR: lock ownership lost (expected %ld, got %ld)\n",
                    (long)pid, owner_pid);
            return EXIT_FAILURE;
        }

        if (unlink(lockfile) < 0)
        {
            die("unlink");
        }

        success_count++;

        sleep_random_us(1000, 10000);
    }

    int stat_fd = open("stats.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (stat_fd < 0)
    {
        die("open stats.txt");
    }

    char stat_buf[128];
    int stat_len = snprintf(stat_buf, sizeof(stat_buf),
                            "PID %ld: %lu locks\n",
                            (long)pid, success_count);
    if (stat_len < 0 || stat_len >= (int)sizeof(stat_buf))
    {
        close(stat_fd);
        fprintf(stderr, "Failed to format stats\n");
        return EXIT_FAILURE;
    }

    ssize_t stat_written = write(stat_fd, stat_buf, (size_t)stat_len);
    if (stat_written != stat_len)
    {
        close(stat_fd);
        die("write stats");
    }

    if (close(stat_fd) < 0)
    {
        die("close stats.txt");
    }

    return EXIT_SUCCESS;
}
