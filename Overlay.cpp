#include "VisualAtmosphere.hpp"
#include "Overlay.hpp"
#include "Minhook/MinHook.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx12.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace VA
{
    namespace Overlay
    {
        typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
        typedef void (WINAPI* ExecuteCommandLists_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
        typedef HRESULT(WINAPI* GetDeviceState_t)(void*, DWORD, LPVOID);

        static const UINT kMaxBuffers = 8;
        static const int kMaxSeenQueues = 16;

        static Present_t g_OrigPresent = nullptr;
        static ExecuteCommandLists_t g_OrigExecuteCommandLists = nullptr;
        static GetDeviceState_t g_OrigGetDeviceStateKeyboard = nullptr;
        static GetDeviceState_t g_OrigGetDeviceStateMouse = nullptr;

        static std::atomic<bool> g_MenuOpen{ false };
        static std::atomic<bool> g_BlockInput{ false };
        static std::atomic<bool> g_Disabled{ false };
        static std::atomic<bool> g_InPresent{ false };
        static std::atomic<int> g_Wheel{ 0 };

        // Address of the game's global that points at FD4PadManager.
        static std::atomic<uintptr_t*> g_PadSlot{ nullptr };
        static bool g_PadScanRetried = false;

        struct SeenQueue
        {
            ID3D12CommandQueue* queue;
            bool direct;
        };
        static SeenQueue g_Seen[kMaxSeenQueues];
        static std::atomic<int> g_SeenCount{ 0 };
        static int g_SeenNext = 0;
        static SRWLOCK g_SeenLock = SRWLOCK_INIT;
        static std::atomic<ID3D12CommandQueue*> g_LastDirect{ nullptr };

        struct Gfx
        {
            bool ready = false;
            bool backend = false;
            IDXGISwapChain* swapChain = nullptr; // identity only, no reference held
            HWND hwnd = NULL;
            UINT bufferCount = 0;
            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            ID3D12DescriptorHeap* rtvHeap = nullptr;
            ID3D12DescriptorHeap* srvHeap = nullptr;
            ID3D12CommandAllocator* allocators[kMaxBuffers] = {};
            UINT64 allocatorFence[kMaxBuffers] = {};
            ID3D12GraphicsCommandList* list = nullptr;
            ID3D12Fence* fence = nullptr;
            HANDLE fenceEvent = NULL;
            UINT64 fenceValue = 0;
            float scale = 1.0f;
        };
        static Gfx g;
        static int g_SetupFailures = 0;

        static HWND g_SubclassedWnd = NULL;
        static WNDPROC g_OldWndProc = nullptr;

        static RECT g_SavedClip = {};
        static bool g_HaveSavedClip = false;
        static bool g_PrevMenuKey = false;
        static bool g_PrevEscape = false;
        static LARGE_INTEGER g_LastTime = {};
        static LARGE_INTEGER g_TimeFreq = {};

        // ---------------------------------------------------------------- input block

        static bool FindPadSlot()
        {
            // mov rax, [rip+disp32] ; cmp byte ptr [rax+0x2F9], 0 ; jne ...
            static const uint8_t head[] = { 0x48, 0x8B, 0x05 };
            static const uint8_t tail[] = { 0x80, 0xB8, 0xF9, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x85 };

            uint8_t* base = (uint8_t*)GetModuleHandleW(NULL);
            if (!base) return false;
            IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
            IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
            IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

            for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s)
            {
                if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
                uint8_t* start = base + sec[s].VirtualAddress;
                size_t size = sec[s].Misc.VirtualSize;
                if (size < 16) continue;
                uint8_t* end = start + size - 16;

                for (uint8_t* p = start; p <= end; ++p)
                {
                    p = (uint8_t*)memchr(p, 0x48, (size_t)(end - p) + 1);
                    if (!p) break;
                    if (memcmp(p, head, sizeof(head)) != 0) continue;
                    if (memcmp(p + 7, tail, sizeof(tail)) != 0) continue;

                    int32_t disp;
                    memcpy(&disp, p + 3, sizeof(disp));
                    uintptr_t* slot = (uintptr_t*)(p + 7 + disp);
                    g_PadSlot.store(slot);
                    Log("Overlay: pad manager pointer at exe+0x%llX", (unsigned long long)((uint8_t*)slot - base));
                    return true;
                }
            }
            return false;
        }

        static bool IsWritable(const void* address, size_t size)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(address, &mbi, sizeof(mbi))) return false;
            if (mbi.State != MEM_COMMIT) return false;
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
            if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) return false;
            return (const uint8_t*)address + size <= (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
        }

        void HoldGameInput()
        {
            if (!g_BlockInput.load(std::memory_order_relaxed)) return;

            uintptr_t* slot = g_PadSlot.load(std::memory_order_relaxed);
            if (!slot) return;
            uint8_t* manager = (uint8_t*)*slot;
            if (!manager) return;

            static std::atomic<uint8_t*> validated{ nullptr };
            if (validated.load(std::memory_order_relaxed) != manager)
            {
                if (!IsWritable(manager + 0x2F8, 2)) return;
                validated.store(manager, std::memory_order_relaxed);
            }

            // The game copies 0x2F8 into 0x2F9 every update and skips input while 0x2F9 is set.
            manager[0x2F8] = 1;
            manager[0x2F9] = 1;
        }

        static void BlankDeviceState(DWORD size, LPVOID data)
        {
            if (!g_BlockInput.load(std::memory_order_relaxed)) return;
            HoldGameInput();
            // Keyboard (256), DIMOUSESTATE (16), DIMOUSESTATE2 (20). Zero is not neutral for joysticks.
            if (data && (size == 256 || size == sizeof(DIMOUSESTATE) || size == sizeof(DIMOUSESTATE2)))
            {
                memset(data, 0, size);
            }
        }

        static HRESULT WINAPI Hooked_GetDeviceStateKeyboard(void* self, DWORD size, LPVOID data)
        {
            HRESULT hr = g_OrigGetDeviceStateKeyboard(self, size, data);
            BlankDeviceState(size, data);
            return hr;
        }

        static HRESULT WINAPI Hooked_GetDeviceStateMouse(void* self, DWORD size, LPVOID data)
        {
            HRESULT hr = g_OrigGetDeviceStateMouse(self, size, data);
            BlankDeviceState(size, data);
            return hr;
        }

        static bool InstallDirectInputHooks()
        {
            IDirectInput8W* di = nullptr;
            HRESULT hr = DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, NULL);
            if (FAILED(hr) || !di)
            {
                Log("Overlay: DirectInput8Create failed (0x%08X)", (unsigned)hr);
                return false;
            }

            void* keyboardTarget = nullptr;
            void* mouseTarget = nullptr;
            IDirectInputDevice8W* dev = nullptr;
            if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &dev, NULL)) && dev)
            {
                keyboardTarget = (*(void***)dev)[9];
                dev->Release();
                dev = nullptr;
            }
            if (SUCCEEDED(di->CreateDevice(GUID_SysMouse, &dev, NULL)) && dev)
            {
                mouseTarget = (*(void***)dev)[9];
                dev->Release();
            }
            di->Release();

            bool ok = false;
            if (keyboardTarget
                && MH_CreateHook(keyboardTarget, (LPVOID)&Hooked_GetDeviceStateKeyboard, (LPVOID*)&g_OrigGetDeviceStateKeyboard) == MH_OK
                && MH_EnableHook(keyboardTarget) == MH_OK)
            {
                ok = true;
            }
            if (mouseTarget && mouseTarget != keyboardTarget
                && MH_CreateHook(mouseTarget, (LPVOID)&Hooked_GetDeviceStateMouse, (LPVOID*)&g_OrigGetDeviceStateMouse) == MH_OK
                && MH_EnableHook(mouseTarget) == MH_OK)
            {
                ok = true;
            }
            Log("Overlay: DirectInput GetDeviceState hook %s (keyboard %p, mouse %p)", ok ? "installed" : "failed", keyboardTarget, mouseTarget);
            return ok;
        }

        static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (g_BlockInput.load(std::memory_order_relaxed))
            {
                switch (msg)
                {
                case WM_MOUSEWHEEL:
                    g_Wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wParam));
                    return 0;
                case WM_MOUSEMOVE:
                case WM_MOUSEHWHEEL:
                case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
                case WM_KEYDOWN:
                case WM_CHAR:
                    return 0;
                case WM_INPUT:
                    return DefWindowProcW(hWnd, msg, wParam, lParam);
                case WM_SETCURSOR:
                    if (LOWORD(lParam) == HTCLIENT)
                    {
                        SetCursor(NULL); // the window draws its own cursor
                        return TRUE;
                    }
                    break;
                }
            }
            return CallWindowProcW(g_OldWndProc, hWnd, msg, wParam, lParam);
        }

        static void SubclassWindow(HWND hwnd)
        {
            if (!hwnd || hwnd == g_SubclassedWnd) return;
            if (g_SubclassedWnd && IsWindow(g_SubclassedWnd)) return;

            WNDPROC old = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&OverlayWndProc);
            if (!old)
            {
                Log("Overlay: window subclass failed (%lu)", GetLastError());
                return;
            }
            g_OldWndProc = old;
            g_SubclassedWnd = hwnd;
        }

        static bool IsGameForeground()
        {
            HWND fg = GetForegroundWindow();
            if (!fg) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            return pid == GetCurrentProcessId();
        }

        static bool KeyDown(int vk)
        {
            return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        static void OpenMenu()
        {
            if (!g_PadSlot.load() && !g_PadScanRetried)
            {
                g_PadScanRetried = true;
                if (!FindPadSlot()) Log("Overlay: pad manager signature not found, relying on DirectInput and window message blocking");
            }
            g_HaveSavedClip = GetClipCursor(&g_SavedClip) != FALSE;
            g_Wheel.store(0);
            g_LastTime.QuadPart = 0;
            g_BlockInput.store(true);
            g_MenuOpen.store(true);
        }

        static void CloseMenu()
        {
            g_MenuOpen.store(false);
        }

        // ---------------------------------------------------------------- graphics

        static void RememberQueue(ID3D12CommandQueue* queue)
        {
            AcquireSRWLockExclusive(&g_SeenLock);
            int count = g_SeenCount.load(std::memory_order_relaxed);
            bool known = false;
            for (int i = 0; i < count; ++i)
            {
                if (g_Seen[i].queue == queue) { known = true; break; }
            }
            if (!known)
            {
                D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
                int index = (count < kMaxSeenQueues) ? count : (g_SeenNext++ % kMaxSeenQueues);
                g_Seen[index].direct = desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
                g_Seen[index].queue = queue;
                if (count < kMaxSeenQueues) g_SeenCount.store(count + 1, std::memory_order_release);
                if (g_Seen[index].direct) g_LastDirect.store(queue);
            }
            ReleaseSRWLockExclusive(&g_SeenLock);
        }

        static void WINAPI Hooked_ExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
        {
            int seen = g_SeenCount.load(std::memory_order_acquire);
            bool known = false;
            for (int i = 0; i < seen; ++i)
            {
                if (g_Seen[i].queue == queue)
                {
                    known = true;
                    if (g_Seen[i].direct) g_LastDirect.store(queue, std::memory_order_relaxed);
                    break;
                }
            }
            if (!known) RememberQueue(queue);
            g_OrigExecuteCommandLists(queue, count, lists);
        }

        // The swap chain keeps a pointer to the queue it was created with; find it among the queues seen so far.
        static ID3D12CommandQueue* PickQueue(IDXGISwapChain* swapChain)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(swapChain, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT
                && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            {
                uintptr_t start = (uintptr_t)swapChain;
                uintptr_t end = start + 0x400;
                uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                if (end > regionEnd) end = regionEnd;

                int seen = g_SeenCount.load(std::memory_order_acquire);
                for (uintptr_t p = start; p + sizeof(void*) <= end; p += sizeof(void*))
                {
                    void* value = *(void**)p;
                    if (!value) continue;
                    for (int i = 0; i < seen; ++i)
                    {
                        if (g_Seen[i].direct && g_Seen[i].queue == value) return g_Seen[i].queue;
                    }
                }
            }
            return g_LastDirect.load();
        }

        static void WaitForFence(UINT64 value, DWORD timeoutMs)
        {
            if (!g.fence || !g.fenceEvent || value == 0) return;
            if (g.fence->GetCompletedValue() >= value) return;
            if (SUCCEEDED(g.fence->SetEventOnCompletion(value, g.fenceEvent)))
            {
                WaitForSingleObject(g.fenceEvent, timeoutMs);
            }
        }

        template <typename T>
        static void SafeRelease(T*& p)
        {
            if (p) { p->Release(); p = nullptr; }
        }

        static void DestroyGfx()
        {
            if (g.queue && g.fence)
            {
                if (SUCCEEDED(g.queue->Signal(g.fence, ++g.fenceValue))) WaitForFence(g.fenceValue, 500);
            }
            if (g.backend)
            {
                ImGui_ImplDX12_Shutdown();
                g.backend = false;
            }
            if (ImGui::GetCurrentContext()) ImGui::DestroyContext();

            SafeRelease(g.list);
            for (UINT i = 0; i < kMaxBuffers; ++i)
            {
                SafeRelease(g.allocators[i]);
                g.allocatorFence[i] = 0;
            }
            SafeRelease(g.fence);
            if (g.fenceEvent) { CloseHandle(g.fenceEvent); g.fenceEvent = NULL; }
            SafeRelease(g.srvHeap);
            SafeRelease(g.rtvHeap);
            SafeRelease(g.device);
            SafeRelease(g.queue);
            g.ready = false;
            g.swapChain = nullptr;
            g.fenceValue = 0;
        }

        static void SetupImGui(const DXGI_SWAP_CHAIN_DESC& desc)
        {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = NULL;
            io.LogFilename = NULL;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            float scale = (float)desc.BufferDesc.Height / 1080.0f;
            if (scale < 1.0f) scale = 1.0f;
            if (scale > 2.5f) scale = 2.5f;
            g.scale = scale;

            ImFont* font = nullptr;
            char winDir[MAX_PATH];
            UINT len = GetWindowsDirectoryA(winDir, MAX_PATH);
            if (len > 0 && len < MAX_PATH - 24)
            {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s\\Fonts\\segoeui.ttf", winDir);
                if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
                {
                    font = io.Fonts->AddFontFromFileTTF(path, 18.0f * scale);
                }
            }
            if (!font)
            {
                io.Fonts->AddFontDefault();
                io.FontGlobalScale = scale;
            }

            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 6.0f;
            style.FrameRounding = 4.0f;
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ScaleAllSizes(scale);
        }

        static bool CreateGfx(IDXGISwapChain* swapChain, const DXGI_SWAP_CHAIN_DESC& desc)
        {
            if (desc.BufferCount == 0 || desc.BufferCount > kMaxBuffers)
            {
                Log("Overlay: unsupported back buffer count %u", desc.BufferCount);
                return false;
            }

            g.queue = PickQueue(swapChain);
            if (!g.queue)
            {
                Log("Overlay: no DIRECT command queue seen yet");
                return false;
            }
            g.queue->AddRef();

            if (FAILED(g.queue->GetDevice(IID_PPV_ARGS(&g.device)))) { Log("Overlay: GetDevice failed"); return false; }

            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = 1;
            if (FAILED(g.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g.rtvHeap)))) { Log("Overlay: RTV heap failed"); return false; }

            D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
            srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvDesc.NumDescriptors = 1;
            srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g.device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g.srvHeap)))) { Log("Overlay: SRV heap failed"); return false; }

            for (UINT i = 0; i < desc.BufferCount; ++i)
            {
                if (FAILED(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.allocators[i]))))
                {
                    Log("Overlay: command allocator failed");
                    return false;
                }
            }
            if (FAILED(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.allocators[0], NULL, IID_PPV_ARGS(&g.list))))
            {
                Log("Overlay: command list failed");
                return false;
            }
            g.list->Close();

            if (FAILED(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)))) { Log("Overlay: fence failed"); return false; }
            g.fenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
            if (!g.fenceEvent) return false;

            SetupImGui(desc);
            if (!ImGui_ImplDX12_Init(g.device, (int)desc.BufferCount, desc.BufferDesc.Format, g.srvHeap,
                g.srvHeap->GetCPUDescriptorHandleForHeapStart(), g.srvHeap->GetGPUDescriptorHandleForHeapStart()))
            {
                Log("Overlay: ImGui DX12 backend init failed");
                return false;
            }
            g.backend = true;

            g.swapChain = swapChain;
            g.hwnd = desc.OutputWindow;
            g.bufferCount = desc.BufferCount;
            g.width = desc.BufferDesc.Width;
            g.height = desc.BufferDesc.Height;
            g.format = desc.BufferDesc.Format;
            g.ready = true;

            SubclassWindow(g.hwnd);
            Log("Overlay: ready (%ux%u, format %d, %u buffers, scale %.2f)", g.width, g.height, (int)g.format, g.bufferCount, g.scale);
            return true;
        }

        static bool EnsureGfx(IDXGISwapChain* swapChain, const DXGI_SWAP_CHAIN_DESC& desc)
        {
            if (g.ready && (g.swapChain != swapChain || g.bufferCount != desc.BufferCount || g.format != desc.BufferDesc.Format
                || g.width != desc.BufferDesc.Width || g.height != desc.BufferDesc.Height))
            {
                DestroyGfx();
            }
            if (g.ready) return true;

            if (!CreateGfx(swapChain, desc))
            {
                DestroyGfx();
                if (++g_SetupFailures >= 3)
                {
                    g_Disabled.store(true);
                    Log("Overlay: setup failed %d times, settings window disabled for this session", g_SetupFailures);
                }
                return false;
            }
            g_SetupFailures = 0;
            return true;
        }

        // ---------------------------------------------------------------- menu

        static void KeyName(int vk, char* out, size_t size)
        {
            UINT scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
            if (!scan || GetKeyNameTextA((LONG)(scan << 16), out, (int)size) <= 0)
            {
                snprintf(out, size, "0x%X", vk);
            }
        }

        static void DrawMenu()
        {
            const float s = g.scale;
            ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowSize(ImVec2(450.0f * s, 520.0f * s), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

            bool open = true;
            if (ImGui::Begin("Visual Atmosphere  |  Dark Fantasy Engine v2.6", &open, ImGuiWindowFlags_NoCollapse))
            {
                AtmosphereConfig& cfg = g_Config;
                const ImVec4 accent(0.88f, 0.76f, 0.48f, 1.0f);

                if (ImGui::Checkbox("Enable Visual Atmosphere", &cfg.enableAtmosphere))
                {
                    g_Enabled = cfg.enableAtmosphere;
                    SaveConfig();
                }

                ImGui::Separator();

                static const char* presets[] = {
                    "0. Bloodborne / Yharnam Noir",
                    "1. Dark Souls 3 / Lothric Ashen",
                    "2. Demon's Souls / Boletaria Deep Cold",
                    "3. Cold Slate (Cinematic)",
                    "4. Custom"
                };
                int preset = cfg.activePreset;
                if (preset < 0 || preset > 4) preset = 4;
                if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets)))
                {
                    ApplyPreset(preset);
                    SaveConfig();
                }

                ImGui::Separator();
                ImGui::TextColored(accent, "RGB Channel Multipliers");
                bool changed = false;
                changed |= ImGui::SliderFloat("Red", &cfg.scaleR, 0.50f, 1.50f, "%.3f");
                changed |= ImGui::SliderFloat("Green", &cfg.scaleG, 0.50f, 1.50f, "%.3f");
                changed |= ImGui::SliderFloat("Blue", &cfg.scaleB, 0.50f, 1.50f, "%.3f");

                ImGui::Separator();
                ImGui::TextColored(accent, "Tonal & Post-Processing Grading");
                changed |= ImGui::SliderFloat("Saturation", &cfg.saturation, 0.00f, 2.00f, "%.2f");
                changed |= ImGui::SliderFloat("Contrast", &cfg.contrast, 0.50f, 2.00f, "%.2f");
                changed |= ImGui::SliderFloat("Shadow Depth", &cfg.shadowDepth, 0.00f, 0.15f, "%.3f");
                changed |= ImGui::SliderFloat("Vignette", &cfg.vignetteIntensity, 0.00f, 0.50f, "%.2f");

                if (changed && cfg.activePreset != 4) cfg.activePreset = 4;

                ImGui::Separator();
                if (ImGui::Button("Save as Custom", ImVec2(200.0f * s, 26.0f * s)))
                {
                    cfg.customScaleR = cfg.scaleR;
                    cfg.customScaleG = cfg.scaleG;
                    cfg.customScaleB = cfg.scaleB;
                    cfg.customSaturation = cfg.saturation;
                    cfg.customContrast = cfg.contrast;
                    cfg.customShadowDepth = cfg.shadowDepth;
                    cfg.customVignetteIntensity = cfg.vignetteIntensity;
                    cfg.activePreset = 4;
                    SaveConfig();
                }
                ImGui::SameLine();
                if (ImGui::Button("Save to INI")) SaveConfig();

                if (ImGui::Button("Reset to Bloodborne"))
                {
                    ApplyPreset(0);
                    SaveConfig();
                }
                ImGui::SameLine();
                char key[32];
                KeyName(cfg.menuKey, key, sizeof(key));
                char label[64];
                snprintf(label, sizeof(label), "Close (%s)", key);
                if (ImGui::Button(label)) open = false;

                ImGui::Separator();
                ImGui::TextDisabled("Esc also closes. The game gets no input while this window is open.");
            }
            ImGui::End();

            if (!open) CloseMenu();
        }

        static void FeedInput(const DXGI_SWAP_CHAIN_DESC& desc)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)desc.BufferDesc.Width, (float)desc.BufferDesc.Height);

            if (!g_TimeFreq.QuadPart) QueryPerformanceFrequency(&g_TimeFreq);
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float dt = 1.0f / 60.0f;
            if (g_LastTime.QuadPart && g_TimeFreq.QuadPart)
            {
                dt = (float)((double)(now.QuadPart - g_LastTime.QuadPart) / (double)g_TimeFreq.QuadPart);
            }
            g_LastTime = now;
            if (dt < 0.0005f) dt = 0.0005f;
            if (dt > 0.25f) dt = 0.25f;
            io.DeltaTime = dt;

            const bool focused = IsGameForeground();
            POINT pt;
            RECT rc;
            if (focused && GetCursorPos(&pt) && ScreenToClient(g.hwnd, &pt) && GetClientRect(g.hwnd, &rc) && rc.right > 0 && rc.bottom > 0)
            {
                io.MousePos = ImVec2((float)pt.x * io.DisplaySize.x / (float)rc.right, (float)pt.y * io.DisplaySize.y / (float)rc.bottom);
            }
            else
            {
                io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            }
            io.MouseDown[0] = focused && KeyDown(VK_LBUTTON);
            io.MouseDown[1] = focused && KeyDown(VK_RBUTTON);
            io.MouseDown[2] = focused && KeyDown(VK_MBUTTON);
            io.MouseWheel += (float)g_Wheel.exchange(0) / (float)WHEEL_DELTA;
            io.MouseDrawCursor = true;
        }

        static void RenderFrame(IDXGISwapChain3* swapChain, const DXGI_SWAP_CHAIN_DESC& desc)
        {
            UINT index = swapChain->GetCurrentBackBufferIndex();
            if (index >= g.bufferCount) return;

            ID3D12Resource* backBuffer = nullptr;
            if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer))) || !backBuffer) return;

            FeedInput(desc);
            ImGui_ImplDX12_NewFrame();
            ImGui::NewFrame();
            DrawMenu();
            ImGui::Render();

            WaitForFence(g.allocatorFence[index], 200);
            ID3D12CommandAllocator* allocator = g.allocators[index];
            if (SUCCEEDED(allocator->Reset()) && SUCCEEDED(g.list->Reset(allocator, NULL)))
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = backBuffer;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                g.list->ResourceBarrier(1, &barrier);

                D3D12_CPU_DESCRIPTOR_HANDLE rtv = g.rtvHeap->GetCPUDescriptorHandleForHeapStart();
                g.device->CreateRenderTargetView(backBuffer, NULL, rtv);
                g.list->OMSetRenderTargets(1, &rtv, FALSE, NULL);
                g.list->SetDescriptorHeaps(1, &g.srvHeap);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g.list);

                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                g.list->ResourceBarrier(1, &barrier);
                g.list->Close();

                ID3D12CommandList* lists[] = { g.list };
                g_OrigExecuteCommandLists(g.queue, 1, lists);
                if (SUCCEEDED(g.queue->Signal(g.fence, ++g.fenceValue))) g.allocatorFence[index] = g.fenceValue;
            }

            backBuffer->Release(); // no back buffer references are kept, so ResizeBuffers keeps working
        }

        static void Tick(IDXGISwapChain* swapChain)
        {
            const int menuKey = g_Config.menuKey;
            const bool focused = IsGameForeground();
            const bool menuDown = focused && KeyDown(menuKey);
            const bool escDown = focused && KeyDown(VK_ESCAPE);

            if (menuDown && !g_PrevMenuKey)
            {
                if (g_MenuOpen.load()) CloseMenu(); else OpenMenu();
            }
            else if (escDown && !g_PrevEscape && g_MenuOpen.load())
            {
                CloseMenu();
            }
            g_PrevMenuKey = menuDown;
            g_PrevEscape = escDown;

            if (g_MenuOpen.load())
            {
                bool drawn = false;
                IDXGISwapChain3* sc3 = nullptr;
                DXGI_SWAP_CHAIN_DESC desc;
                if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3)
                {
                    if (SUCCEEDED(sc3->GetDesc(&desc)) && EnsureGfx(swapChain, desc))
                    {
                        RenderFrame(sc3, desc);
                        drawn = true;
                    }
                    sc3->Release();
                }
                // Never hold the game's input without a visible window.
                if (!drawn) CloseMenu();
            }

            if (g_MenuOpen.load())
            {
                ClipCursor(NULL);
                HoldGameInput();
            }
            else if (g_BlockInput.load())
            {
                // Keep blocking until the keys that closed the window are released, so the game does not see them.
                if (KeyDown(VK_ESCAPE) || KeyDown(menuKey) || KeyDown(VK_LBUTTON))
                {
                    HoldGameInput();
                }
                else
                {
                    g_BlockInput.store(false);
                    if (g_HaveSavedClip) ClipCursor(&g_SavedClip);
                    g_HaveSavedClip = false;
                }
            }
        }

        static HRESULT WINAPI Hooked_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
        {
            if (!(flags & DXGI_PRESENT_TEST) && !g_Disabled.load(std::memory_order_relaxed)
                && !g_InPresent.exchange(true))
            {
                Tick(swapChain);
                g_InPresent.store(false);
            }
            return g_OrigPresent(swapChain, syncInterval, flags);
        }

        static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            RECT r = {};
            GetClientRect(hwnd, &r);
            if (r.right - r.left < 320 || r.bottom - r.top < 240) return TRUE;
            *(HWND*)lParam = hwnd;
            return FALSE;
        }

        // The helper device below must not be created while the game and Streamline (ERSS-FG) are still
        // setting up their own: doing it 0.6 s after load crashed inside sl.interposer.dll.
        static void WaitForGameRenderer()
        {
            HWND hwnd = NULL;
            while (!hwnd)
            {
                EnumWindows(FindGameWindowProc, (LPARAM)&hwnd);
                if (!hwnd) Sleep(500);
            }
            Sleep(7000);
        }

        // Creates a throwaway device and swap chain to read the addresses of Present and ExecuteCommandLists.
        static bool ResolveGraphicsTargets(void** presentOut, void** executeOut)
        {
            bool ok = false;
            HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
            IDXGIFactory1* factory = nullptr;
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            IDXGISwapChain* swapChain = nullptr;

            do
            {
                if (!hwnd) { Log("Overlay: helper window failed (%lu)", GetLastError()); break; }
                if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { Log("Overlay: CreateDXGIFactory1 failed"); break; }

                HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
                if (FAILED(hr))
                {
                    IDXGIAdapter1* adapter = nullptr;
                    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
                    {
                        hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
                        adapter->Release();
                        if (SUCCEEDED(hr)) break;
                    }
                }
                if (FAILED(hr) || !device) { Log("Overlay: D3D12CreateDevice failed (0x%08X)", (unsigned)hr); break; }

                D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) { Log("Overlay: helper queue failed"); break; }

                DXGI_SWAP_CHAIN_DESC desc = {};
                desc.BufferDesc.Width = 100;
                desc.BufferDesc.Height = 100;
                desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.OutputWindow = hwnd;
                desc.Windowed = TRUE;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                hr = factory->CreateSwapChain(queue, &desc, &swapChain);
                if (FAILED(hr) || !swapChain) { Log("Overlay: helper swap chain failed (0x%08X)", (unsigned)hr); break; }

                *presentOut = (*(void***)swapChain)[8];
                *executeOut = (*(void***)queue)[10];
                ok = true;
            } while (false);

            if (swapChain) swapChain->Release();
            if (queue) queue->Release();
            if (device) device->Release();
            if (factory) factory->Release();
            if (hwnd) DestroyWindow(hwnd);
            return ok;
        }

        bool Init()
        {
            if (g_Config.menuKey <= 0)
            {
                Log("Overlay: MenuKey is 0, settings window is off");
                return true;
            }

            if (!FindPadSlot()) Log("Overlay: pad manager signature not found yet, will retry when the window opens");

            WaitForGameRenderer();
            Log("Overlay: game window is up, resolving Present and ExecuteCommandLists");

            void* present = nullptr;
            void* execute = nullptr;
            if (!ResolveGraphicsTargets(&present, &execute)) return false;

            if (MH_CreateHook(execute, (LPVOID)&Hooked_ExecuteCommandLists, (LPVOID*)&g_OrigExecuteCommandLists) != MH_OK
                || MH_EnableHook(execute) != MH_OK)
            {
                Log("Overlay: ExecuteCommandLists hook failed");
                return false;
            }
            if (MH_CreateHook(present, (LPVOID)&Hooked_Present, (LPVOID*)&g_OrigPresent) != MH_OK
                || MH_EnableHook(present) != MH_OK)
            {
                Log("Overlay: Present hook failed");
                return false;
            }

            InstallDirectInputHooks();
            Log("Overlay: hooks installed, settings window on key 0x%X", g_Config.menuKey);
            return true;
        }

        void Shutdown()
        {
            // Process is going away: release the game's input, leave D3D objects to the OS.
            g_Disabled.store(true);
            g_MenuOpen.store(false);
            g_BlockInput.store(false);
        }
    }
}
