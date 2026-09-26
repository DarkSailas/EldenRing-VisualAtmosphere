#include "D3DRenderer.hpp"
#include "VisualAtmosphere.hpp"
#include "Minhook/MinHook.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"
#include <dxgi1_4.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace VA
{
    static HWND FindERWindow()
    {
        HWND found = FindWindowA("GRDirect3D12Window", NULL);
        if (found) return found;

        found = FindWindowA(NULL, "ELDEN RING™");
        if (found) return found;

        found = FindWindowA(NULL, "ELDEN RING");
        if (found) return found;

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                char title[256];
                GetWindowTextA(hwnd, title, sizeof(title));
                if (strstr(title, "ELDEN RING")) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE;
                }
                RECT r;
                GetWindowRect(hwnd, &r);
                if ((r.right - r.left) > 300 && (r.bottom - r.top) > 200) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&found));

        return found;
    }

    D3DRenderer::D3DRenderer()
    {
    }

    D3DRenderer::~D3DRenderer()
    {
        Unhook();
    }

    static uint64_t* MethodsTable = NULL;

    bool D3DRenderer::InitWindow()
    {
        WindowClass.cbSize = sizeof(WNDCLASSEX);
        WindowClass.style = CS_HREDRAW | CS_VREDRAW;
        WindowClass.lpfnWndProc = DefWindowProc;
        WindowClass.cbClsExtra = 0;
        WindowClass.cbWndExtra = 0;
        WindowClass.hInstance = GetModuleHandle(NULL);
        WindowClass.hIcon = NULL;
        WindowClass.hCursor = NULL;
        WindowClass.hbrBackground = NULL;
        WindowClass.lpszMenuName = NULL;
        WindowClass.lpszClassName = "VA_D3D_WINDOW";
        WindowClass.hIconSm = NULL;

        RegisterClassEx(&WindowClass);
        WindowHwnd = CreateWindow(WindowClass.lpszClassName, "VA Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, WindowClass.hInstance, NULL);
        return WindowHwnd != NULL;
    }

    bool D3DRenderer::DeleteWindow()
    {
        if (WindowHwnd)
        {
            DestroyWindow(WindowHwnd);
            UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
            WindowHwnd = NULL;
        }
        return true;
    }

    bool D3DRenderer::InitHook()
    {
        if (!InitWindow())
            return false;

        HMODULE D3D12Module = GetModuleHandle("d3d12.dll");
        HMODULE DXGIModule = GetModuleHandle("dxgi.dll");
        if (!D3D12Module || !DXGIModule)
        {
            DeleteWindow();
            return false;
        }

        void* CreateDXGIFactory = (void*)GetProcAddress(DXGIModule, "CreateDXGIFactory");
        if (!CreateDXGIFactory)
        {
            DeleteWindow();
            return false;
        }

        IDXGIFactory* Factory = nullptr;
        if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(IID_PPV_ARGS(&Factory)) < 0)
        {
            DeleteWindow();
            return false;
        }

        void* D3D12CreateDevice = (void*)GetProcAddress(D3D12Module, "D3D12CreateDevice");
        if (!D3D12CreateDevice)
        {
            Factory->Release();
            DeleteWindow();
            return false;
        }

        IDXGIAdapter* targetAdapter = nullptr;
        IDXGIAdapter* tempAdapter = nullptr;
        for (UINT i = 0; Factory->EnumAdapters(i, &tempAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(tempAdapter->GetDesc(&desc)))
            {
                if (desc.VendorId == 0x10DE) // NVIDIA GeForce
                {
                    targetAdapter = tempAdapter;
                    break;
                }
            }
            tempAdapter->Release();
        }

        ID3D12Device* Device = nullptr;
        if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(targetAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)) < 0)
        {
            if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)) < 0)
            {
                if (targetAdapter) targetAdapter->Release();
                Factory->Release();
                DeleteWindow();
                return false;
            }
        }
        if (targetAdapter) targetAdapter->Release();

        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* CommandQueue = nullptr;
        if (Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&CommandQueue)) < 0)
        {
            Device->Release();
            Factory->Release();
            DeleteWindow();
            return false;
        }

        ID3D12CommandAllocator* CommandAllocator = nullptr;
        if (Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator)) < 0)
        {
            CommandQueue->Release();
            Device->Release();
            Factory->Release();
            DeleteWindow();
            return false;
        }

        ID3D12GraphicsCommandList* CommandList = nullptr;
        if (Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, NULL, IID_PPV_ARGS(&CommandList)) < 0)
        {
            CommandAllocator->Release();
            CommandQueue->Release();
            Device->Release();
            Factory->Release();
            DeleteWindow();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
        SwapChainDesc.BufferDesc.Width = 100;
        SwapChainDesc.BufferDesc.Height = 100;
        SwapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
        SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        SwapChainDesc.SampleDesc.Count = 1;
        SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        SwapChainDesc.BufferCount = 2;
        SwapChainDesc.OutputWindow = WindowHwnd;
        SwapChainDesc.Windowed = 1;
        SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        IDXGISwapChain* SwapChain = nullptr;
        if (Factory->CreateSwapChain(CommandQueue, &SwapChainDesc, &SwapChain) < 0)
        {
            CommandList->Release();
            CommandAllocator->Release();
            CommandQueue->Release();
            Device->Release();
            Factory->Release();
            DeleteWindow();
            return false;
        }

        MethodsTable = (uint64_t*)::calloc(150, sizeof(uint64_t));
        memcpy(MethodsTable, *(uint64_t**)Device, 44 * sizeof(uint64_t));
        memcpy(MethodsTable + 44, *(uint64_t**)CommandQueue, 19 * sizeof(uint64_t));
        memcpy(MethodsTable + 44 + 19, *(uint64_t**)CommandAllocator, 9 * sizeof(uint64_t));
        memcpy(MethodsTable + 44 + 19 + 9, *(uint64_t**)CommandList, 60 * sizeof(uint64_t));
        memcpy(MethodsTable + 44 + 19 + 9 + 60, *(uint64_t**)SwapChain, 18 * sizeof(uint64_t));

        MH_Initialize();

        Device->Release();
        CommandQueue->Release();
        CommandAllocator->Release();
        CommandList->Release();
        SwapChain->Release();
        Factory->Release();
        DeleteWindow();
        return true;
    }

    bool D3DRenderer::Hook()
    {
        if (InitHook())
        {
            // Index 54: ExecuteCommandLists on ID3D12CommandQueue
            MH_CreateHook((void*)MethodsTable[54], (void*)HookExecuteCommandLists, (void**)&oExecuteCommandLists);
            // Index 140: Present on IDXGISwapChain
            MH_CreateHook((void*)MethodsTable[140], (void*)HookPresent, (void**)&oPresent);
            MH_EnableHook(MH_ALL_HOOKS);
            return true;
        }
        return false;
    }

    void D3DRenderer::Unhook()
    {
        if (m_GameWindow && m_OldWndProc)
        {
            SetWindowLongPtr(m_GameWindow, GWLP_WNDPROC, (LONG_PTR)m_OldWndProc);
        }
        MH_DisableHook(MH_ALL_HOOKS);
    }

    void D3DRenderer::HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
    {
        if (g_D3DRenderer && queue && queue != g_D3DRenderer->m_CommandQueue)
        {
            if (queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
                g_D3DRenderer->m_CommandQueue = queue;
        }
        g_D3DRenderer->oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
    }

    HRESULT APIENTRY D3DRenderer::HookPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        if (g_D3DRenderer)
        {
            g_D3DRenderer->Overlay(pSwapChain);
        }
        return g_D3DRenderer->oPresent(pSwapChain, SyncInterval, Flags);
    }

    void D3DRenderer::Overlay(IDXGISwapChain* pSwapChain)
    {
        if (pSwapChain == nullptr || m_CommandQueue == nullptr)
            return;

        // Re-initialize if swapchain pointer changed
        if (m_Init && m_CurrentSwapChain != pSwapChain)
        {
            if (m_Fence && m_FenceEvent && m_CommandQueue)
            {
                m_CommandQueue->Signal(m_Fence, ++m_FenceValue);
                if (m_Fence->GetCompletedValue() < m_FenceValue)
                {
                    m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent);
                    WaitForSingleObject(m_FenceEvent, 1000);
                }
            }

            if (m_BackBuffers)
            {
                for (UINT i = 0; i < m_BuffersCounts; i++)
                {
                    if (m_BackBuffers[i]) { m_BackBuffers[i]->Release(); m_BackBuffers[i] = nullptr; }
                    if (m_CommandAllocators && m_CommandAllocators[i]) { m_CommandAllocators[i]->Release(); m_CommandAllocators[i] = nullptr; }
                }
                delete[] m_BackBuffers; m_BackBuffers = nullptr;
                delete[] m_CommandAllocators; m_CommandAllocators = nullptr;
            }
            if (m_CommandList) { m_CommandList->Release(); m_CommandList = nullptr; }
            if (m_rtvDescriptorHeap) { m_rtvDescriptorHeap->Release(); m_rtvDescriptorHeap = nullptr; }
            if (m_srvDescriptorHeap) { m_srvDescriptorHeap->Release(); m_srvDescriptorHeap = nullptr; }

            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            if (ImGui::GetCurrentContext())
                ImGui::DestroyContext();

            m_Init = false;
        }
        m_CurrentSwapChain = pSwapChain;

        IDXGISwapChain3* pSwapChain3 = nullptr;
        if (FAILED(pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3))) || !pSwapChain3)
            return;

        DXGI_SWAP_CHAIN_DESC sc_desc;
        pSwapChain3->GetDesc(&sc_desc);

        if (!m_Init)
        {
            ID3D12Device* pDevice = nullptr;
            if (FAILED(pSwapChain3->GetDevice(IID_PPV_ARGS(&pDevice))) || !pDevice)
            {
                pSwapChain3->Release();
                return;
            }

            m_BuffersCounts = sc_desc.BufferCount;

            if (!m_Fence)
            {
                if (SUCCEEDED(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence))))
                {
                    m_FenceValue = 0;
                    m_FenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
                }
            }

            // SRV heap for ImGui font texture
            D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
            srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvDesc.NumDescriptors = 1;
            srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(pDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvDescriptorHeap))))
            {
                pDevice->Release();
                pSwapChain3->Release();
                return;
            }

            // RTV heap for backbuffers
            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = sc_desc.BufferCount;
            rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(pDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvDescriptorHeap))))
            {
                pDevice->Release();
                pSwapChain3->Release();
                m_srvDescriptorHeap->Release();
                return;
            }

            SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            m_RenderTargets.clear();
            m_CommandAllocators = new ID3D12CommandAllocator*[sc_desc.BufferCount];

            for (UINT i = 0; i < sc_desc.BufferCount; ++i)
            {
                m_RenderTargets.push_back(rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize;
                pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocators[i]));
            }

            pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[0], NULL, IID_PPV_ARGS(&m_CommandList));
            m_CommandList->Close();

            m_BackBuffers = new ID3D12Resource*[sc_desc.BufferCount];
            for (UINT i = 0; i < sc_desc.BufferCount; i++)
            {
                pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
                pDevice->CreateRenderTargetView(m_BackBuffers[i], NULL, m_RenderTargets[i]);
            }

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = NULL;

            m_GameWindow = sc_desc.OutputWindow ? sc_desc.OutputWindow : FindERWindow();
            if (m_GameWindow)
            {
                m_OldWndProc = (uint64_t)SetWindowLongPtr(m_GameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
            }

            ImGui_ImplWin32_Init(m_GameWindow);
            ImGui_ImplDX12_Init(pDevice, sc_desc.BufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, NULL,
                m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
            ImGui_ImplDX12_CreateDeviceObjects();

            m_Init = true;
            pDevice->Release();
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        VisualAtmosphere::Draw();

        ImGui::EndFrame();

        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_BackBuffers[bufferIndex];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        m_CommandAllocators[bufferIndex]->Reset();
        m_CommandList->Reset(m_CommandAllocators[bufferIndex], NULL);
        m_CommandList->ResourceBarrier(1, &barrier);
        m_CommandList->OMSetRenderTargets(1, &m_RenderTargets[bufferIndex], FALSE, NULL);
        m_CommandList->SetDescriptorHeaps(1, &m_srvDescriptorHeap);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CommandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_CommandList->ResourceBarrier(1, &barrier);
        m_CommandList->Close();

        m_CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&m_CommandList);

        pSwapChain3->Release();
    }

    LRESULT D3DRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN)
        {
            if (wParam == (WPARAM)AtmosphereConfig::toggleKey)
            {
                AtmosphereConfig::enableAtmosphere = !AtmosphereConfig::enableAtmosphere;
            }
            else if (wParam == (WPARAM)AtmosphereConfig::reloadKey)
            {
                VisualAtmosphere::LoadConfig();
            }
        }

        if (ImGui::GetCurrentContext())
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

        return CallWindowProc((WNDPROC)g_D3DRenderer->m_OldWndProc, hWnd, msg, wParam, lParam);
    }
}
