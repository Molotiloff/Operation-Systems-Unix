#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_BLOCK_SIZE 4096

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

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [-b block_size] output_file\n"
            "  %s [-b block_size] input_file output_file\n",
            prog, prog);
    exit(EXIT_FAILURE);
}

static bool is_all_zero(const unsigned char *buf, ssize_t len)
{
    for (ssize_t i = 0; i < len; ++i)
    {
        if (buf[i] != 0)
        {
            return false;
        }
    }
    return true;
}

static void write_all(int fd, const unsigned char *buf, ssize_t len)
{
    ssize_t written_total = 0;

    while (written_total < len)
    {
        ssize_t written = write(fd, buf + written_total, (size_t)(len - written_total));
        if (written < 0)
        {
            die("write");
        }
        written_total += written;
    }
}

int main(int argc, char *argv[])
{
    int opt;
    long block_size_long = DEFAULT_BLOCK_SIZE;

    while ((opt = getopt(argc, argv, "b:")) != -1)
    {
        switch (opt)
        {
        case 'b':
        {
            char *endptr = NULL;
            errno = 0;
            block_size_long = strtol(optarg, &endptr, 10);
            if (errno != 0 || endptr == optarg || *endptr != '\0' || block_size_long <= 0)
            {
                die_msg("Invalid block size");
            }
            break;
        }
        default:
            usage(argv[0]);
        }
    }

    int remaining = argc - optind;
    if (remaining != 1 && remaining != 2)
    {
        usage(argv[0]);
    }

    const char *input_name = NULL;
    const char *output_name = NULL;

    int input_fd = STDIN_FILENO;
    int output_fd = -1;

    if (remaining == 1)
    {
        output_name = argv[optind];
    }
    else
    {
        input_name = argv[optind];
        output_name = argv[optind + 1];
    }

    if (input_name != NULL)
    {
        input_fd = open(input_name, O_RDONLY);
        if (input_fd < 0)
        {
            die("open input");
        }
    }

    output_fd = open(output_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0)
    {
        if (input_name != NULL)
        {
            close(input_fd);
        }
        die("open output");
    }

    size_t block_size = (size_t)block_size_long;
    unsigned char *buf = malloc(block_size);
    if (buf == NULL)
    {
        if (input_name != NULL)
        {
            close(input_fd);
        }
        close(output_fd);
        die("malloc");
    }

    off_t pending_hole = 0;
    off_t total_size = 0;

    for (;;)
    {
        ssize_t rd = read(input_fd, buf, block_size);
        if (rd < 0)
        {
            free(buf);
            if (input_name != NULL)
            {
                close(input_fd);
            }
            close(output_fd);
            die("read");
        }

        if (rd == 0)
        {
            break;
        }

        total_size += rd;

        if (is_all_zero(buf, rd))
        {
            pending_hole += rd;
            continue;
        }

        if (pending_hole > 0)
        {
            if (lseek(output_fd, pending_hole, SEEK_CUR) == (off_t)-1)
            {
                free(buf);
                if (input_name != NULL)
                {
                    close(input_fd);
                }
                close(output_fd);
                die("lseek");
            }
            pending_hole = 0;
        }

        write_all(output_fd, buf, rd);
    }

    if (ftruncate(output_fd, total_size) < 0)
    {
        free(buf);
        if (input_name != NULL)
        {
            close(input_fd);
        }
        close(output_fd);
        die("ftruncate");
    }

    free(buf);

    if (input_name != NULL)
    {
        if (close(input_fd) < 0)
        {
            close(output_fd);
            die("close input");
        }
    }

    if (close(output_fd) < 0)
    {
        die("close output");
    }

    return EXIT_SUCCESS;
}
