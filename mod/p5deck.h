// Thor P5R status mod: shared helpers. Windows x64, built with mingw-w64, no dependencies besides the Win32 API.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#define P5_PORT 8767
#define P5_VERSION "0.1.0"

// --- log (p5deck.log next to the game exe) ---
void p5log(const char* fmt, ...);

// --- memory: every read is guarded, a bad pointer yields 0 instead of a crash ---
int      mem_ok(const void* p, size_t n);
uint8_t  rd8(const void* p);
uint16_t rd16(const void* p);
uint32_t rd32(const void* p);
uint64_t rd64(const void* p);
void*    rdp(const void* p);
uint8_t* image_base(void);
size_t   image_size(void);
// pattern: "48 8B 05 ?? ?? ?? ?? 48 85 C0"; returns the first match in the exe image or NULL
uint8_t* scan(const char* pattern);
// resolves a RIP-relative operand: address of the instruction + offset of the rel32 + instruction length
uint8_t* rip(uint8_t* insn, int rel_at, int insn_len);

// --- JSON writer into a growing buffer ---
typedef struct { char* p; size_t len, cap; int need_comma; } Json;
void j_init(Json* j);
void j_free(Json* j);
void j_raw(Json* j, const char* s);
void j_key(Json* j, const char* k);
void j_obj(Json* j);
void j_end_obj(Json* j);
void j_arr(Json* j);
void j_end_arr(Json* j);
void j_int(Json* j, long long v);
void j_bool(Json* j, int v);
void j_str(Json* j, const char* s);   // s is UTF-8; control chars and quotes are escaped
void j_null(Json* j);

// --- game state (p5state.c) ---
void state_init(void);            // resolve addresses once the game image is ready
void state_write(Json* j);        // the whole snapshot
int  state_route(const char* path, Json* j);  // extra GET routes (e.g. /debug/...); returns 1 if handled

// --- server (p5http.c) ---
void http_start(void);
