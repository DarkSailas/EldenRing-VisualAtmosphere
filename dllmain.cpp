#include <windows.h>
#include "VisualAtmosphere.hpp"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        VA::Init(hinstDLL);
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        VA::Shutdown();
    }
    return TRUE;
}
