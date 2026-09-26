#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>
#include <string>
#include <memory>

namespace VA
{
    class D3DRenderer
    {
        typedef HRESULT(APIENTRY* Present12)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        Present12 oPresent = NULL;

        typedef void(APIENTRY* ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
        ExecuteCommandLists oExecuteCommandLists = NULL;

    public:
        D3DRenderer();
        ~D3DRenderer();

        bool m_Init = false;
        ID3D12CommandQueue* m_CommandQueue = nullptr;
        uint64_t m_OldWndProc = 0;

        bool Hook();
        void Unhook();
        void Overlay(IDXGISwapChain* pSwapChain);

        static HRESULT APIENTRY HookPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static void HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
        static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:
        bool InitHook();
        bool InitWindow();
        bool DeleteWindow();

        HWND m_GameWindow = NULL;
        WNDCLASSEX WindowClass{};
        HWND WindowHwnd = NULL;

        IDXGISwapChain* m_CurrentSwapChain = nullptr;
        UINT m_BuffersCounts = 0;
        ID3D12DescriptorHeap* m_srvDescriptorHeap = nullptr;
        ID3D12DescriptorHeap* m_rtvDescriptorHeap = nullptr;
        ID3D12CommandAllocator** m_CommandAllocators = nullptr;
        ID3D12GraphicsCommandList* m_CommandList = nullptr;
        ID3D12Resource** m_BackBuffers = nullptr;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RenderTargets;

        ID3D12Fence* m_Fence = nullptr;
        HANDLE m_FenceEvent = NULL;
        UINT64 m_FenceValue = 0;
    };

    inline std::unique_ptr<D3DRenderer> g_D3DRenderer;
}
