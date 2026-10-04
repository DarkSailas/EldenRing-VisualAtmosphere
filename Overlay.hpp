#pragma once

namespace VA
{
    namespace Overlay
    {
        // Hooks the swap chain and DirectInput. Returns false when the settings window is unavailable.
        bool Init();
        void Shutdown();

        // Keeps keyboard and mouse away from the game while the settings window is open.
        // Cheap when the window is closed, safe to call from any game thread.
        void HoldGameInput();
    }
}
