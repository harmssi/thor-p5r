// Thor P5R status mod: debug routes used to find and verify addresses on the real game (a tiny memory viewer over HTTP).
//   /debug/info                       image base, size, PE timestamp
//   /debug/read?addr=<hex>&len=<n>    hex dump (addr absolute; "+<hex>" = relative to the image base)
//   /debug/find?hex=<bytes>[&w=1]     addresses of a byte sequence in committed memory (w=1: writable regions only), max 200
//   /debug/scan?pat=<48_8B_05_??...>  first match of a code pattern in the exe image
#include "p5deck.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* arg(const char* path, const char* name, char* out, size_t cap) {
    const char* q = strchr(path, '?'); out[0] = 0;
    size_t n = strlen(name);
    while (q && *q) {
        q++;
        if (!strncmp(q, name, n) && q[n] == '=') {
            const char* v = q + n + 1; size_t i = 0;
            while (v[i] && v[i] != '&' && i + 1 < cap) { out[i] = v[i]; i++; }
            out[i] = 0; return out;
        }
        q = strchr(q, '&');
    }
    return NULL;
}

static uint8_t* parse_addr(const char* s) {
    if (*s == '+' || *s == ' ') return image_base() + strtoull(s + 1, NULL, 16);   // '+' arrives as ' ' in some clients
    return (uint8_t*) (uintptr_t) strtoull(s, NULL, 16);
}

int debug_route(const char* path, Json* j) {
    char a[600], b[64];
    if (!strncmp(path, "/debug/info", 11)) {
        uint8_t* base = image_base();
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*) (base + ((IMAGE_DOS_HEADER*) base)->e_lfanew);
        char exe[MAX_PATH] = ""; GetModuleFileNameA(NULL, exe, sizeof exe);
        const char* name = strrchr(exe, '\\');
        j_obj(j);
        j_key(j, "mod"); j_str(j, P5_VERSION);
        j_key(j, "exe"); j_str(j, name ? name + 1 : exe);
        snprintf(a, sizeof a, "%p", (void*) base); j_key(j, "base"); j_str(j, a);
        j_key(j, "size"); j_int(j, (long long) image_size());
        j_key(j, "timestamp"); j_int(j, nt->FileHeader.TimeDateStamp);
        j_end_obj(j);
        return 1;
    }
    if (!strncmp(path, "/debug/read", 11)) {
        if (!arg(path, "addr", a, sizeof a)) return 0;
        uint8_t* p = parse_addr(a);
        size_t len = arg(path, "len", b, sizeof b) ? strtoul(b, NULL, 10) : 256;
        if (len > 65536) len = 65536;
        j_obj(j);
        snprintf(a, sizeof a, "%p", (void*) p); j_key(j, "addr"); j_str(j, a);
        j_key(j, "hex");
        if (mem_ok(p, len)) {
            char* hex = malloc(len * 2 + 1);
            for (size_t i = 0; i < len; i++) snprintf(hex + i * 2, 3, "%02x", p[i]);
            j_str(j, hex); free(hex);
        } else j_null(j);
        j_end_obj(j);
        return 1;
    }
    if (!strncmp(path, "/debug/find", 11)) {
        if (!arg(path, "hex", a, sizeof a)) return 0;
        uint8_t needle[256]; size_t n = strlen(a) / 2;
        if (n == 0 || n > sizeof needle) return 0;
        for (size_t i = 0; i < n; i++) { char h[3] = { a[i * 2], a[i * 2 + 1], 0 }; needle[i] = (uint8_t) strtoul(h, NULL, 16); }
        int writable = arg(path, "w", b, sizeof b) && b[0] == '1';
        int found = 0;
        j_obj(j); j_key(j, "hits"); j_arr(j);
        SYSTEM_INFO si; GetSystemInfo(&si);
        for (uint8_t* p = si.lpMinimumApplicationAddress; p < (uint8_t*) si.lpMaximumApplicationAddress && found < 200; ) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(p, &mi, sizeof mi)) break;
            uint8_t* end = (uint8_t*) mi.BaseAddress + mi.RegionSize;
            int ok = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
                && (writable ? (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) : (mi.Protect & 0xEE));
            if (ok) {
                for (uint8_t* q = mi.BaseAddress; q + n <= end && found < 200; q++) {
                    if (*q == needle[0] && !memcmp(q, needle, n) && q != needle) {   // not the copy on our own stack
                        snprintf(a, sizeof a, "%p", (void*) q); j_str(j, a); found++;
                    }
                }
            }
            p = end;
        }
        j_end_arr(j); j_end_obj(j);
        return 1;
    }
    if (!strncmp(path, "/debug/scan", 11)) {
        if (!arg(path, "pat", a, sizeof a)) return 0;
        for (char* c = a; *c; c++) if (*c == '_') *c = ' ';
        uint8_t* hit = scan(a);
        j_obj(j); j_key(j, "hit");
        if (hit) { snprintf(a, sizeof a, "+%llx", (unsigned long long) (hit - image_base())); j_str(j, a); } else j_null(j);
        j_end_obj(j);
        return 1;
    }
    return 0;
}
