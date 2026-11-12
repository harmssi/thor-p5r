// Thor P5R status mod: the game snapshot. It is built on the game's main thread (twice a second) from the game's own
// script getters and a few guarded memory reads, and handed to the HTTP thread as finished JSON.
#include "p5deck.h"
#include <string.h>
#include <stdio.h>

int debug_route(const char* path, Json* j);
int flow_route(const char* path, Json* j);
int flow_init(void (*tick)(void));
int flow_ready(void);
int flow_get(const char* name, int nargs, const int* args, int fallback);

static int g0(const char* n) { return flow_get(n, 0, NULL, -1); }
static int g1(const char* n, int a) { return flow_get(n, 1, &a, -1); }
static int g2(const char* n, int a, int b) { int v[2] = { a, b }; return flow_get(n, 2, v, -1); }

// ------------------------------------------------------------------ addresses (resolved by signature)

// values of the game's scene state machine (observed): 4 = field
#define SEQ_FIELD 4
#define SEQ_EVENT_LAST 7
#define UNIT_SIZE 0x2A0
#define UNIT_HP 0x0C
#define UNIT_SP 0x10
#define UNIT_STATUS 0x14
#define UNIT_LEVEL 0x18
#define UNIT_EQUIPPED 0x40      // index into the stock
#define UNIT_STOCK 0x44         // 12 personas of 0x30 bytes
#define STOCK_SIZE 0x30
#define STOCK_COUNT 12

static uint8_t* units;          // DatUnit[]: 1 = protagonist ... 10
static uint32_t* money;
static char** party_names;      // pointers to NUL-separated name lists, indexed by id
static char** persona_names;
static char** enemy_names;
static void** sequence;
// The game keeps its name lists as neighbouring globals; these two sit a fixed number of pointers before the persona names.
#define ARCANA_NAMES_BEFORE 10
#define SKILL_NAMES_BEFORE 5

// confidant id -> arcana (index into the game's arcana names): 1..22 are the arcana themselves, 23..32 the romance
// variants of ten of them, 33/34/36/37 Faith, 35 Councillor
static int confidant_arcana(int c) {
    static const unsigned char variants[] = { 3, 4, 7, 10, 11, 14, 15, 16, 18, 21 };
    if (c >= 1 && c <= 21) return c;
    if (c == 22) return 24;
    if (c >= 23 && c <= 32) return variants[c - 23];
    if (c == 35) return 30;
    return c >= 33 && c <= 37 ? 29 : 0;
}

static void resolve(void) {
    uint8_t* m;
    if ((m = scan("0F B7 C1 48 8D 0D ?? ?? ?? ?? 48 69 C0 A0 02 00 00 48 01 C8"))) units = rip(m, 6, 10);
    if ((m = scan("C7 05 ?? ?? ?? ?? D0 07 00 00 BA 01 00 00 00"))) money = (uint32_t*) rip(m, 2, 10);
    if ((m = scan("48 03 1D ?? ?? ?? ?? E9 ?? ?? ?? ?? 48 8B 05"))) party_names = (char**) rip(m, 3, 7);
    if ((m = scan("48 03 1D ?? ?? ?? ?? F3 41 0F 59 FA"))) persona_names = (char**) rip(m, 3, 7);
    if ((m = scan("4C 03 05 ?? ?? ?? ?? EB ??"))) enemy_names = (char**) rip(m, 3, 7);
    if ((m = scan("48 8B 05 ?? ?? ?? ?? 66 44 89 4C 24 ??"))) sequence = (void**) rip(m, 3, 7);
    p5log("units %p money %p names %p %p %p sequence %p", (void*) units, (void*) money, (void*) party_names,
        (void*) persona_names, (void*) enemy_names, (void*) sequence);
}

// ------------------------------------------------------------------ names (the game's own text, in its language)

// The game stores European text as ASCII plus two-byte codes (both >= 0x80) that select a glyph:
// glyph = (hi-0x80)*0x80 + (lo-0x80). Glyphs 471..563 are the accented letters below (observed in the game's lists).
static const unsigned short ACCENTED[] = {
    0x153, 0x17E, 0x178, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF };
#define ACCENTED_FIRST 471

static void decode(const char* s, char* out, size_t cap) {
    size_t n = 0;
    for (const unsigned char* c = (const unsigned char*) s; *c && n + 4 < cap; c++) {
        if (*c < 0x80) { out[n++] = (char) *c; continue; }
        if (c[1] < 0x80) { out[n++] = '?'; continue; }
        int g = (*c - 0x80) * 0x80 + (c[1] - 0x80); c++;
        int idx = g - ACCENTED_FIRST;
        unsigned cp = g < 0x60 ? (unsigned) g + 0x20      // the first glyphs repeat ASCII
            : idx >= 0 && idx < (int) (sizeof ACCENTED / sizeof *ACCENTED) ? ACCENTED[idx] : '?';
        if (cp < 0x80) out[n++] = (char) cp;
        else if (cp < 0x800) { out[n++] = (char) (0xC0 | (cp >> 6)); out[n++] = (char) (0x80 | (cp & 0x3F)); }
        else { out[n++] = (char) (0xE0 | (cp >> 12)); out[n++] = (char) (0x80 | ((cp >> 6) & 0x3F)); out[n++] = (char) (0x80 | (cp & 0x3F)); }
    }
    out[n] = 0;
}

// entry <id> of a NUL-separated list. The lists do not change while the game runs, so results are cached
// (walking a list means one guarded read per entry, which is slow under emulation).
#define NAME_CACHE 256
static struct { char** table; const char* base; int id; char text[64]; } name_cache[NAME_CACHE];

static void list_name(char** table, int id, char* out, size_t cap) {
    out[0] = 0;
    if (!table || id < 0 || id > 2000 || !mem_ok(table, 8)) return;
    const char* p = *table;
    unsigned slot = ((unsigned) (uintptr_t) table * 31u + (unsigned) id) % NAME_CACHE;
    if (name_cache[slot].table == table && name_cache[slot].base == p && name_cache[slot].id == id) {
        snprintf(out, cap, "%s", name_cache[slot].text); return;
    }
    for (int i = 0; i < id; i++) {
        if (!mem_ok(p, 64)) return;
        p += strlen(p) + 1;
    }
    if (!mem_ok(p, 64) || strlen(p) >= 60) return;
    decode(p, out, cap);
    name_cache[slot].table = table; name_cache[slot].base = *table; name_cache[slot].id = id;
    snprintf(name_cache[slot].text, sizeof name_cache[slot].text, "%s", out);
}

// ------------------------------------------------------------------ affinities

// shortcut: the two affinity tables are static data found by hand in the Steam build of 23 Aug 2024 (1.04); they are
// only used when the exe has that build's timestamp. Upgrade to a signature when another build appears.
#define BUILD_104 1724409370u
#define ENEMY_AFF_RVA 0x233E090
#define PERSONA_AFF_RVA 0x234EF60
#define AFF_SIZE 40             // 20 x (multiplier in 5 % steps, flags)

static int build_ok(void) {
    uint8_t* b = image_base();
    return ((IMAGE_NT_HEADERS*) (b + ((IMAGE_DOS_HEADER*) b)->e_lfanew))->FileHeader.TimeDateStamp == BUILD_104;
}

// ten letters for Phys Gun Fire Ice Elec Wind Psy Nuke Bless Curse: w weak, r resist, n null, p repel, d drain, - neutral
static void affinity(int persona, int id, char out[11]) {
    memset(out, '-', 10); out[10] = 0;
    if (!build_ok() || id < 0 || id > 1200) { out[0] = 0; return; }
    const uint8_t* r = image_base() + (persona ? PERSONA_AFF_RVA : ENEMY_AFF_RVA) + (size_t) id * AFF_SIZE;
    if (!mem_ok(r, AFF_SIZE)) { out[0] = 0; return; }
    for (int e = 0; e < 10; e++) {
        int mult = r[2 * e], f = r[2 * e + 1];
        out[e] = (f & 1) ? 'n' : (f & 2) ? 'p' : (f & 4) ? 'd' : (f & 8) ? 'w' : (f & 0x10) ? 'r'
            : mult > 20 ? 'w' : (mult > 0 && mult < 20) ? 'r' : '-';
    }
}

// /enemies: every shadow with a name, for the lookup tab (memory reads only, safe from the HTTP thread)
static void write_enemies(Json* j) {
    char name[96], aff[11];
    j_obj(j); j_key(j, "enemies"); j_arr(j);
    const char* p = enemy_names && mem_ok(enemy_names, 8) ? *enemy_names : NULL;
    for (int id = 0; p && id < 800; id++, p += strlen(p) + 1) {
        if (!mem_ok(p, 64) || !*p || strlen(p) >= 60) break;
        if (id == 0) continue;
        decode(p, name, sizeof name);
        affinity(0, id, aff);
        j_obj(j); j_key(j, "id"); j_int(j, id); j_key(j, "name"); j_str(j, name); j_key(j, "aff"); j_str(j, aff); j_end_obj(j);
    }
    j_end_arr(j); j_end_obj(j);
}

// ------------------------------------------------------------------ snapshot

static const char* volatile step = "idle";   // what the snapshot was doing, for the crash line in the log

static LONG CALLBACK on_exception(EXCEPTION_POINTERS* e) {
    static LONG logged;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW)
            && InterlockedExchange(&logged, 1) == 0)
        p5log("exception %08lx at +%llx (snapshot step: %s)", (unsigned long) code,
            (unsigned long long) ((uint8_t*) e->ExceptionRecord->ExceptionAddress - image_base()), step);
    return EXCEPTION_CONTINUE_SEARCH;
}

static CRITICAL_SECTION snap_lock;
static Json snap;
static volatile LONG frames;
static int fps, build_ms;
static volatile LONG snapshots_on = 1;

static void write_persona(Json* j, const uint8_t* e) {
    char name[96];
    int id = rd16(e + 2);
    j_obj(j);
    j_key(j, "id"); j_int(j, id);
    list_name(persona_names, id, name, sizeof name); j_key(j, "name"); j_str(j, name);
    j_key(j, "lv"); j_int(j, rd8(e + 4));
    char aff[11]; affinity(1, id, aff); j_key(j, "aff"); j_str(j, aff);
    j_key(j, "skills"); j_arr(j);
    for (int s = 0; s < 8; s++) {
        int sk = rd16(e + 0x0C + 2 * s);
        if (!sk) continue;
        list_name(persona_names ? persona_names - SKILL_NAMES_BEFORE : NULL, sk, name, sizeof name);
        j_str(j, name);
    }
    j_end_arr(j);
    j_key(j, "st"); j_arr(j);
    for (int s = 0; s < 5; s++) j_int(j, rd8(e + 0x1C + s) + rd8(e + 0x21 + s));
    j_end_arr(j);
    j_end_obj(j);
}

static void build(Json* j) {
    char name[96];
    j_obj(j);
    j_key(j, "v"); j_int(j, 1);
    j_key(j, "mod"); j_str(j, P5_VERSION);
    j_key(j, "ready"); j_bool(j, 1);
    j_key(j, "fps"); j_int(j, fps);
    j_key(j, "buildMs"); j_int(j, build_ms);

    // sequence: raw values of the game's scene state machine
    void* seq = sequence ? rdp(sequence) : NULL;
    void* info = seq ? rdp((uint8_t*) seq + 72) : NULL;
    j_key(j, "seq"); j_arr(j); j_int(j, seq ? (int32_t) rd32((uint8_t*) seq + 0x10) : -1);
    j_int(j, info ? (int32_t) rd32((uint8_t*) info + 4) : -1); j_int(j, info ? (int32_t) rd32((uint8_t*) info + 8) : -1); j_end_arr(j);
    int mode = info ? (int32_t) rd32((uint8_t*) info + 4) : -1;
    static int last_mode = -99;
    if (mode != last_mode) { p5log("sequence %d -> %d", last_mode, mode); last_mode = mode; }
    // Script getters are only called while a save is loaded and the game is in the field, a battle or an event.
    int in_game = mode >= SEQ_FIELD && mode <= SEQ_EVENT_LAST;
    j_key(j, "inGame"); j_bool(j, in_game);
    if (!in_game) { j_end_obj(j); return; }
    step = "field";
    j_key(j, "field"); j_arr(j); j_int(j, g0("FLD_GET_MAJOR")); j_int(j, g0("FLD_GET_MINOR")); j_end_arr(j);
    step = "date";

    int month = g0("GET_MONTH"), day = g0("GET_DAY");
    j_key(j, "date"); j_obj(j);
    j_key(j, "month"); j_int(j, month);
    j_key(j, "day"); j_int(j, day);
    j_key(j, "dow"); j_int(j, g0("GET_DAYOFWEEK"));
    j_key(j, "time"); j_int(j, g0("GET_TIME"));
    j_key(j, "total"); j_int(j, g0("GET_TOTAL_DAY"));
    j_end_obj(j);

    step = "alert";
    j_key(j, "alert"); j_int(j, g0("FLD_GET_ALERT_VALUE"));
    j_key(j, "money"); if (money && mem_ok(money, 4)) j_int(j, *money); else j_null(j);

    step = "party";
    j_key(j, "party"); j_arr(j);
    for (int slot = 0; slot < 4; slot++) {
        int id = g1("GET_PARTY", slot);
        if (id < 1 || id > 10) continue;
        const uint8_t* u = units ? units + (size_t) id * UNIT_SIZE : NULL;
        j_obj(j);
        j_key(j, "id"); j_int(j, id);
        list_name(party_names, id, name, sizeof name); j_key(j, "name"); j_str(j, name);
        j_key(j, "lv"); j_int(j, g1("GET_PC_LEVEL", id));
        j_key(j, "hp"); j_int(j, g1("GET_HP", id));
        j_key(j, "maxHp"); j_int(j, g1("GET_MAXHP", id));
        j_key(j, "sp"); j_int(j, g1("GET_SP", id));
        j_key(j, "maxSp"); j_int(j, g1("GET_MAXSP", id));
        j_key(j, "status"); j_int(j, u ? rd32(u + UNIT_STATUS) : 0);
        int pid = g1("GET_EQUIP_PERSONA_ID", id);
        j_key(j, "persona"); j_int(j, pid);
        list_name(persona_names, pid, name, sizeof name); j_key(j, "personaName"); j_str(j, name);
        j_end_obj(j);
    }
    j_end_arr(j);

    step = "personas";
    // the protagonist's persona stock
    j_key(j, "personas"); j_arr(j);
    if (units && mem_ok(units + UNIT_SIZE, UNIT_SIZE)) {
        const uint8_t* u = units + UNIT_SIZE;
        for (int i = 0; i < STOCK_COUNT; i++) {
            const uint8_t* e = u + UNIT_STOCK + i * STOCK_SIZE;
            if ((rd8(e) & 1) && rd16(e + 2)) write_persona(j, e);
        }
        j_end_arr(j);
        j_key(j, "equipped"); j_int(j, rd16(u + UNIT_EQUIPPED));
    } else j_end_arr(j);

    step = "stats";
    j_key(j, "stats"); j_arr(j);
    for (int s = 0; s < 5; s++) {
        j_obj(j); j_key(j, "pts"); j_int(j, g2("GET_PC_PARAM", 1, s)); j_key(j, "lv"); j_int(j, g2("GET_PC_PARAM_LV", 1, s)); j_end_obj(j);
    }
    j_end_arr(j);

    step = "confidants";
    j_key(j, "confidants"); j_arr(j);
    for (int c = 1; c <= 37; c++) {
        int lv = g1("CMM_GET_LV", c);
        if (lv <= 0) continue;
        j_obj(j);
        j_key(j, "id"); j_int(j, c);
        j_key(j, "arcana"); j_int(j, confidant_arcana(c));
        list_name(persona_names ? persona_names - ARCANA_NAMES_BEFORE : NULL, confidant_arcana(c), name, sizeof name);
        j_key(j, "name"); j_str(j, name);
        j_key(j, "lv"); j_int(j, lv);
        j_key(j, "pts"); j_int(j, g1("CMM_GET_POINT", c));
        j_key(j, "next"); j_int(j, g1("CMM_GET_NEXT_POINT", c));
        j_key(j, "lvup"); j_int(j, g1("CMM_CHK_LVUP", c));
        j_key(j, "rev"); j_int(j, g1("CMM_CHK_REVERSE", c));
        j_end_obj(j);
    }
    j_end_arr(j);
    j_end_obj(j);
    step = "idle";
}

static void tick(void) {
    static ULONGLONG last_snap, last_fps; static LONG last_frames;
    InterlockedIncrement(&frames);
    ULONGLONG now = GetTickCount64();
    if (now - last_fps >= 1000) { fps = (int) ((frames - last_frames) * 1000 / (now - last_fps)); last_frames = frames; last_fps = now; }
    if (now - last_snap < 500 || !snapshots_on) return;
    last_snap = now;
    Json fresh; j_init(&fresh);
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    build(&fresh);
    QueryPerformanceCounter(&t1); build_ms = (int) ((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
    EnterCriticalSection(&snap_lock);
    Json old = snap; snap = fresh;
    LeaveCriticalSection(&snap_lock);
    j_free(&old);
}

static DWORD WINAPI init_thread(LPVOID unused) {
    (void) unused;
    // the game's code may not be unpacked when the DLL loads: retry until the signatures are found
    for (int i = 0; i < 600; i++) {
        if (scan("4C 8D 3D ?? ?? ?? ?? 8B F5")) { resolve(); if (flow_init(tick)) { p5log("ready after %d s", i); return 0; } }
        Sleep(1000);
    }
    p5log("signatures not found: unsupported game version?");
    return 0;
}

void state_init(void) {
    InitializeCriticalSection(&snap_lock);
    AddVectoredExceptionHandler(1, on_exception);
    p5log("image %p size %u", (void*) image_base(), (unsigned) image_size());
    CloseHandle(CreateThread(NULL, 0, init_thread, NULL, 0, NULL));
}

void state_write(Json* j) {
    EnterCriticalSection(&snap_lock);
    if (snap.p) j_raw(j, snap.p);
    LeaveCriticalSection(&snap_lock);
    if (!j->p) { j_obj(j); j_key(j, "v"); j_int(j, 1); j_key(j, "mod"); j_str(j, P5_VERSION); j_key(j, "ready"); j_bool(j, 0); j_end_obj(j); }
}

int state_route(const char* path, Json* j) {
    if (!strcmp(path, "/enemies")) { write_enemies(j); return 1; }
    if (!strncmp(path, "/debug/snap?on=", 15)) {   // switch the snapshots off to measure the game without them
        snapshots_on = path[15] == '1';
        j_obj(j); j_key(j, "on"); j_bool(j, snapshots_on); j_key(j, "fps"); j_int(j, fps); j_end_obj(j);
        return 1;
    }
    if (!strncmp(path, "/debug/fps", 10)) { j_obj(j); j_key(j, "fps"); j_int(j, fps); j_key(j, "buildMs"); j_int(j, build_ms); j_end_obj(j); return 1; }
#ifdef P5_DEBUG   // memory viewer and direct script calls: for finding addresses only, never in a release build
    return flow_route(path, j) || debug_route(path, j);
#else
    return 0;
#endif
}
