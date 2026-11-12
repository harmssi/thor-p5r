// Thor P5R status mod: calls the game's own script ("flow") functions – the same getters the game's scripts use –
// on the game's main thread. The function table carries the names, so functions are looked up by name, not by index.
#include "p5deck.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { void* fn; int64_t nargs; const char* name; } FlowFn;
typedef struct { FlowFn* fns; int64_t count; } FlowGroup;
#define GROUPS 6
#define CTX_SIZE 1024            // the game's context is at least 0x270 bytes
#define CTX_NUMARGS 0x2C
#define CTX_ARGTYPES 0x30
#define CTX_ARGS 0x58
#define CTX_RET 0x1D8

static FlowGroup* groups;        // FlowGroup[6]
static void** ctx_slot;          // the game's "current flow context" pointer
static void (*tick_cb)(void);
static volatile LONG hooked;

int flow_ready(void) { return groups && ctx_slot && hooked; }

FlowFn* flow_find(const char* name) {
    if (!groups) return NULL;
    for (int g = 0; g < GROUPS; g++) {
        FlowFn* f = groups[g].fns; int64_t n = groups[g].count;
        if (!mem_ok(f, sizeof(FlowFn)) || n <= 0 || n > 4096) continue;
        for (int64_t i = 0; i < n; i++)
            if (mem_ok(f[i].name, 2) && !strcmp(f[i].name, name)) return &f[i];
    }
    return NULL;
}

// Main thread only. Returns the int result; *ok = 0 if the function is unknown or the argument count does not match.
int flow_call(FlowFn* f, int nargs, const int* args, int* ok) {
    static uint8_t ctx[CTX_SIZE];
    if (ok) *ok = 0;
    if (!f || !mem_ok(f->fn, 16) || f->nargs != nargs || nargs > 8) return 0;
    memset(ctx, 0, sizeof ctx);
    *(int32_t*) (ctx + CTX_NUMARGS) = nargs;
    for (int i = 0; i < nargs; i++) {   // the game stores arguments in reverse
        *(int64_t*) (ctx + CTX_ARGS + 8 * (nargs - i)) = args[i];
        ctx[CTX_ARGTYPES + nargs - i - 1] = 0;
    }
    void* prev = *ctx_slot;
    *ctx_slot = ctx;
    ((char (*)(void*)) f->fn)(NULL);
    *ctx_slot = prev;
    if (ok) *ok = 1;
    return *(int32_t*) (ctx + CTX_RET);
}

int flow_get(const char* name, int nargs, const int* args, int fallback) {
    int ok; int v = flow_call(flow_find(name), nargs, args, &ok);
    return ok ? v : fallback;
}

// ------------------------------------------------------------------ one-shot calls from the HTTP thread (debug)

static CRITICAL_SECTION q_lock;
static struct { volatile LONG pending, done; char name[64]; int nargs, args[8], result, ok; } q;

int flow_call_queued(const char* name, int nargs, const int* args, int* ok) {
    *ok = 0;
    if (!flow_ready()) return 0;
    EnterCriticalSection(&q_lock);
    snprintf(q.name, sizeof q.name, "%s", name); q.nargs = nargs; memcpy(q.args, args, sizeof(int) * (size_t) nargs);
    q.done = 0; q.pending = 1;
    for (int i = 0; i < 200 && !q.done; i++) Sleep(10);   // the main loop picks it up within a frame
    int r = q.done ? q.result : 0; *ok = q.done ? q.ok : 0;
    q.pending = 0;
    LeaveCriticalSection(&q_lock);
    return r;
}

static void on_main_loop(void) {
    if (q.pending && !q.done) {
        q.result = flow_call(flow_find(q.name), q.nargs, q.args, &q.ok);
        q.done = 1;
    }
    if (tick_cb) tick_cb();
}

// ------------------------------------------------------------------ hook: one call site in the game's main loop

static void (*original_loop)(void);

static void hook_entry(void) {      // replaces the call target; keeps the callee's contract (no arguments, no result)
    on_main_loop();
    original_loop();
}

// The call is "E8 rel32", so the new target must be within 2 GB: a 12-byte jump pad next to the image.
static uint8_t* near_pad(uint8_t* around) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    for (intptr_t step = 0x10000; step < 0x70000000; step += 0x10000) {
        for (int dir = -1; dir <= 1; dir += 2) {
            uint8_t* want = around + dir * step;
            uint8_t* p = VirtualAlloc((void*) ((uintptr_t) want & ~(uintptr_t) 0xFFFF), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return NULL;
}

int flow_init(void (*tick)(void)) {
    static int lock_ready;
    if (!lock_ready) { InitializeCriticalSection(&q_lock); lock_ready = 1; }
    tick_cb = tick;
    uint8_t* a = scan("4C 8D 3D ?? ?? ?? ?? 8B F5");
    uint8_t* b = scan("4C 8B 05 ?? ?? ?? ?? 41 8B 50 ?? 29 CA");
    uint8_t* c = scan("E8 ?? ?? ?? ?? FF 05 ?? ?? ?? ?? 83 0D ?? ?? ?? ?? 10");
    if (!a || !b || !c) return 0;   // code not unpacked yet, or a different game version
    groups = (FlowGroup*) rip(a, 3, 7);
    ctx_slot = (void**) rip(b, 3, 7);
    if (!mem_ok(groups, sizeof(FlowGroup) * GROUPS) || !mem_ok(groups[0].fns, sizeof(FlowFn))) { groups = NULL; return 0; }
    if (!hooked) {
        original_loop = (void (*)(void)) rip(c, 1, 5);
        uint8_t* pad = near_pad(c);
        if (!pad) { p5log("no memory near the image for the hook"); return 0; }
        // mov rax, hook_entry ; jmp rax
        pad[0] = 0x48; pad[1] = 0xB8; void* target = (void*) hook_entry; memcpy(pad + 2, &target, 8); pad[10] = 0xFF; pad[11] = 0xE0;
        int32_t rel = (int32_t) (pad - (c + 5));
        DWORD old;
        if (!VirtualProtect(c + 1, 4, PAGE_EXECUTE_READWRITE, &old)) { p5log("VirtualProtect failed"); return 0; }
        memcpy(c + 1, &rel, 4);
        VirtualProtect(c + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), c, 5);
        hooked = 1;
        p5log("flow table +%llx, context +%llx, main loop call +%llx hooked",
            (unsigned long long) ((uint8_t*) groups - image_base()), (unsigned long long) ((uint8_t*) ctx_slot - image_base()),
            (unsigned long long) (c - image_base()));
    }
    return 1;
}

// ------------------------------------------------------------------ debug routes

static const char* q_arg(const char* path, const char* name, char* out, size_t cap) {
    const char* q = strchr(path, '?'); out[0] = 0; size_t n = strlen(name);
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

// /debug/flowlist?like=GET   names, group, index and argument counts of the game's script functions
// /debug/call?name=GET_MONTH&a=1,2   calls one function on the main thread (use getters only)
int flow_route(const char* path, Json* j) {
    char a[128], b[128];
    if (!strncmp(path, "/debug/flowlist", 15)) {
        const char* like = q_arg(path, "like", a, sizeof a);
        j_obj(j); j_key(j, "ready"); j_bool(j, flow_ready()); j_key(j, "fns"); j_arr(j);
        for (int g = 0; groups && g < GROUPS; g++) {
            FlowFn* f = groups[g].fns; int64_t n = groups[g].count;
            if (!mem_ok(f, sizeof(FlowFn)) || n <= 0 || n > 4096) continue;
            for (int64_t i = 0; i < n; i++) {
                if (!mem_ok(f[i].name, 2) || (like && !strstr(f[i].name, like))) continue;
                char line[160]; snprintf(line, sizeof line, "%d:%d %s/%d", g, (int) i, f[i].name, (int) f[i].nargs);
                j_str(j, line);
            }
        }
        j_end_arr(j); j_end_obj(j);
        return 1;
    }
    if (!strncmp(path, "/debug/call", 11)) {
        if (!q_arg(path, "name", a, sizeof a)) return 0;
        int args[8], n = 0;
        if (q_arg(path, "a", b, sizeof b))
            for (char* t = strtok(b, ","); t && n < 8; t = strtok(NULL, ",")) args[n++] = (int) strtol(t, NULL, 0);
        int ok; int r = flow_call_queued(a, n, args, &ok);
        j_obj(j); j_key(j, "ok"); j_bool(j, ok); j_key(j, "result"); j_int(j, r); j_end_obj(j);
        return 1;
    }
    return 0;
}
