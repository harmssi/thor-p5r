// Thor P5R status mod: log, guarded memory reads, pattern scan, JSON writer.
#include "p5deck.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static CRITICAL_SECTION log_lock;
static int log_ready;

void p5log(const char* fmt, ...) {
    if (!log_ready) { InitializeCriticalSection(&log_lock); log_ready = 1; }
    char line[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    EnterCriticalSection(&log_lock);
    FILE* f = fopen("p5deck.log", "a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, line); fclose(f); }
    LeaveCriticalSection(&log_lock);
}

int mem_ok(const void* p, size_t n) {
    if ((uintptr_t) p < 0x10000 || n == 0) return 0;
    const uint8_t* a = p; const uint8_t* end = a + n;
    while (a < end) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) return 0;
        if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(mi.Protect & 0xEE)) return 0;
        a = (const uint8_t*) mi.BaseAddress + mi.RegionSize;
    }
    return 1;
}

uint8_t  rd8(const void* p)  { return mem_ok(p, 1) ? *(const uint8_t*) p : 0; }
uint16_t rd16(const void* p) { uint16_t v = 0; if (mem_ok(p, 2)) memcpy(&v, p, 2); return v; }
uint32_t rd32(const void* p) { uint32_t v = 0; if (mem_ok(p, 4)) memcpy(&v, p, 4); return v; }
uint64_t rd64(const void* p) { uint64_t v = 0; if (mem_ok(p, 8)) memcpy(&v, p, 8); return v; }
void*    rdp(const void* p)  { return (void*) (uintptr_t) rd64(p); }

uint8_t* image_base(void) { return (uint8_t*) GetModuleHandleA(NULL); }

size_t image_size(void) {
    uint8_t* b = image_base();
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*) (b + ((IMAGE_DOS_HEADER*) b)->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

uint8_t* scan(const char* pattern) {
    uint8_t pat[128], mask[128]; int n = 0;
    for (const char* s = pattern; *s && n < 128; ) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '?') { pat[n] = 0; mask[n++] = 0; while (*s == '?') s++; }
        else { pat[n] = (uint8_t) strtoul(s, NULL, 16); mask[n++] = 1; s += 2; }
    }
    uint8_t* b = image_base(); size_t size = image_size();
    for (uint8_t* a = b; a + n <= b + size; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        uint8_t* end = (uint8_t*) mi.BaseAddress + mi.RegionSize;
        if (end > b + size) end = b + size;
        int readable = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && (mi.Protect & 0xEE);
        if (readable) {
            for (uint8_t* p = a; p + n <= end; p++) {
                int i = 0;
                while (i < n && (!mask[i] || p[i] == pat[i])) i++;
                if (i == n) return p;
            }
        }
        a = end;
    }
    return NULL;
}

uint8_t* rip(uint8_t* insn, int rel_at, int insn_len) {
    if (!insn || !mem_ok(insn + rel_at, 4)) return NULL;
    int32_t rel; memcpy(&rel, insn + rel_at, 4);
    return insn + insn_len + rel;
}

// ------------------------------------------------------------------ JSON

static void j_put(Json* j, const char* s, size_t n) {
    if (j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap * 2 : 4096;
        while (cap < j->len + n + 1) cap *= 2;
        char* p = realloc(j->p, cap);
        if (!p) return;
        j->p = p; j->cap = cap;
    }
    memcpy(j->p + j->len, s, n); j->len += n; j->p[j->len] = 0;
}

static void j_sep(Json* j) { if (j->need_comma) j_put(j, ",", 1); j->need_comma = 1; }

void j_init(Json* j) { memset(j, 0, sizeof *j); }
void j_free(Json* j) { free(j->p); memset(j, 0, sizeof *j); }
void j_raw(Json* j, const char* s) { j_put(j, s, strlen(s)); }
void j_obj(Json* j) { j_sep(j); j_put(j, "{", 1); j->need_comma = 0; }
void j_end_obj(Json* j) { j_put(j, "}", 1); j->need_comma = 1; }
void j_arr(Json* j) { j_sep(j); j_put(j, "[", 1); j->need_comma = 0; }
void j_end_arr(Json* j) { j_put(j, "]", 1); j->need_comma = 1; }
void j_int(Json* j, long long v) { char b[32]; j_sep(j); j_put(j, b, (size_t) snprintf(b, sizeof b, "%lld", v)); }
void j_bool(Json* j, int v) { j_sep(j); j_raw(j, v ? "true" : "false"); }
void j_null(Json* j) { j_sep(j); j_raw(j, "null"); }

static void j_quoted(Json* j, const char* s) {
    j_put(j, "\"", 1);
    for (const unsigned char* c = (const unsigned char*) (s ? s : ""); *c; c++) {
        char b[8];
        if (*c == '"' || *c == '\\') { b[0] = '\\'; b[1] = (char) *c; j_put(j, b, 2); }
        else if (*c < 0x20) j_put(j, b, (size_t) snprintf(b, sizeof b, "\\u%04x", *c));
        else j_put(j, (const char*) c, 1);
    }
    j_put(j, "\"", 1);
}

void j_str(Json* j, const char* s) { j_sep(j); j_quoted(j, s); }
void j_key(Json* j, const char* k) { j_sep(j); j_quoted(j, k); j_put(j, ":", 1); j->need_comma = 0; }
