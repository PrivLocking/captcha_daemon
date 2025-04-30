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

int debug_mode = 0;
char *socket_path = DEFAULT_SOCKET_PATH;
char *redis_sock = DEFAULT_REDIS_SOCK;
char *redis_db = DEFAULT_REDIS_DB;
int auth_ttl = DEFAULT_AUTH_TTL;

static EVP_MD_CTX *mdctx = NULL;
static char randkey[33] = {0};

void print_escaped(const char *label, const char *str) {
    if (!debug_mode) return;
    printf("%s: [", label);
    for (const char *p = str; *p; p++) {
        if (*p == '\r') printf("\\r");
        else if (*p == '\n') printf("\\n");
        else putchar(*p);
    }
    printf("]\n");
}

void redis_command(int fd, const char *cmd, char *buffer, size_t buffer_size) {
    print_escaped("Redis command", cmd);
    write(fd, cmd, strlen(cmd));
    int n = read(fd, buffer, buffer_size - 1);
    if (n > 0) {
        buffer[n] = '\0';
        print_escaped("Redis reply", buffer);
    } else if (debug_mode) printf("Redis read failed, bytes read: %d, errno: [%s]\n", n, strerror(errno));
}

int redis_connect() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, redis_sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }
    char *passwd = getenv("REDISCLI_AUTH");
    if (!passwd) passwd = "default_password";
    char auth_cmd[256], buffer[1024];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\r\n", passwd);
    redis_command(fd, auth_cmd, buffer, sizeof(buffer));
    if (strstr(buffer, "+OK") == NULL) {
        close(fd);
        return -1;
    }
    snprintf(auth_cmd, sizeof(auth_cmd), "SELECT %s\r\n", redis_db);
    redis_command(fd, auth_cmd, buffer, sizeof(buffer));
    if (strstr(buffer, "+OK") == NULL) {
        close(fd);
        return -1;
    }
    return fd;
}

void redis_hset(int fd, const char *key, const char *field, const char *value) {
    char cmd[512], buffer[1024];
    snprintf(cmd, sizeof(cmd), "HSET %s %s %s\r\n", key, field, value);
    redis_command(fd, cmd, buffer, sizeof(buffer));
}

char* redis_hget(int fd, const char *key, const char *field) {
    char cmd[512], buffer[1024];
    snprintf(cmd, sizeof(cmd), "HGET %s %s\r\n", key, field);
    redis_command(fd, cmd, buffer, sizeof(buffer));
    char *value = strstr(buffer, "$");
    if (value) {
        value += 1;
        if (strcmp(value, "-1\r\n") == 0) return NULL;
        char *start = strchr(value, '\n');
        if (!start) return NULL;
        start += 1;
        char *end = strrchr(start, '\r');
        if (!end) return NULL;
        *end = '\0';
        return strdup(start);
    }
    return NULL;
}

void redis_set_ttl(int fd, const char *key, int ttl) {
    char cmd[256], buffer[1024];
    snprintf(cmd, sizeof(cmd), "EXPIRE %s %d\r\n", key, ttl);
    redis_command(fd, cmd, buffer, sizeof(buffer));
}

int redis_zcount(int fd, const char *key, time_t min, time_t max) {
    char cmd[256], buffer[1024];
    snprintf(cmd, sizeof(cmd), "ZCOUNT %s %ld %ld\r\n", key, min, max);
    redis_command(fd, cmd, buffer, sizeof(buffer));
    char *count = strstr(buffer, ":");
    if (count) return atoi(count + 1);
    return -1;
}

void static_generate_md5(char *output) {
    char urandom[32], input[72];
    FILE *urand = fopen("/dev/urandom", "r");
    if (!urand || fread(urandom, 1, 32, urand) != 32) {
        if (urand) fclose(urand);
        strcpy(output, randkey);
        return;
    }
    fclose(urand);
    time_t ts = time(NULL);
    memcpy(input, randkey, 32);
    memcpy(input + 32, urandom, 32);
    memcpy(input + 64, &ts, sizeof(ts));
    EVP_MD_CTX_reset(mdctx);
    EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
    EVP_DigestUpdate(mdctx, input, 64 + sizeof(ts));
    unsigned char digest[16];
    unsigned int digest_len;
    EVP_DigestFinal_ex(mdctx, digest, &digest_len);
    for (int i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", digest[i]);
    }
    output[32] = '\0';
    strcpy(randkey, output);
}

void generate_md5(const char *input, size_t len, char *output) {
    EVP_MD_CTX_reset(mdctx);
    EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
    EVP_DigestUpdate(mdctx, input, len);
    unsigned char digest[16];
    unsigned int digest_len;
    EVP_DigestFinal_ex(mdctx, digest, &digest_len);
    for (int i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", digest[i]);
    }
    output[32] = '\0';
}

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

void generate_captcha(gdImagePtr im, char *session_id, const char *code) {
    int bg = gdImageColorAllocate(im, 200, 255, 255);
    int fg = gdImageColorAllocate(im, 100, 100, 100);
    int colors[6] = {
        gdImageColorAllocate(im, 255, 0, 0),
        gdImageColorAllocate(im, 0, 255, 0),
        gdImageColorAllocate(im, 0, 0, 255),
        gdImageColorAllocate(im, 255, 255, 0),
        gdImageColorAllocate(im, 255, 0, 255),
        gdImageColorAllocate(im, 0, 255, 255)
    };
    gdImageFilledRectangle(im, 0, 0, WIDTH - 1, HEIGHT - 1, bg);
    for (int i = 0; i < 20; i++) {
        int line_color = colors[rand() % 6];
        gdImageLine(im, rand() % WIDTH, rand() % HEIGHT, rand() % WIDTH, rand() % HEIGHT, line_color);
    }
    for (int i = 0; i < 100; i++) {
        gdImageSetPixel(im, rand() % WIDTH, rand() % HEIGHT, colors[rand() % 6]);
    }
    char *font = "./DejaVuSans.ttf";
    int x = 10;
    int font_size = 64;
    char *err;
    for (int i = 0; i < 6; i++) {
        int colorIdx = color6pick();
        int y_offset = rand() % 70 - 10;
        char text[2] = {code[i], '\0'};
        err = gdImageStringFT(im, NULL, colors[colorIdx], font, font_size, 0.0, x, 100 + y_offset, text);
        if (err) gdImageChar(im, gdFontGetGiant(), x, 70 + y_offset, code[i], colors[colorIdx]);
        x += 60 + (rand() % 10);
    }
    x = 10;
    for (int i = 0; i < 16; i++) {
        int y_offset = 15;
        char text[2] = {session_id[i], '\0'};
        err = gdImageStringFT(im, NULL, fg, font, 30, 0.0, x,  40 + y_offset, text);
        if (err) gdImageChar(im, gdFontGetGiant(), x, 70 + y_offset, session_id[i], fg);
        x += 24 ;
    }
    x = 10;
    for (int i = 16; i < 32; i++) {
        int y_offset = 15;
        char text[2] = {session_id[i], '\0'};
        err = gdImageStringFT(im, NULL, fg, font, 30, 0.0, x, 120 + y_offset, text);
        if (err) gdImageChar(im, gdFontGetGiant(), x, 70 + y_offset, session_id[i], fg);
        x += 24 ;
    }
    for (int i = 0; i < 10; i++) {
        int circle_color = gdImageColorAllocateAlpha(im, rand() % 256, rand() % 256, rand() % 256, 64);
        int cx = rand() % WIDTH;
        int cy = rand() % HEIGHT;
        int r = 20 + rand() % 40;
        gdImageFilledEllipse(im, cx, cy, r, r, circle_color);
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) debug_mode = 1;
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) redis_db = argv[++i];
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) socket_path = argv[++i];
        else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) redis_sock = argv[++i];
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) auth_ttl = atoi(argv[++i]);
    }
    if ( 1 && debug_mode ) {
        printf("\n");
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 6; i++) {
                printf("%d ", color6pick() );
            }
            printf("\n");
        }
        printf("\n");
    }
    srand(time(NULL) ^ getpid());
    mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        if (debug_mode) printf("EVP_MD_CTX_new error\n");
        return 1;
    }
    char urandom[32], init_input[64];
    FILE *urand = fopen("/dev/urandom", "r");
    if (!urand || fread(urandom, 1, 32, urand) != 32) {
        if (urand) fclose(urand);
        if (mdctx) EVP_MD_CTX_free(mdctx);
        if (debug_mode) printf("/dev/urandom error\n");
        return 1;
    }
    fclose(urand);
    time_t ts = time(NULL);
    memcpy(init_input, urandom, 32);
    memcpy(init_input + 32, &ts, sizeof(ts));
    generate_md5(init_input, 32 + sizeof(ts), randkey);
    int redis_fd = redis_connect();
    if (redis_fd == -1) {
        if (mdctx) EVP_MD_CTX_free(mdctx);
        if (debug_mode) printf(" redis_connect error\n");
        return 1;
    }
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        close(redis_fd);
        if (mdctx) EVP_MD_CTX_free(mdctx);
        if (debug_mode) printf(" socket create error\n");
        return 1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    unlink(socket_path);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(server_fd);
        close(redis_fd);
        if (mdctx) EVP_MD_CTX_free(mdctx);
        if (debug_mode) printf(" server_fd bind error\n");
        return 1;
    }
    chmod(socket_path, 0666);
    if (listen(server_fd, 5) == -1) {
        close(server_fd);
        close(redis_fd);
        if (mdctx) EVP_MD_CTX_free(mdctx);
        if (debug_mode) printf(" server_fd listen error\n");
        return 1;
    }
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) continue;
        char request[1024];
        read(client_fd, request, sizeof(request));
        if (debug_mode) print_escaped("Received request", request);

        if (strstr(request, "GET /captcha_png")) {
            char *cookie_session = strstr(request, "captcha_session=");
            char session_id[33] = {0};
            if (cookie_session) {
                sscanf(cookie_session, "captcha_session=%32s", session_id);
                char redis_key[48];
                snprintf(redis_key, sizeof(redis_key), "captcha:%s", session_id);
                char *code = redis_hget(redis_fd, redis_key, "code");
                if (code && strlen(code) > 0) {
                    gdImagePtr im = gdImageCreateTrueColor(WIDTH, HEIGHT);
                    generate_captcha(im, session_id, code);
                    int size;
                    void *png_data = gdImagePngPtr(im, &size);
                    gdImageDestroy(im);
                    char header[256];
                    snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: image/png\r\n"
                             "Content-Length: %d\r\n"
                             "Set-Cookie: captcha_session=%s; Path=/\r\n"
                             "\r\n", size, session_id);
                    write(client_fd, header, strlen(header));
                    write(client_fd, png_data, size);
                    gdFree(png_data);
                    free(code);
                    close(client_fd);
                    continue;
                }
                if (code) free(code);
            }
            gdImagePtr im = gdImageCreateTrueColor(WIDTH, HEIGHT);
            char code[7];
            char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            for (int i = 0; i < 6; i++) code[i] = chars[rand() % strlen(chars)];
            code[6] = '\0';
            generate_captcha(im, session_id, code);
            char current_randkey[33];
            strcpy(current_randkey, randkey);
            char session_input[96];
            urand = fopen("/dev/urandom", "r");
            if (!urand || fread(urandom, 1, 32, urand) != 32) {
                close(client_fd);
                if (urand) fclose(urand);
                continue;
            }
            fclose(urand);
            ts = time(NULL);
            memcpy(session_input, current_randkey, 32);
            memcpy(session_input + 32, urandom, 32);
            memcpy(session_input + 64, &ts, sizeof(ts));
            generate_md5(session_input, 64 + sizeof(ts), session_id);
            char new_randkey[33];
            static_generate_md5(new_randkey);
            char redis_key[48];
            snprintf(redis_key, sizeof(redis_key), "captcha:%s", session_id);
            redis_hset(redis_fd, redis_key, "code", code);
            redis_set_ttl(redis_fd, redis_key, 240);
            char *referer = strstr(request, "Referer: ");
            char origin_url[256] = "/editor001/";
            if (referer) {
                referer += 9;
                char *end = strstr(referer, "\r\n");
                if (end) *end = '\0';
                strncpy(origin_url, strstr(referer, "/") ? strstr(referer, "/") : "/", sizeof(origin_url) - 1);
                origin_url[sizeof(origin_url) - 1] = '\0';
            }
            redis_hset(redis_fd, redis_key, "origin", origin_url);
            int size;
            void *png_data = gdImagePngPtr(im, &size);
            gdImageDestroy(im);
            char header[256];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: image/png\r\n"
                     "Content-Length: %d\r\n"
                     "Set-Cookie: captcha_session=%s; Path=/\r\n"
                     "\r\n", size, session_id);
            write(client_fd, header, strlen(header));
            write(client_fd, png_data, size);
            gdFree(png_data);
        } else if (strstr(request, "GET /captcha_auth")) {
            char *answer = strstr(request, "answer=");
            char *cookie = strcasestr(request, "Cookie: ");
            char sess[33] = {0};
            if (cookie) {
                char *session = strstr(cookie, "captcha_session=");
                if (session) sscanf(session, "captcha_session=%32s", sess);
                if (debug_mode) printf("Parsed session: [%s]\n", sess);
            } else {
                if (debug_mode) printf(" why no cookie ?\n" );
            }
            char response[512];
            if (answer && sess[0]) {
                answer += 7;
                char user_input[7];
                sscanf(answer, "%6s", user_input);
                if (debug_mode) printf("Parsed user_input: [%s]\n", user_input);
                char redis_key[48];
                snprintf(redis_key, sizeof(redis_key), "captcha:%s", sess);
                char *stored_code = redis_hget(redis_fd, redis_key, "code");
                char *origin_url = redis_hget(redis_fd, redis_key, "origin");
                if (debug_mode) printf("Redis stored_code: [%s], origin_url: [%s]\n", stored_code ? stored_code : "NULL", origin_url ? origin_url : "NULL");
                if (!stored_code || !origin_url) {
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 400 Bad Request\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n");
                } else {
                    ts = time(NULL);
                    char fails_key[64];
                    snprintf(fails_key, sizeof(fails_key), "captcha:%s:fails", sess);
                    int fails = redis_zcount(redis_fd, fails_key, ts - FAIL_WINDOW, ts);
                    if (fails >= 10) {
                        char cmd[256], buffer[1024];
                        snprintf(cmd, sizeof(cmd), "ZREMRANGEBYSCORE %s 0 %ld\r\n", fails_key, ts - FAIL_WINDOW);
                        redis_command(redis_fd, cmd, buffer, sizeof(buffer));
                        snprintf(response, sizeof(response),
                                 "HTTP/1.1 403 Forbidden\r\n"
                                 "Content-Length: 0\r\n"
                                 "\r\n");
                    } else if (strcasecmp(user_input, stored_code) == 0) {
                        char *existing_auth = redis_hget(redis_fd, redis_key, "auth");
                        char new_auth_token[33];
                        if (!existing_auth) {
                            char *current_randkey = malloc(33);
                            if (!current_randkey) {
                                snprintf(response, sizeof(response),
                                         "HTTP/1.1 500 Internal Server Error\r\n"
                                         "Content-Length: 0\r\n"
                                         "\r\n");
                            } else {
                                strcpy(current_randkey, randkey);
                                char auth_input[72];
                                memcpy(auth_input, current_randkey, 32);
                                memcpy(auth_input + 32, sess, 32);
                                memcpy(auth_input + 64, &ts, sizeof(ts));
                                generate_md5(auth_input, 64 + sizeof(ts), new_auth_token);
                                redis_hset(redis_fd, redis_key, "auth", new_auth_token);
                                free(current_randkey);
                            }
                        } else {
                            strncpy(new_auth_token, existing_auth, sizeof(new_auth_token));
                            new_auth_token[32] = '\0';
                            free(existing_auth);
                        }
                        redis_set_ttl(redis_fd, redis_key, auth_ttl);
                        char cmd[256], buffer[1024];
                        snprintf(cmd, sizeof(cmd), "DEL %s\r\n", fails_key);
                        redis_command(redis_fd, cmd, buffer, sizeof(buffer));
                        snprintf(response, sizeof(response),
                                 "HTTP/1.1 302 Found\r\n"
                                 "Location: %s\r\n"
                                 "Set-Cookie: captcha_auth=%s; Path=/; Max-Age=%d\r\n"
                                 "\r\n",
                                 origin_url, new_auth_token, auth_ttl);
                        if (debug_mode) printf(
                                 "HTTP/1.1 302 Found\r\n"
                                 "Location: %s\r\n"
                                 "Set-Cookie: captcha_auth=%s; Path=/; Max-Age=%d\r\n"
                                 "\r\n",
                                 origin_url, new_auth_token, auth_ttl);
                    } else {
                        char cmd[256], buffer[1024];
                        snprintf(cmd, sizeof(cmd), "ZADD %s %ld %ld\r\n", fails_key, ts, ts);
                        redis_command(redis_fd, cmd, buffer, sizeof(buffer));
                        snprintf(cmd, sizeof(cmd), "ZREMRANGEBYSCORE %s 0 %ld\r\n", fails_key, ts - FAIL_WINDOW);
                        redis_command(redis_fd, cmd, buffer, sizeof(buffer));
                        redis_set_ttl(redis_fd, fails_key, FAIL_WINDOW);
                        snprintf(response, sizeof(response),
                                 "HTTP/1.1 302 Found\r\n"
                                 "Location: /captcha_html.html?error=incorrect&origin=%s\r\n"
                                 "\r\n",
                                 origin_url);
                    }
                }
                if (stored_code) free(stored_code);
                if (origin_url) free(origin_url);
            } else {
                snprintf(response, sizeof(response),
                         "HTTP/1.1 400 Bad Request\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n");
            }
            write(client_fd, response, strlen(response));
        } else if (strstr(request, "GET /captcha_check")) {
            char *cookie = strcasestr(request, "Cookie: ");
            char sess[33] = {0}, auth_token[33] = {0}, failed_url[256] = "/captcha_html.html";
            char *failed_param = strstr(request, "failed=");
            if (failed_param) {
                failed_param += 7;
                char *end = strstr(failed_param, " ");
                if (end) *end = '\0';
                strncpy(failed_url, failed_param, sizeof(failed_url) - 1);
                failed_url[sizeof(failed_url) - 1] = '\0';
                if (failed_url[0] != '/' || strstr(failed_url, "://")) strcpy(failed_url, "/captcha_html.html");
            }
            if (cookie) {
                char *session = strstr(cookie, "captcha_session=");
                char *auth = strstr(cookie, "captcha_auth=");
                if (session) sscanf(session, "captcha_session=%32s", sess);
                if (auth) sscanf(auth, "captcha_auth=%32s", auth_token);
                if (debug_mode) printf("Parsed session: [%s], auth_token: [%s]\n", sess, auth_token);
            }
            char response[512];
            if (sess[0] && auth_token[0]) {
                char redis_key[48];
                snprintf(redis_key, sizeof(redis_key), "captcha:%s", sess);
                char *stored_auth = redis_hget(redis_fd, redis_key, "auth");
                if (debug_mode) printf("Redis stored_auth: [%s]\n", stored_auth ? stored_auth : "NULL");
                if (stored_auth && strcmp(stored_auth, auth_token) == 0) {
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Cache-Control: no-cache, no-store\r\n"
                             "Content-Length: 2\r\n"
                             "\r\n"
                             "OK");
                    if (debug_mode) printf(
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Cache-Control: no-cache, no-store\r\n"
                             "Content-Length: 2\r\n"
                             "\r\n"
                             "OK");
                } else {
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 302 Found\r\n"
                             "Location: %s\r\n"
                             "\r\n",
                             failed_url);
                    if (debug_mode) printf(
                             "HTTP/1.1 302 Found\r\n"
                             "Location: %s\r\n"
                             "\r\n",
                             failed_url);
                }
                if (stored_auth) free(stored_auth);
            } else {
                snprintf(response, sizeof(response),
                         "HTTP/1.1 302 Found\r\n"
                         "Location: %s\r\n"
                         "\r\n",
                         failed_url);
                if (debug_mode) printf(
                         "HTTP/1.1 302 Found\r\n"
                         "Location: %s\r\n"
                         "\r\n",
                         failed_url);
            }
            write(client_fd, response, strlen(response));
        }
        close(client_fd);
    }
    close(server_fd);
    close(redis_fd);
    unlink(socket_path);
    if (mdctx) EVP_MD_CTX_free(mdctx);
    return 0;
}
