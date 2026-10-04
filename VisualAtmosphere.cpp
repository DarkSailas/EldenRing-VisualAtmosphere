#include "VisualAtmosphere.hpp"
#include "Overlay.hpp"
#include "Minhook/MinHook.h"
#include "Ini/ini.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <fstream>
#include <atomic>

namespace VA
{
    AtmosphereConfig g_Config;
    bool g_Enabled = true;

    static HMODULE g_hModule = NULL;
    static std::atomic<bool> g_Running{ true };
    static HANDLE g_hWorkerThread = NULL;

    typedef void* (__fastcall *FnComputeColorMatrix)(void* pParam, float* pOutMatrix);
    static FnComputeColorMatrix g_OrigComputeColorMatrix = nullptr;
    static uintptr_t g_ColorMatrixTarget = 0;

    typedef void* (__fastcall *FnComputeVignette)(void* rcx, void* rdx, void* r8, void* r9, float arg5, float arg6);
    static FnComputeVignette g_OrigComputeVignette = nullptr;
    static uintptr_t g_VignetteTarget = 0;

    static std::string GetModuleDir()
    {
        char buffer[MAX_PATH];
        GetModuleFileNameA(g_hModule, buffer, sizeof(buffer));
        std::string path(buffer);
        size_t pos = path.find_last_of("\\/");
        return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
    }

    void Log(const char* fmt, ...)
    {
        std::string logPath = GetModuleDir() + "VisualAtmosphere.log";
        FILE* fp = fopen(logPath.c_str(), "a");
        if (!fp) return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(fp, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        va_list args;
        va_start(args, fmt);
        vfprintf(fp, fmt, args);
        va_end(args);

        fprintf(fp, "\n");
        fclose(fp);
    }

    static float SafeStof(const std::string& str, float def)
    {
        try {
            return std::stof(str);
        } catch (...) {
            return def;
        }
    }

    static int SafeStoi(const std::string& str, int def)
    {
        try {
            if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0)
                return std::stoi(str, nullptr, 16);
            return std::stoi(str);
        } catch (...) {
            return def;
        }
    }

    // Forward declarations for cached adj matrix (defined near ApplyAtmosphereMatrix)
    static float g_CachedAdj[4][4];
    static bool g_AdjDirty = true;

    void ApplyPreset(int id)
    {
        g_Config.activePreset = id;
        switch (id)
        {
        case 0: // Bloodborne / Yharnam Noir (Cold Slate & Sombre Gothic)
            g_Config.scaleR = 0.96f;
            g_Config.scaleG = 0.95f;
            g_Config.scaleB = 1.00f;
            g_Config.saturation = 0.82f;
            g_Config.contrast = 1.05f;
            g_Config.shadowDepth = 0.01f;
            g_Config.vignetteIntensity = 0.15f;
            break;
        case 1: // Dark Souls 3 / Lothric Ashen
            g_Config.scaleR = 0.94f;
            g_Config.scaleG = 0.94f;
            g_Config.scaleB = 0.97f;
            g_Config.saturation = 0.75f;
            g_Config.contrast = 1.04f;
            g_Config.shadowDepth = 0.01f;
            g_Config.vignetteIntensity = 0.12f;
            break;
        case 2: // Demon's Souls / Boletaria Deep Cold
            g_Config.scaleR = 0.92f;
            g_Config.scaleG = 0.93f;
            g_Config.scaleB = 1.03f;
            g_Config.saturation = 0.78f;
            g_Config.contrast = 1.07f;
            g_Config.shadowDepth = 0.02f;
            g_Config.vignetteIntensity = 0.18f;
            break;
        case 3: // Subtle Cold Slate (Cinematic)
            g_Config.scaleR = 0.98f;
            g_Config.scaleG = 0.98f;
            g_Config.scaleB = 1.01f;
            g_Config.saturation = 0.88f;
            g_Config.contrast = 1.02f;
            g_Config.shadowDepth = 0.00f;
            g_Config.vignetteIntensity = 0.10f;
            break;
        case 4: // Custom (User Saved Preset)
            g_Config.scaleR = g_Config.customScaleR;
            g_Config.scaleG = g_Config.customScaleG;
            g_Config.scaleB = g_Config.customScaleB;
            g_Config.saturation = g_Config.customSaturation;
            g_Config.contrast = g_Config.customContrast;
            g_Config.shadowDepth = g_Config.customShadowDepth;
            g_Config.vignetteIntensity = g_Config.customVignetteIntensity;
            break;
        default:
            break;
        }
        Log("Applied preset %d (Scale=(%.2f, %.2f, %.2f), Sat=%.2f, Contrast=%.2f)",
            id, g_Config.scaleR, g_Config.scaleG, g_Config.scaleB, g_Config.saturation, g_Config.contrast);
        g_AdjDirty = true;
    }

    void LoadConfig()
    {
        std::string iniPath = GetModuleDir() + "VisualAtmosphere.ini";
        std::ifstream f(iniPath);
        if (!f.is_open())
        {
            ApplyPreset(0);
            Log("VisualAtmosphere.ini not found, default Bloodborne preset applied");
            return;
        }

        mINI::INIStructure ini;
        mINI::INIFile file(iniPath);
        if (!file.read(ini))
        {
            ApplyPreset(0);
            Log("Failed to read VisualAtmosphere.ini");
            return;
        }

        if (ini.has("CustomPreset"))
        {
            auto& cp = ini["CustomPreset"];
            if (cp.has("ScaleR")) g_Config.customScaleR = SafeStof(cp.get("ScaleR"), 0.96f);
            if (cp.has("ScaleG")) g_Config.customScaleG = SafeStof(cp.get("ScaleG"), 0.95f);
            if (cp.has("ScaleB")) g_Config.customScaleB = SafeStof(cp.get("ScaleB"), 1.00f);
            if (cp.has("Saturation")) g_Config.customSaturation = SafeStof(cp.get("Saturation"), 0.82f);
            if (cp.has("Contrast")) g_Config.customContrast = SafeStof(cp.get("Contrast"), 1.05f);
            if (cp.has("ShadowDepth")) g_Config.customShadowDepth = SafeStof(cp.get("ShadowDepth"), 0.01f);
            if (cp.has("VignetteIntensity")) g_Config.customVignetteIntensity = SafeStof(cp.get("VignetteIntensity"), 0.15f);
        }

        if (ini.has("Atmosphere"))
        {
            auto& s = ini["Atmosphere"];
            if (s.has("EnableAtmosphere"))
                g_Config.enableAtmosphere = (s.get("EnableAtmosphere") == "true" || s.get("EnableAtmosphere") == "1");

            int p = 0;
            if (s.has("Preset"))
            {
                p = SafeStoi(s.get("Preset"), 0);
            }
            ApplyPreset(p);

            // If active preset is custom (4), or if explicit values are provided
            if (p == 4)
            {
                if (s.has("ScaleR")) g_Config.scaleR = SafeStof(s.get("ScaleR"), g_Config.customScaleR);
                if (s.has("ScaleG")) g_Config.scaleG = SafeStof(s.get("ScaleG"), g_Config.customScaleG);
                if (s.has("ScaleB")) g_Config.scaleB = SafeStof(s.get("ScaleB"), g_Config.customScaleB);
                if (s.has("Saturation")) g_Config.saturation = SafeStof(s.get("Saturation"), g_Config.customSaturation);
                if (s.has("Contrast")) g_Config.contrast = SafeStof(s.get("Contrast"), g_Config.customContrast);
                if (s.has("ShadowDepth")) g_Config.shadowDepth = SafeStof(s.get("ShadowDepth"), g_Config.customShadowDepth);
                if (s.has("VignetteIntensity")) g_Config.vignetteIntensity = SafeStof(s.get("VignetteIntensity"), g_Config.customVignetteIntensity);
            }
        }

        if (ini.has("Hotkeys"))
        {
            auto& s = ini["Hotkeys"];
            if (s.has("MenuKey")) g_Config.menuKey = SafeStoi(s.get("MenuKey"), VK_F5);
        }

        g_Enabled = g_Config.enableAtmosphere;
        g_AdjDirty = true;
        Log("Config loaded: Enabled=%d, Preset=%d, MenuKey=0x%X, Scale=(%.2f, %.2f, %.2f), Sat=%.2f, Contrast=%.2f",
            g_Enabled ? 1 : 0, g_Config.activePreset, g_Config.menuKey,
            g_Config.scaleR, g_Config.scaleG, g_Config.scaleB, g_Config.saturation, g_Config.contrast);
    }

    void SaveConfig()
    {
        std::string iniPath = GetModuleDir() + "VisualAtmosphere.ini";
        mINI::INIStructure ini;
        mINI::INIFile file(iniPath);
        file.read(ini);

        ini["Atmosphere"]["EnableAtmosphere"] = g_Config.enableAtmosphere ? "true" : "false";
        ini["Atmosphere"]["Preset"] = std::to_string(g_Config.activePreset);
        ini["Atmosphere"]["ScaleR"] = std::to_string(g_Config.scaleR);
        ini["Atmosphere"]["ScaleG"] = std::to_string(g_Config.scaleG);
        ini["Atmosphere"]["ScaleB"] = std::to_string(g_Config.scaleB);
        ini["Atmosphere"]["Saturation"] = std::to_string(g_Config.saturation);
        ini["Atmosphere"]["Contrast"] = std::to_string(g_Config.contrast);
        ini["Atmosphere"]["ShadowDepth"] = std::to_string(g_Config.shadowDepth);
        ini["Atmosphere"]["VignetteIntensity"] = std::to_string(g_Config.vignetteIntensity);

        ini["CustomPreset"]["ScaleR"] = std::to_string(g_Config.customScaleR);
        ini["CustomPreset"]["ScaleG"] = std::to_string(g_Config.customScaleG);
        ini["CustomPreset"]["ScaleB"] = std::to_string(g_Config.customScaleB);
        ini["CustomPreset"]["Saturation"] = std::to_string(g_Config.customSaturation);
        ini["CustomPreset"]["Contrast"] = std::to_string(g_Config.customContrast);
        ini["CustomPreset"]["ShadowDepth"] = std::to_string(g_Config.customShadowDepth);
        ini["CustomPreset"]["VignetteIntensity"] = std::to_string(g_Config.customVignetteIntensity);

        char hexMenu[16];
        sprintf(hexMenu, "0x%X", g_Config.menuKey);
        ini["Hotkeys"].remove("ToggleKey");
        ini["Hotkeys"]["MenuKey"] = hexMenu;

        file.write(ini);
        Log("VisualAtmosphere.ini saved: MenuKey=%s, Preset=%d", hexMenu, g_Config.activePreset);
        g_AdjDirty = true;
    }

    static void RecomputeAdjMatrix()
    {
        float s = g_Config.saturation;
        float scaleR = g_Config.scaleR;
        float scaleG = g_Config.scaleG;
        float scaleB = g_Config.scaleB;
        float contrast = g_Config.contrast;
        float shadowOffset = -g_Config.shadowDepth * 0.2f;

        float rw = 0.3333f * (1.0f - s);
        float gw = 0.3333f * (1.0f - s);
        float bw = 0.3333f * (1.0f - s);

        float S[4][4] = {
            { rw + s, gw,     bw,     0.0f },
            { rw,     gw + s, bw,     0.0f },
            { rw,     gw,     bw + s, 0.0f },
            { 0.0f,   0.0f,   0.0f,   1.0f }
        };

        float T[4][4] = {
            { scaleR, 0.0f,   0.0f,   0.0f },
            { 0.0f,   scaleG, 0.0f,   0.0f },
            { 0.0f,   0.0f,   scaleB, 0.0f },
            { 0.0f,   0.0f,   0.0f,   1.0f }
        };

        float off = 0.18f * (1.0f - contrast) + shadowOffset;
        float C[4][4] = {
            { contrast, 0.0f,     0.0f,     0.0f },
            { 0.0f,     contrast, 0.0f,     0.0f },
            { 0.0f,     0.0f,     contrast, 0.0f },
            { off,      off,      off,      1.0f }
        };

        float ST[4][4] = { 0 };
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < 4; ++k)
                    ST[r][c] += S[r][k] * T[k][c];

        memset(g_CachedAdj, 0, sizeof(g_CachedAdj));
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < 4; ++k)
                    g_CachedAdj[r][c] += ST[r][k] * C[k][c];

        g_AdjDirty = false;
    }

    // The settings window (Overlay.cpp) writes g_Config directly, so slider
    // changes are detected here by comparing against the values last used.
    static bool AdjInputsChanged()
    {
        static float last[6] = { 0 };
        const float cur[6] = { g_Config.scaleR, g_Config.scaleG, g_Config.scaleB,
                               g_Config.saturation, g_Config.contrast, g_Config.shadowDepth };
        if (memcmp(last, cur, sizeof(cur)) == 0) return false;
        memcpy(last, cur, sizeof(cur));
        return true;
    }

    static void ApplyAtmosphereMatrix(float* pOutMatrix)
    {
        if (AdjInputsChanged() || g_AdjDirty) RecomputeAdjMatrix();

        float orig[4][4];
        memcpy(orig, pOutMatrix, sizeof(orig));

        float result[4][4];
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                result[r][c] = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    result[r][c] += orig[r][k] * g_CachedAdj[k][c];
                }
            }
        }

        memcpy(pOutMatrix, result, sizeof(result));
    }

    static void* __fastcall Hooked_ComputeColorMatrix(void* pParam, float* pOutMatrix)
    {
        void* ret = g_OrigComputeColorMatrix(pParam, pOutMatrix);
        Overlay::HoldGameInput();
        if (g_Config.enableAtmosphere && ret)
        {
            ApplyAtmosphereMatrix((float*)ret);
        }
        return ret;
    }

    static void* __fastcall Hooked_ComputeVignette(void* rcx, void* rdx, void* r8, void* r9, float arg5, float arg6)
    {
        void* ret = g_OrigComputeVignette(rcx, rdx, r8, r9, arg5, arg6);
        if (g_Config.enableAtmosphere && ret)
        {
            float* pVignette = (float*)ret;
            if (g_Config.vignetteIntensity > 0.0f)
            {
                pVignette[0] *= (1.0f + g_Config.vignetteIntensity * 1.5f);
            }
        }
        return ret;
    }

    static uintptr_t FindPattern(uintptr_t start, size_t length, const uint8_t* pattern, size_t patternLen)
    {
        for (size_t i = 0; i <= length - patternLen; ++i)
        {
            if (memcmp((const void*)(start + i), pattern, patternLen) == 0)
                return start + i;
        }
        return 0;
    }

    static bool InitHooks()
    {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        if (!base)
        {
            Log("Failed to get eldenring.exe base handle");
            return false;
        }

        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
        uintptr_t textStart = base + nt->OptionalHeader.BaseOfCode;
        size_t textSize = nt->OptionalHeader.SizeOfCode;

        Log("Scanning eldenring.exe .text (0x%llX - 0x%llX, size 0x%zX)...",
            (unsigned long long)textStart, (unsigned long long)(textStart + textSize), textSize);

        // 32-byte UNIQUE signature for Color Matrix computation (0x1B8DC30, RVA 0x1B8E630):
        const uint8_t colorSig[] = {
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
            0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x0F,
            0x10, 0x81, 0x90, 0x00, 0x00, 0x00, 0x4C, 0x8D
        };

        // 24-byte UNIQUE signature for Vignette computation (0x1B8DDD0, RVA 0x1B8E7D0):
        const uint8_t vignetteSig[] = {
            0x48, 0x8B, 0xC4, 0x53, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
            0x0F, 0x29, 0x78, 0xE8, 0x0F, 0x57, 0xC9, 0x44, 0x0F, 0x29, 0x40, 0xD8
        };

        g_ColorMatrixTarget = FindPattern(textStart, textSize, colorSig, sizeof(colorSig));
        g_VignetteTarget = FindPattern(textStart, textSize, vignetteSig, sizeof(vignetteSig));

        if (!g_ColorMatrixTarget)
        {
            Log("ERROR: ColorMatrix unique signature not found in eldenring.exe!");
            return false;
        }
        Log("ColorMatrix target confirmed at 0x%llX (RVA 0x%llX)",
            (unsigned long long)g_ColorMatrixTarget, (unsigned long long)(g_ColorMatrixTarget - base));

        if (!g_VignetteTarget)
        {
            Log("ERROR: Vignette unique signature not found in eldenring.exe!");
            return false;
        }
        Log("Vignette target confirmed at 0x%llX (RVA 0x%llX)",
            (unsigned long long)g_VignetteTarget, (unsigned long long)(g_VignetteTarget - base));

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            Log("ERROR: MH_Initialize failed with status %d", (int)status);
            return false;
        }

        status = MH_CreateHook((LPVOID)g_ColorMatrixTarget, (LPVOID)&Hooked_ComputeColorMatrix, (LPVOID*)&g_OrigComputeColorMatrix);
        if (status != MH_OK)
        {
            Log("ERROR: MH_CreateHook(ColorMatrix) failed with status %d", (int)status);
            return false;
        }

        status = MH_CreateHook((LPVOID)g_VignetteTarget, (LPVOID)&Hooked_ComputeVignette, (LPVOID*)&g_OrigComputeVignette);
        if (status != MH_OK)
        {
            Log("ERROR: MH_CreateHook(Vignette) failed with status %d", (int)status);
            return false;
        }

        status = MH_EnableHook(MH_ALL_HOOKS);
        if (status != MH_OK)
        {
            Log("ERROR: MH_EnableHook failed with status %d", (int)status);
            return false;
        }

        Log("SUCCESS: VisualAtmosphere hooks successfully verified and active!");
        return true;
    }

    static DWORD WINAPI WorkerThread(LPVOID lpParam)
    {
        Sleep(600);
        LoadConfig();

        if (!InitHooks())
        {
            Log("VisualAtmosphere initialization aborted due to hook errors");
            return 1;
        }

        // The colour hooks work without the settings window.
        if (!Overlay::Init())
        {
            Log("Settings window is unavailable, edit VisualAtmosphere.ini instead");
        }

        while (g_Running)
        {
            Sleep(200);
        }

        return 0;
    }

    bool Init(HMODULE hModule)
    {
        g_hModule = hModule;

        std::string logPath = GetModuleDir() + "VisualAtmosphere.log";
        DeleteFileA(logPath.c_str());

        Log("=== VisualAtmosphere Engine v2.6 (Live ColorMatrix Engine) ===");
        Log("Module base: 0x%llX", (unsigned long long)g_hModule);

        g_Running = true;
        g_hWorkerThread = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
        return g_hWorkerThread != NULL;
    }

    void Shutdown()
    {
        Overlay::Shutdown();
        g_Running = false;
        if (g_hWorkerThread)
        {
            WaitForSingleObject(g_hWorkerThread, 1000);
            CloseHandle(g_hWorkerThread);
            g_hWorkerThread = NULL;
        }

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        Log("VisualAtmosphere unhooked and shut down");
    }
}

extern "C"
{
    __declspec(dllexport) VA::AtmosphereConfig* VA_GetConfig()
    {
        return &VA::g_Config;
    }

    __declspec(dllexport) void VA_SaveConfig()
    {
        VA::SaveConfig();
    }

    __declspec(dllexport) void VA_ApplyPreset(int id)
    {
        VA::ApplyPreset(id);
    }

    __declspec(dllexport) void VA_Toggle()
    {
        VA::g_Enabled = !VA::g_Enabled;
        VA::g_Config.enableAtmosphere = VA::g_Enabled;
    }
}
