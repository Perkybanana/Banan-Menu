// Test host: a window with a DX11 or DX12 swapchain that loads the trainer DLL, opens the menu with a posted Insert key
// and saves a screenshot of its client area. Usage: overlay_host <dx11|dx12> <path\to\winmm.dll> <out.bmp> [width height]
// No global input is injected, and the window never steals focus.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cwchar>
#include <vector>

static void SaveClientBmp(HWND hwnd, const wchar_t* path)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    HDC wdc = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    SelectObject(mem, old);
    BITMAPINFOHEADER bi{sizeof(bi), w, -h, 1, 32, BI_RGB};
    std::vector<BYTE> px(size_t(w) * h * 4);
    GetDIBits(mem, bmp, 0, h, px.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    BITMAPFILEHEADER fh{0x4D42, DWORD(sizeof(fh) + sizeof(bi) + px.size()), 0, 0, DWORD(sizeof(fh) + sizeof(bi))};
    if (FILE* f = _wfopen(path, L"wb")) {
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi, sizeof(bi), 1, f);
        fwrite(px.data(), 1, px.size(), f);
        fclose(f);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, wdc);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) return 2;
    bool dx12 = wcscmp(argv[1], L"dx12") == 0;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"OverlayHost";
    RegisterClassExW(&wc);
    int width = argc > 5 ? _wtoi(argv[4]) : 1280, height = argc > 5 ? _wtoi(argv[5]) : 720;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"overlay_host", WS_OVERLAPPEDWINDOW, 0, 0, width, height,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    // DX11
    IDXGISwapChain* sc11 = nullptr;
    ID3D11Device* dev11 = nullptr;
    ID3D11DeviceContext* ctx11 = nullptr;
    // DX12
    constexpr UINT N = 3;
    ID3D12Device* dev12 = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain3* sc12 = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12CommandAllocator* alloc[N] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValues[N] = {}, fenceCounter = 0;
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT rtvStride = 0;

    if (!dx12) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                 D3D11_SDK_VERSION, &sd, &sc11, &dev11, nullptr, &ctx11)))
            return printf("FAIL dx11 init\n"), 1;
    } else {
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12)))) return printf("FAIL dx12 device\n"), 1;
        D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
        dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
        IDXGIFactory4* factory = nullptr;
        CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = N;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.SampleDesc.Count = 1;
        IDXGISwapChain1* sc1 = nullptr;
        if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1))) return printf("FAIL dx12 swapchain\n"), 1;
        sc1->QueryInterface(IID_PPV_ARGS(&sc12));
        sc1->Release();
        factory->Release();
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, N};
        dev12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap));
        rtvStride = dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (auto& a : alloc) dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a));
        dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0], nullptr, IID_PPV_ARGS(&list));
        list->Close();
        dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    }

    if (!LoadLibraryW(argv[2])) return printf("FAIL LoadLibrary %lu\n", GetLastError()), 1;

    const float color[4] = {0.03f, 0.03f, 0.04f, 1.0f}; // dark, so the menu's rounded corners crop cleanly
    for (int frame = 0; frame < 240; frame++) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (frame == 90) PostMessageW(hwnd, WM_KEYDOWN, VK_INSERT, 1);
        if (frame == 92) PostMessageW(hwnd, WM_KEYUP, VK_INSERT, 0xC0000001);
        if (frame == 200) SaveClientBmp(hwnd, argv[3]);

        if (!dx12) {
            ID3D11Texture2D* buf = nullptr;
            sc11->GetBuffer(0, IID_PPV_ARGS(&buf));
            ID3D11RenderTargetView* rtv = nullptr;
            dev11->CreateRenderTargetView(buf, nullptr, &rtv);
            ctx11->ClearRenderTargetView(rtv, color);
            rtv->Release();
            buf->Release();
            sc11->Present(1, 0);
        } else {
            UINT i = sc12->GetCurrentBackBufferIndex();
            if (fence->GetCompletedValue() < fenceValues[i]) {
                fence->SetEventOnCompletion(fenceValues[i], fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }
            ID3D12Resource* buf = nullptr;
            sc12->GetBuffer(i, IID_PPV_ARGS(&buf));
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(i) * rtvStride};
            dev12->CreateRenderTargetView(buf, nullptr, rtv);
            alloc[i]->Reset();
            list->Reset(alloc[i], nullptr);
            D3D12_RESOURCE_BARRIER b{D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
            b.Transition = {buf, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                            D3D12_RESOURCE_STATE_RENDER_TARGET};
            list->ResourceBarrier(1, &b);
            list->ClearRenderTargetView(rtv, color, 0, nullptr);
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            list->ResourceBarrier(1, &b);
            list->Close();
            ID3D12CommandList* lists[] = {list};
            queue->ExecuteCommandLists(1, lists);
            sc12->Present(1, 0);
            queue->Signal(fence, ++fenceCounter);
            fenceValues[i] = fenceCounter;
            buf->Release();
        }
    }
    if (dx12) {
        fence->SetEventOnCompletion(fenceCounter, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    printf("DONE\n");
    ExitProcess(0); // the trainer thread may still be running; skip normal teardown
}
