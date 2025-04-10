#define _GNU_SOURCE
#include <gd.h>
#include <gdfonts.h>
#include <gdfontl.h>
#include <gdfontmb.h>
#include <gdfontg.h>
#include <gdfontt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <strings.h>

#define WIDTH 400
#define HEIGHT 160
#define DEFAULT_SOCKET_PATH "/wwwFS.out/unix.captcha.sock"
#define DEFAULT_REDIS_SOCK "/wwwFS.in/u98/unix.redis.sock"
#define DEFAULT_REDIS_DB "7"
#define DEFAULT_AUTH_TTL 7200
#define FAIL_WINDOW 3600


int color6pick() {
    static int pool[6] = {0,1,2,3,4,5};
    static int count = 0;
    static int initialized = 0;

    // Initialize on first call
    if (!initialized) {
        srand(time(NULL));
        initialized = 1;
    }

    // Reshuffle when needed
    if (count % 6 == 0) {
        // Fisher-Yates shuffle
        for (int i = 5; i > 0; i--) {
            int j = rand() % (i + 1);
            int temp = pool[i];
            pool[i] = pool[j];
            pool[j] = temp;
        }
    }

    // Return next number in sequence
    return pool[count++ % 6];
}


int main(int argc, char *argv[]) {
    printf("\n");
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 6; i++) {
            printf("%d ", color6pick() );
        }
        printf("\n");
    }
    printf("\n");
}
