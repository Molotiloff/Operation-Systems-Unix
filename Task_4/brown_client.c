#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
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
            die("read");
        }
        if (rd == 0)
        {
            break;
        }
        buf[off++] = ch;
        if (ch == '\n')
        {
            break;
        }
    }
    buf[off] = '\0';
    return (ssize_t)off;
}

int main(int argc, char *argv[])
{
    const char *config_path = "config";
    if (argc >= 2)
    {
        config_path = argv[1];
    }

    char socket_path[108];
    if (load_socket_path(config_path, socket_path, sizeof(socket_path)) < 0)
    {
        die("load socket path");
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket");
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        die("connect");
    }

    char line[128];
    char reply[128];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        write_all(fd, line, strlen(line));
        if (read_line(fd, reply, sizeof(reply)) <= 0)
        {
            break;
        }
        fputs(reply, stdout);
        fflush(stdout);
    }

    close(fd);
    return 0;
}
