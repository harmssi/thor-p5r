// Thor P5R status mod: tiny HTTP server on 127.0.0.1 (GET only, one request per connection).
#include "p5deck.h"
#include <stdio.h>
#include <string.h>

static void reply(SOCKET c, const char* status, const char* type, const char* body, size_t n) {
    char head[256];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, type, (unsigned) n);
    send(c, head, h, 0);
    for (size_t off = 0; off < n; ) {
        int s = send(c, body + off, (int) (n - off > 65536 ? 65536 : n - off), 0);
        if (s <= 0) break;
        off += (size_t) s;
    }
}

static void handle(SOCKET c) {
    char req[2048]; int n = 0;
    DWORD timeout = 2000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout, sizeof timeout);
    while (n < (int) sizeof req - 1) {
        int r = recv(c, req + n, (int) sizeof req - 1 - n, 0);
        if (r <= 0) break;
        n += r; req[n] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[n] = 0;
    char path[512] = "";
    if (sscanf(req, "GET %511s", path) != 1) { reply(c, "400 Bad Request", "text/plain", "bad request", 11); return; }
    Json j; j_init(&j);
    if (!strcmp(path, "/state")) state_write(&j);
    else if (!state_route(path, &j)) { j_free(&j); reply(c, "404 Not Found", "text/plain", "not found", 9); return; }
    reply(c, "200 OK", "application/json; charset=utf-8", j.p ? j.p : "{}", j.p ? j.len : 2);
    j_free(&j);
}

static DWORD WINAPI serve(LPVOID unused) {
    (void) unused;
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) { p5log("WSAStartup failed"); return 0; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*) &yes, sizeof yes);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(P5_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // never reachable from outside
    if (bind(s, (struct sockaddr*) &a, sizeof a) || listen(s, 8)) { p5log("bind/listen on 127.0.0.1:%d failed: %d", P5_PORT, WSAGetLastError()); return 0; }
    p5log("listening on 127.0.0.1:%d", P5_PORT);
    state_init();
    for (;;) {
        SOCKET c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET) { Sleep(50); continue; }
        handle(c);
        closesocket(c);
    }
}

void http_start(void) { CloseHandle(CreateThread(NULL, 0, serve, NULL, 0, NULL)); }
