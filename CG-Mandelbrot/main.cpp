#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// when creating window
//glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

// global vars
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<IDXGISwapChain3> swapchain;
ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12Resource> backBuffers[2];
UINT rtvDescriptorSize;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList> commandList;

// enable debug layer
void EnableDebugLayer()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
    }
#endif
}

// create device for GPU connection
void CreateDevice()
{
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
}

// setup command queue
void SetupCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));
}

// create swap chain
void CreateSwapChain(GLFWwindow* window)
{
    ComPtr<IDXGISwapChain1> swapchain1;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    factory->CreateSwapChainForHwnd(commandQueue.Get(), glfwGetWin32Window(window), &scDesc, nullptr, nullptr, &swapchain1);

    swapchain1.As(&swapchain);
}

// render target view
void RenderTargetView()
{
    // create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 2;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap));

    // get descriptor size
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // get starting handle
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();

    // create RTVs for each back buffer
    for (UINT i = 0; i < 2; i++)
    {
        swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));

        device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, handle);

        handle.ptr += rtvDescriptorSize;
    }
}

// command allocator
void CreateCommandAllocator()
{
    // create allocator
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

    // create command list
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

    commandList->Close();
}

int main()
{
    // create window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "DX12", nullptr, nullptr);

    EnableDebugLayer();
    CreateDevice();
    SetupCommandQueue();
    CreateSwapChain(window);
    RenderTargetView();
    CreateCommandAllocator();

    // main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}