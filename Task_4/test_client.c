#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void trim_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
    {
        s[len - 1] = '\0';
        len--;
    }
}

static int load_socket_path(const char *config_path, char *socket_path, size_t cap)
{
    FILE *fp = fopen(config_path, "r");
    if (fp == NULL)
    {
        return -1;
    }

    if (fgets(socket_path, (int)cap, fp) == NULL)
    {
        fclose(fp);
        errno = EINVAL;
        return -1;
    }

    fclose(fp);
    trim_newline(socket_path);
    return 0;
}

static void write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len)
    {
        ssize_t wr = write(fd, buf + off, len - off);
        if (wr < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            die("write");
        }
        off += (size_t)wr;
    }
}

static ssize_t read_line(int fd, char *buf, size_t cap)
{
    size_t off = 0;

    while (off + 1 < cap)
    {
        char ch;
        ssize_t rd = read(fd, &ch, 1);

        if (rd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        if (rd == 0)
        {
            return -1;
        }

        buf[off++] = ch;

        if (ch == '\n')
        {
            buf[off] = '\0';
            return (ssize_t)off;
        }
    }

    buf[off] = '\0';
    return (ssize_t)off;
}

static void sleep_delay(double sec)
{
    if (sec <= 0.0)
    {
        return;
    }

    struct timespec ts;
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1000000000.0);

    if (ts.tv_nsec < 0)
    {
        ts.tv_nsec = 0;
    }

    while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
    {
    }
}

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        fprintf(stderr,
                "Usage: %s <config> <numbers_file> <delay_sec> <delay_log> <client_id>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *config_path = argv[1];
    const char *numbers_path = argv[2];
    double delay_sec = atof(argv[3]);
    const char *delay_log = argv[4];
    const char *client_id = argv[5];

    FILE *numbers = NULL;
    FILE *logf = NULL;
    int fd = -1;
    int exit_code = EXIT_SUCCESS;

    numbers = fopen(numbers_path, "r");
    if (numbers == NULL)
    {
        die("fopen numbers");
    }

    char socket_path[108];
    if (load_socket_path(config_path, socket_path, sizeof(socket_path)) < 0)
    {
        fclose(numbers);
        die("load socket path");
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        fclose(numbers);
        die("socket");
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fclose(numbers);
        close(fd);
        die("connect");
    }

    unsigned int seed = (unsigned int)(time(NULL) ^ getpid());
    int next_pause_after = (rand_r(&seed) % 255) + 1;
    int sent_since_pause = 0;
    double total_delay = 0.0;

    char reply[128];
    int ch;

    while ((ch = fgetc(numbers)) != EOF)
    {
        char c = (char)ch;
        write_all(fd, &c, 1);
        sent_since_pause++;

        if (sent_since_pause >= next_pause_after)
        {
            sleep_delay(delay_sec);
            total_delay += delay_sec;
            sent_since_pause = 0;
            next_pause_after = (rand_r(&seed) % 255) + 1;
        }

        if (c == '\n')
        {
            if (read_line(fd, reply, sizeof(reply)) <= 0)
            {
                exit_code = EXIT_SUCCESS;
                goto cleanup;
            }
        }
    }

cleanup:
    if (numbers != NULL)
    {
        fclose(numbers);
    }

    if (fd >= 0)
    {
        close(fd);
    }

    logf = fopen(delay_log, "w");
    if (logf == NULL)
    {
        die("fopen delay log");
    }

    fprintf(logf, "client=%s total_delay=%.6f\n", client_id, total_delay);
    fclose(logf);

    return exit_code;
}
