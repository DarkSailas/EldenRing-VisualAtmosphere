#pragma once
#include <windows.h>

namespace VA
{
    bool InitOverlay(HMODULE hModule);
    void ShutdownOverlay();
    void ToggleMenu();
    bool IsMenuOpen();
}
