#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace VA
{
    struct AtmosphereConfig
    {
        bool enableAtmosphere = true;
        int activePreset = 0; // 0=Bloodborne, 1=DS3, 2=DeS, 3=ColdSlate, 4=Custom

        // Channel multipliers (RGB Balance)
        float scaleR = 0.96f;
        float scaleG = 0.95f;
        float scaleB = 1.00f;

        // Tonal grading
        float saturation = 0.82f;
        float contrast = 1.05f;
        float shadowDepth = 0.01f;
        float vignetteIntensity = 0.15f;

        // Hotkeys
        int toggleKey = 0;       // Disabled (toggled via in-game checkbox)
        int menuKey = VK_F5;     // 0x74: open GUI menu (Default F5)

        // Custom User Preset storage
        float customScaleR = 0.96f;
        float customScaleG = 0.95f;
        float customScaleB = 1.00f;
        float customSaturation = 0.82f;
        float customContrast = 1.05f;
        float customShadowDepth = 0.01f;
        float customVignetteIntensity = 0.15f;
    };

    extern AtmosphereConfig g_Config;
    extern bool g_Enabled;

    bool Init(HMODULE hModule);
    void Shutdown();
    void LoadConfig();
    void SaveConfig();
    void ApplyPreset(int id);
    void Log(const char* fmt, ...);
}

extern "C"
{
    __declspec(dllexport) VA::AtmosphereConfig* VA_GetConfig();
    __declspec(dllexport) void VA_SaveConfig();
    __declspec(dllexport) void VA_ApplyPreset(int id);
    __declspec(dllexport) void VA_Toggle();
}
