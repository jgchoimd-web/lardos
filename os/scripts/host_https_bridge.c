/*
 * host_https_bridge - HTTP→HTTPS bridge for lard OS in QEMU.
 * Replaces host_https_bridge.py. No Python required.
 *
 * Run on host (not inside lard). Kernel speaks HTTP; this fetches HTTPS.
 *
 * Usage: host_https_bridge [-p port] [-b bind]
 * In lard: http://10.0.2.2:8765/?url=https://example.com/
 *
 * Requires: curl (for HTTPS fetch)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")
#define close closesocket
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define DEFAULT_PORT 8765
#define MAX_BODY (256 * 1024)
#define BUF_SIZE 8192

#if defined(_WIN32) && !defined(strncasecmp)
#define strncasecmp _strnicmp
#endif

static int parse_url_param(const char* req, size_t len, char* out, size_t out_cap) {
    const char* q = memchr(req, '?', len);
    if (!q) return -1;
    q++;
    const char* url = "url=";
    size_t ul = strlen(url);
    while (q + ul < req + len) {
        if (strncasecmp(q, url, ul) == 0) {
            q += ul;
            size_t i = 0;
            while (q < req + len && *q != ' ' && *q != '\r' && *q != '\n' && *q != '&' && i + 1 < out_cap) {
                if (*q == '%' && q + 2 < req + len) {
                    int a = q[1], b = q[2];
                    if (a >= '0' && a <= '9') a -= '0';
                    else if (a >= 'A' && a <= 'F') a -= 'A' - 10;
                    else if (a >= 'a' && a <= 'f') a -= 'a' - 10;
                    else break;
                    if (b >= '0' && b <= '9') b -= '0';
                    else if (b >= 'A' && b <= 'F') b -= 'A' - 10;
                    else if (b >= 'a' && b <= 'f') b -= 'a' - 10;
                    else break;
                    out[i++] = (char)((a << 4) | b);
                    q += 3;
                } else {
                    out[i++] = *q++;
                }
            }
            out[i] = '\0';
            return 0;
        }
        q++;
    }
    return -1;
}

static void url_escape_for_shell(const char* url, char* buf, size_t cap) {
    size_t j = 0;
    for (; *url && j + 4 < cap; url++) {
        if (*url == '\'' || *url == '\\' || *url == '"' || *url == '`' || *url == '$') {
            buf[j++] = '\\';
        }
        buf[j++] = *url;
    }
    buf[j] = '\0';
}

int main(int argc, char** argv) {
    int port = DEFAULT_PORT;
    const char* bind_addr = "0.0.0.0";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) bind_addr = argv[++i];
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (strcmp(bind_addr, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, bind_addr, &addr.sin_addr);
    }

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }
    if (listen(s, 5) < 0) {
        perror("listen");
        close(s);
        return 1;
    }

    printf("HTTPS bridge listening on http://%s:%d/\n", bind_addr, port);
    printf("In lard (QEMU), try URL:\n  http://10.0.2.2:%d/?url=https://example.com/\n", port);

    for (;;) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int c = accept(s, (struct sockaddr*)&client, &clen);
        if (c < 0) continue;

        char req[BUF_SIZE];
        int n = 0;
        while (n < (int)sizeof(req) - 1) {
            int r = recv(c, req + n, 1, 0);
            if (r <= 0) break;
            n += r;
            if (n >= 4 && memcmp(req + n - 4, "\r\n\r\n", 4) == 0) break;
        }
        req[n] = '\0';

        char url[2048];
        if (parse_url_param(req, (size_t)n, url, sizeof(url)) != 0) {
            const char* err = "HTTP/1.0 400 Bad Request\r\nContent-Type: text/plain\r\n\r\n"
                "Missing query: ?url=https://example.com/\n";
            send(c, err, (int)strlen(err), 0);
            close(c);
            continue;
        }

        char escaped[4096];
        url_escape_for_shell(url, escaped, sizeof(escaped));
        char cmd[5120];
        snprintf(cmd, sizeof(cmd),
            "curl -s -L -A \"lardOS-https-bridge/1.0\" -H \"Accept: text/html,text/plain,*/*;q=0.8\" --max-time 45 \"%s\"",
            url);

        FILE* pipe = popen(cmd, "rb");
        if (!pipe) {
            const char* err = "HTTP/1.0 502 Bad Gateway\r\nContent-Type: text/plain\r\n\r\ncurl failed\n";
            send(c, err, (int)strlen(err), 0);
            close(c);
            continue;
        }

        char body[MAX_BODY];
        size_t body_len = 0;
        while (body_len < sizeof(body)) {
            size_t r = fread(body + body_len, 1, sizeof(body) - body_len, pipe);
            if (r == 0) break;
            body_len += r;
        }
        pclose(pipe);

        char hdr[256];
        snprintf(hdr, sizeof(hdr), "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n\r\n", body_len);
        send(c, hdr, (int)strlen(hdr), 0);
        send(c, body, (int)body_len, 0);
        close(c);
    }
    close(s);
    return 0;
}
