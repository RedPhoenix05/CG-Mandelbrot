#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// when creating window
//glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <Windows.h>

#include <cstring>
#include <utility>

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
ComPtr<ID3D12Fence> fence;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12PipelineState> pipelineState;
ComPtr<ID3D12DescriptorHeap> cbvHeap;
ComPtr<ID3D12Resource> constantBuffer;
UINT8* mappedConstantBuffer = nullptr;
UINT64 fenceValue = 0;
HANDLE fenceEvent = nullptr;

struct MandelbrotConstants
{
    float centerX;
    float centerY;
    float scale;
    UINT maxIterations;
};

MandelbrotConstants mandelbrotConstants = { -0.5f, 0.0f, 2.2f, 256 };

const char* kFullscreenShaderSource = R"(
struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 pos;
    if (vertexId == 0) pos = float2(-1.0, -1.0);
    else if (vertexId == 1) pos = float2(-1.0, 3.0);
    else pos = float2(3.0, -1.0);

    VSOutput output;
    output.position = float4(pos, 0.0, 1.0);
    output.uv = pos * 0.5 + 0.5;
    return output;
}

cbuffer MandelbrotParams : register(b0)
{
    float2 center;
    float scale;
    uint maxIterations;
};

float4 PSMain(VSOutput input) : SV_Target
{
    float aspect = 1280.0 / 720.0;
    float2 c = center + float2((input.uv.x * 2.0 - 1.0) * scale * aspect, (input.uv.y * 2.0 - 1.0) * scale);
    float2 z = float2(0.0, 0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        float x = z.x * z.x - z.y * z.y + c.x;
        float y = 2.0 * z.x * z.y + c.y;
        z = float2(x, y);

        if (dot(z, z) > 4.0)
        {
            break;
        }
    }

    if (i == maxIterations)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float t = (float)i / (float)maxIterations;
    return float4(0.15 + 0.85 * t, 0.2 * t * t, 0.4 + 0.6 * sqrt(t), 1.0);
}
)";

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

void CreateFenceObjects()
{
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceValue = 1;
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void CreateConstantBufferResources()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cbvHeap));

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = 256;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = 256;
    device->CreateConstantBufferView(&cbvDesc, cbvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RANGE readRange = { 0, 0 };
    constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedConstantBuffer));
}

void UpdateConstantBuffer()
{
    std::memcpy(mappedConstantBuffer, &mandelbrotConstants, sizeof(mandelbrotConstants));
}

ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target)
{
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    D3DCompile(
        kFullscreenShaderSource,
        strlen(kFullscreenShaderSource),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        compileFlags,
        0,
        &bytecode,
        &errors);

    return bytecode;
}

void CreateFullscreenPipeline()
{
    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptorRange.NumDescriptors = 1;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = 1;
    rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParameter;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> rootSigErrors;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &rootSigErrors);
    device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

    ComPtr<ID3DBlob> vsBytecode = CompileShader("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> psBytecode = CompileShader("PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vsBytecode->GetBufferPointer(), vsBytecode->GetBufferSize() };
    psoDesc.PS = { psBytecode->GetBufferPointer(), psBytecode->GetBufferSize() };
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
}

void WaitForGpu()
{
    const UINT64 signalValue = fenceValue;
    commandQueue->Signal(fence.Get(), signalValue);
    fenceValue++;

    if (fence->GetCompletedValue() < signalValue)
    {
        fence->SetEventOnCompletion(signalValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void Render()
{
    WaitForGpu();

    UINT frameIndex = swapchain->GetCurrentBackBufferIndex();

    // reset command system
    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), pipelineState.Get());

    // get current RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * rtvDescriptorSize;

    // transition PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffers[frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);

    // bind render target
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // viewport + scissor (required)
    D3D12_VIEWPORT viewport = {};
    viewport.Width = 1280.0f;
    viewport.Height = 720.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = { 0, 0, 1280, 720 };

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    // clear screen
    const float color[4] = { 0.1f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);

    UpdateConstantBuffer();

    commandList->SetGraphicsRootSignature(rootSignature.Get());
    ID3D12DescriptorHeap* descriptorHeaps[] = { cbvHeap.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    commandList->SetGraphicsRootDescriptorTable(0, cbvHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    // transition back RENDER_TARGET -> PRESENT
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList->ResourceBarrier(1, &barrier);

    // execute
    commandList->Close();

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // present
    swapchain->Present(1, 0);
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
    CreateFenceObjects();
    CreateConstantBufferResources();
    CreateFullscreenPipeline();

    // main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        Render();
    }

    WaitForGpu();
    if (constantBuffer)
    {
        constantBuffer->Unmap(0, nullptr);
        mappedConstantBuffer = nullptr;
    }
    if (fenceEvent)
    {
        CloseHandle(fenceEvent);
        fenceEvent = nullptr;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}