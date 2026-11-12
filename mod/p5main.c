// Thor P5R status mod: entry. The DLL only reads; it starts one thread with the local status server.
#include "p5deck.h"

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    (void) reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        p5log("Thor P5R status mod " P5_VERSION " loaded");
        http_start();
    }
    return TRUE;
}
