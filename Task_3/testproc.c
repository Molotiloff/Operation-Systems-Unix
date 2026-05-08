#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;

static void handle_term(int sig)
{
    (void)sig;
    stop = 1;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_term;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) < 0)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    while (!stop)
    {
        sleep(1);
    }

    return EXIT_SUCCESS;
}
