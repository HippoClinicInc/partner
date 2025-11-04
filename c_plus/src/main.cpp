#include "common/S3Common.h"

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        // When DLL is unloaded, auto cleanup
        if (g_isInitialized) {
            // AWS SDK cleanup is handled automatically
        }
        break;
    }
    return TRUE;
}
