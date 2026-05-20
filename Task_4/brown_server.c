#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_CLIENTS 256
#define INBUF_SIZE 8192
#define OUTBUF_SIZE 8192
#define LINE_SIZE 128

typedef struct
{
    int fd;
    char inbuf[INBUF_SIZE];
    size_t in_len;
    char outbuf[OUTBUF_SIZE];
    size_t out_len;
    size_t out_sent;
} Client;

static volatile sig_atomic_t g_stop = 0;
static FILE *g_log = NULL;
static char g_socket_path[PATH_MAX];
static long long g_state = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

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

static void log_ts(const char *kind, int fd, const char *payload)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    if (payload != NULL)
    {
        fprintf(g_log, "%s %.6f fd=%d %s\n",
                kind,
                (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0,
                fd,
                payload);
    }
    else
    {
        fprintf(g_log, "%s %.6f fd=%d\n",
                kind,
                (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0,
                fd);
    }

    fflush(g_log);
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

    if (socket_path[0] != '/')
    {
        errno = EINVAL;
        return -1;
    }

    if (strlen(socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static void client_reset(Client *c)
{
    c->fd = -1;
    c->in_len = 0;
    c->out_len = 0;
    c->out_sent = 0;
}

static void client_close(Client *c)
{
    if (c->fd >= 0)
    {
        log_ts("CLOSE", c->fd, NULL);
        close(c->fd);
    }
    client_reset(c);
}

static int append_output(Client *c, const char *text)
{
    size_t len = strlen(text);

    if (c->out_len + len > OUTBUF_SIZE)
    {
        errno = ENOBUFS;
        return -1;
    }

    memcpy(c->outbuf + c->out_len, text, len);
    c->out_len += len;
    return 0;
}

static int process_line(Client *c, const char *line)
{
    char *endptr = NULL;
    errno = 0;
    long long value = strtoll(line, &endptr, 10);
    if (errno != 0 || endptr == line || *endptr != '\0')
    {
        return -1;
    }

    g_state += value;

    char reply[LINE_SIZE];
    snprintf(reply, sizeof(reply), "%lld\n", g_state);

    char req_msg[LINE_SIZE + 32];
    char resp_msg[LINE_SIZE + 32];
    snprintf(req_msg, sizeof(req_msg), "line=%s", line);
    snprintf(resp_msg, sizeof(resp_msg), "line=%lld", g_state);

    log_ts("REQ", c->fd, req_msg);
    log_ts("RESP", c->fd, resp_msg);

    return append_output(c, reply);
}

static int consume_input(Client *c)
{
    size_t start = 0;

    for (size_t i = 0; i < c->in_len; ++i)
    {
        if (c->inbuf[i] == '\n')
        {
            size_t len = i - start;
            if (len >= LINE_SIZE)
            {
                errno = EMSGSIZE;
                return -1;
            }

            char line[LINE_SIZE];
            memcpy(line, c->inbuf + start, len);
            line[len] = '\0';

            if (process_line(c, line) < 0)
            {
                return -1;
            }

            start = i + 1;
        }
    }

    if (start > 0)
    {
        memmove(c->inbuf, c->inbuf + start, c->in_len - start);
        c->in_len -= start;
    }

    if (c->in_len == INBUF_SIZE)
    {
        errno = ENOBUFS;
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *config_path = "config";
    if (argc >= 2)
    {
        config_path = argv[1];
    }

    if (load_socket_path(config_path, g_socket_path, sizeof(g_socket_path)) < 0)
    {
        die("load socket path");
    }

    g_log = fopen("/tmp/brown_server.log", "a");
    if (g_log == NULL)
    {
        die("fopen log");
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        die("socket");
    }

    unlink(g_socket_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        die("bind");
    }

    if (listen(listen_fd, 128) < 0)
    {
        die("listen");
    }

    Client *clients = calloc(MAX_CLIENTS, sizeof(Client));
    if (clients == NULL)
    {
        die("calloc clients");
    }

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        client_reset(&clients[i]);
    }

    log_ts("SERVER_START", listen_fd, g_socket_path);

    while (!g_stop)
    {
        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (clients[i].fd >= 0)
            {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].out_sent < clients[i].out_len)
                {
                    FD_SET(clients[i].fd, &wfds);
                }
                if (clients[i].fd > maxfd)
                {
                    maxfd = clients[i].fd;
                }
            }
        }

        int ready = select(maxfd + 1, &rfds, &wfds, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            die("select");
        }

        if (FD_ISSET(listen_fd, &rfds))
        {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0)
            {
                bool stored = false;
                for (int i = 0; i < MAX_CLIENTS; ++i)
                {
                    if (clients[i].fd < 0)
                    {
                        clients[i].fd = cfd;
                        clients[i].in_len = 0;
                        clients[i].out_len = 0;
                        clients[i].out_sent = 0;
                        log_ts("ACCEPT", cfd, NULL);
                        stored = true;
                        break;
                    }
                }

                if (!stored)
                {
                    close(cfd);
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            Client *c = &clients[i];
            if (c->fd < 0)
            {
                continue;
            }

            if (FD_ISSET(c->fd, &rfds))
            {
                ssize_t rd = read(c->fd, c->inbuf + c->in_len, INBUF_SIZE - c->in_len);
                if (rd < 0)
                {
                    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        client_close(c);
                    }
                }
                else if (rd == 0)
                {
                    client_close(c);
                }
                else
                {
                    c->in_len += (size_t)rd;
                    if (consume_input(c) < 0)
                    {
                        client_close(c);
                    }
                }
            }

            if (c->fd >= 0 && c->out_sent < c->out_len && FD_ISSET(c->fd, &wfds))
            {
                ssize_t wr = write(c->fd, c->outbuf + c->out_sent, c->out_len - c->out_sent);
                if (wr < 0)
                {
                    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        client_close(c);
                    }
                }
                else
                {
                    c->out_sent += (size_t)wr;
                    if (c->out_sent == c->out_len)
                    {
                        c->out_len = 0;
                        c->out_sent = 0;
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        client_close(&clients[i]);
    }

    log_ts("SERVER_STOP", listen_fd, NULL);
    close(listen_fd);
    unlink(g_socket_path);
    fclose(g_log);
    free(clients);

    return 0;
}
