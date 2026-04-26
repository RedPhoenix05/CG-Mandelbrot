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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

// Double-double (quad precision) support
struct dd_real
{
    double hi;
    double lo;
};

// Split a double for exact arithmetic
inline void Split(double a, double& hi, double& lo)
{
    const double SPLIT = 134217729.0; // 2^27 + 1
    double temp = SPLIT * a;
    hi = temp - (temp - a);
    lo = a - hi;
}

// Exact sum of two doubles
inline dd_real TwoSum(double a, double b)
{
    dd_real result;
    result.hi = a + b;
    double v = result.hi - a;
    result.lo = (a - (result.hi - v)) + (b - v);
    return result;
}

// Exact product of two doubles
inline dd_real TwoProduct(double a, double b)
{
    dd_real result;
    result.hi = a * b;

    double a_hi, a_lo, b_hi, b_lo;
    Split(a, a_hi, a_lo);
    Split(b, b_hi, b_lo);

    result.lo = ((a_hi * b_hi - result.hi) + a_hi * b_lo + a_lo * b_hi) + a_lo * b_lo;
    return result;
}

// Convert double to dd_real
inline dd_real ToDD(double a)
{
    dd_real result;
    result.hi = a;
    result.lo = 0.0;
    return result;
}

// Add dd_real and double
inline dd_real AddDD(dd_real a, double b)
{
    dd_real sum = TwoSum(a.hi, b);
    sum.lo += a.lo;
    // Renormalize
    dd_real result = TwoSum(sum.hi, sum.lo);
    return result;
}

// global vars
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<IDXGISwapChain3> swapchain;
ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12Resource> backBuffers[2];
constexpr UINT kFrameCount = 2;
UINT rtvDescriptorSize;
ComPtr<ID3D12CommandAllocator> commandAllocators[kFrameCount];
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12Fence> fence;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12PipelineState> pipelineState;
ComPtr<ID3D12DescriptorHeap> cbvHeap;
ComPtr<ID3D12Resource> constantBuffer;
ComPtr<ID3D12Resource> offscreenRenderTarget;
ComPtr<ID3D12Resource> readbackBuffer;
UINT8* mappedConstantBuffer = nullptr;
UINT64 fenceValue = 0;
UINT64 frameFenceValues[kFrameCount] = {};
HANDLE fenceEvent = nullptr;
UINT clientWidth = 1280;
UINT clientHeight = 720;
UINT configuredWindowWidth = 1920;
UINT configuredWindowHeight = 1080;
bool resizePending = false;
UINT pendingWidth = 1280;
UINT pendingHeight = 720;

enum class PrecisionMode
{
    Float32,    // USE_FP64=0
    Float64,    // USE_FP64=1, USE_FP128=0
    Float128    // USE_FP128=1
};

struct MandelbrotConstants
{
    // For FP128 mode, center uses 4 doubles (hi/lo for x and y)
    // For FP64/FP32 mode, only first 2 floats are used (as float2)
    union {
        struct {
            float centerX;
            float centerY;
        };
        struct {
            double centerX_hi;
            double centerX_lo;
            double centerY_hi;
            double centerY_lo;
        };
    };
    float scale;
    uint32_t maxIterations;
    float resolutionX;
    float resolutionY;
    float paletteCycle;
    float deaaStrength;
    float colorPeriod;
    float padding0;
    float padding0b;
    float padding0c;
    float colorA_R;
    float colorA_G;
    float colorA_B;
    float padding1;
    float colorB_R;
    float colorB_G;
    float colorB_B;
    float padding2;
    float colorC_R;
    float colorC_G;
    float colorC_B;
    float padding3;
};

MandelbrotConstants mandelbrotConstants = {
    -0.7436439f,
    0.1318259f,
    0.0025f,
    256u,
    1280.0f,
    720.0f,
    3.0f,
    1.0f,
    32.0f,
    0.0f,
    0.0f,
    0.0f,
    0.08f, 0.02f, 0.20f, 0.0f,
    0.10f, 0.55f, 0.95f, 0.0f,
    0.95f, 0.90f, 0.25f, 0.0f
};
float zoomFactorPerSecond = 0.94f;
float minScale = 0.0000001f;
float panSpeed = 0.75f;
uint32_t minIterations = 256u;
uint32_t maxIterationsCap = 8192u;
float iterationsPerZoomOctave = 128.0f;
float initialScale = 0.0025f;
bool requestDoublePrecision = true;
bool requestQuadPrecision = false; // Enable for FP128 mode
bool gpuSupportsDoublePrecision = false;
bool useDoublePrecisionShader = false;
bool useQuadPrecisionShader = false;
PrecisionMode currentPrecisionMode = PrecisionMode::Float32;
double centerX_hp = -0.7436439;
double centerY_hp = 0.1318259;
dd_real centerX_qp = ToDD(-0.7436439);
dd_real centerY_qp = ToDD(0.1318259);
float perturbationHintScale = 0.000000001f;
bool perturbationHintEmitted = false;
bool captureEnabled = false;
uint32_t captureEveryNFrames = 60u;
std::string captureDirectory = "captures";
uint64_t frameCounter = 0;
uint64_t capturedFrameCounter = 0;
UINT captureRowPitch = 0;
UINT64 captureBufferSize = 0;

void WaitForGpu();
void CreateOffscreenResources();
void CreateReadbackResources();
void WaitForFrame(UINT frameIndex);

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

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        gpuSupportsDoublePrecision = options.DoublePrecisionFloatShaderOps == TRUE;
    }

    // Determine precision mode
    if (requestQuadPrecision && gpuSupportsDoublePrecision)
    {
        useQuadPrecisionShader = true;
        useDoublePrecisionShader = false;
        currentPrecisionMode = PrecisionMode::Float128;
        OutputDebugStringA("Using quad-precision (FP128) shader mode.\n");
    }
    else if (requestDoublePrecision && gpuSupportsDoublePrecision)
    {
        useDoublePrecisionShader = true;
        useQuadPrecisionShader = false;
        currentPrecisionMode = PrecisionMode::Float64;
        OutputDebugStringA("Using double-precision (FP64) shader mode.\n");
    }
    else
    {
        useDoublePrecisionShader = false;
        useQuadPrecisionShader = false;
        currentPrecisionMode = PrecisionMode::Float32;
        if (requestDoublePrecision || requestQuadPrecision)
        {
            OutputDebugStringA("FP64 shader ops unsupported on this GPU; using float precision shader path.\n");
        }
    }
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
    scDesc.Width = clientWidth;
    scDesc.Height = clientHeight;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    factory->CreateSwapChainForHwnd(commandQueue.Get(), glfwGetWin32Window(window), &scDesc, nullptr, nullptr, &swapchain1);

    swapchain1.As(&swapchain);
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    pendingWidth = static_cast<UINT>(width);
    pendingHeight = static_cast<UINT>(height);
    resizePending = true;
}

// render target view
void RenderTargetView()
{
    // create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3;
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

void CreateOffscreenResources()
{
    offscreenRenderTarget.Reset();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = clientWidth;
    resourceDesc.Height = clientHeight;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        &clearValue,
        IID_PPV_ARGS(&offscreenRenderTarget));

    D3D12_CPU_DESCRIPTOR_HANDLE offscreenRtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    offscreenRtvHandle.ptr += 2 * rtvDescriptorSize;
    device->CreateRenderTargetView(offscreenRenderTarget.Get(), nullptr, offscreenRtvHandle);
}

void CreateReadbackResources()
{
    readbackBuffer.Reset();

    captureRowPitch = (clientWidth * 4u + 255u) & ~255u;
    captureBufferSize = static_cast<UINT64>(captureRowPitch) * static_cast<UINT64>(clientHeight);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = captureBufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));
}

void SaveFrameAsBmp(const uint8_t* pixels, UINT rowPitch, UINT width, UINT height, const std::string& filename)
{
    const uint32_t fileHeaderSize = 14u;
    const uint32_t infoHeaderSize = 40u;
    const uint32_t bytesPerPixel = 4u;
    const uint32_t imageSize = width * height * bytesPerPixel;
    const uint32_t fileSize = fileHeaderSize + infoHeaderSize + imageSize;

    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        return;
    }

    const uint8_t fileHeader[fileHeaderSize] = {
        'B', 'M',
        static_cast<uint8_t>(fileSize),
        static_cast<uint8_t>(fileSize >> 8),
        static_cast<uint8_t>(fileSize >> 16),
        static_cast<uint8_t>(fileSize >> 24),
        0, 0, 0, 0,
        static_cast<uint8_t>(fileHeaderSize + infoHeaderSize), 0, 0, 0
    };
    file.write(reinterpret_cast<const char*>(fileHeader), fileHeaderSize);

    const uint8_t infoHeader[infoHeaderSize] = {
        infoHeaderSize, 0, 0, 0,
        static_cast<uint8_t>(width),
        static_cast<uint8_t>(width >> 8),
        static_cast<uint8_t>(width >> 16),
        static_cast<uint8_t>(width >> 24),
        static_cast<uint8_t>(height),
        static_cast<uint8_t>(height >> 8),
        static_cast<uint8_t>(height >> 16),
        static_cast<uint8_t>(height >> 24),
        1, 0,
        32, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(imageSize),
        static_cast<uint8_t>(imageSize >> 8),
        static_cast<uint8_t>(imageSize >> 16),
        static_cast<uint8_t>(imageSize >> 24),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    file.write(reinterpret_cast<const char*>(infoHeader), infoHeaderSize);

    std::vector<uint8_t> outRow(width * bytesPerPixel);
    for (int y = static_cast<int>(height) - 1; y >= 0; --y)
    {
        const uint8_t* row = pixels + static_cast<size_t>(y) * rowPitch;

        for (UINT x = 0; x < width; ++x)
        {
            const uint8_t r = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2];
            const uint8_t a = row[x * 4 + 3];

            outRow[x * 4 + 0] = b;
            outRow[x * 4 + 1] = g;
            outRow[x * 4 + 2] = r;
            outRow[x * 4 + 3] = a;
        }

        file.write(reinterpret_cast<const char*>(outRow.data()), width * bytesPerPixel);
    }
}

void CaptureCurrentFrameIfRequested(bool shouldCapture, UINT frameIndex)
{
    if (!shouldCapture)
    {
        return;
    }

    const UINT64 valueToWaitFor = frameFenceValues[frameIndex];
    if (valueToWaitFor > 0 && fence->GetCompletedValue() < valueToWaitFor)
    {
        fence->SetEventOnCompletion(valueToWaitFor, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, captureBufferSize };
    if (FAILED(readbackBuffer->Map(0, &readRange, &mappedData)))
    {
        return;
    }

    CreateDirectoryA(captureDirectory.c_str(), nullptr);

    std::ostringstream filename;
    filename << captureDirectory << "\\frame_"
             << std::setw(6) << std::setfill('0') << capturedFrameCounter
             << ".bmp";
    SaveFrameAsBmp(static_cast<const uint8_t*>(mappedData), captureRowPitch, clientWidth, clientHeight, filename.str());
    capturedFrameCounter++;

    D3D12_RANGE writtenRange = { 0, 0 };
    readbackBuffer->Unmap(0, &writtenRange);
}

void UpdateAdaptiveIterations()
{
    const float safeScale = (std::max)(mandelbrotConstants.scale, minScale);
    const float zoomRatio = (std::max)(initialScale / safeScale, 1.0f);
    const float zoomDepth = log2f(zoomRatio);

    const float targetIterations = static_cast<float>(minIterations) + zoomDepth * iterationsPerZoomOctave;
    uint32_t adaptiveIterations = static_cast<uint32_t>(targetIterations);
    adaptiveIterations = (std::max)(minIterations, (std::min)(adaptiveIterations, maxIterationsCap));
    mandelbrotConstants.maxIterations = adaptiveIterations;
}

// command allocator
void CreateCommandAllocator()
{
    for (UINT i = 0; i < kFrameCount; i++)
    {
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i]));
    }

    // create command list
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&commandList));

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

void ResizeSwapChainIfNeeded()
{
    if (!resizePending)
    {
        return;
    }

    WaitForGpu();

    for (UINT i = 0; i < 2; i++)
    {
        backBuffers[i].Reset();
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapchain->GetDesc(&swapChainDesc);
    swapchain->ResizeBuffers(2, pendingWidth, pendingHeight, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; i++)
    {
        swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
        device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, handle);
        handle.ptr += rtvDescriptorSize;
    }

    clientWidth = pendingWidth;
    clientHeight = pendingHeight;
    CreateOffscreenResources();
    CreateReadbackResources();
    resizePending = false;
}

void UpdateConstantBuffer()
{
    mandelbrotConstants.resolutionX = static_cast<float>(clientWidth);
    mandelbrotConstants.resolutionY = static_cast<float>(clientHeight);

    // Update center based on precision mode
    if (currentPrecisionMode == PrecisionMode::Float128)
    {
        mandelbrotConstants.centerX_hi = centerX_qp.hi;
        mandelbrotConstants.centerX_lo = centerX_qp.lo;
        mandelbrotConstants.centerY_hi = centerY_qp.hi;
        mandelbrotConstants.centerY_lo = centerY_qp.lo;
    }
    else if (currentPrecisionMode == PrecisionMode::Float64)
    {
        mandelbrotConstants.centerX = static_cast<float>(centerX_hp);
        mandelbrotConstants.centerY = static_cast<float>(centerY_hp);
    }
    // else Float32 mode uses the values already in mandelbrotConstants

    std::memcpy(mappedConstantBuffer, &mandelbrotConstants, sizeof(mandelbrotConstants));
}

void UpdateAnimation(float deltaTime)
{
    mandelbrotConstants.scale *= powf(zoomFactorPerSecond, deltaTime);
    if (mandelbrotConstants.scale < minScale)
    {
        mandelbrotConstants.scale = minScale;
    }
}

void UpdateCenterFromInput(GLFWwindow* window, float deltaTime)
{
    const double step = static_cast<double>(panSpeed) * static_cast<double>(mandelbrotConstants.scale) * static_cast<double>(deltaTime);

    double dx = 0.0;
    double dy = 0.0;

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        dx -= step;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        dx += step;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        dy -= step;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        dy += step;
    }

    if (dx != 0.0 || dy != 0.0)
    {
        if (currentPrecisionMode == PrecisionMode::Float128)
        {
            centerX_qp = AddDD(centerX_qp, dx);
            centerY_qp = AddDD(centerY_qp, dy);
        }
        else if (currentPrecisionMode == PrecisionMode::Float64)
        {
            centerX_hp += dx;
            centerY_hp += dy;
        }
        else
        {
            mandelbrotConstants.centerX += static_cast<float>(dx);
            mandelbrotConstants.centerY += static_cast<float>(dy);
        }
    }
}

ComPtr<ID3DBlob> CompileShaderFromFile(const wchar_t* shaderPath, const char* entryPoint, const char* target, const D3D_SHADER_MACRO* defines)
{
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    D3DCompileFromFile(
        shaderPath,
        defines,
        nullptr,
        entryPoint,
        target,
        compileFlags,
        0,
        &bytecode,
        &errors);

    return bytecode;
}

void LoadConfig(const char* path)
{
    std::ifstream file(path);
    if (!file)
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const size_t separatorPos = line.find('=');
        if (separatorPos == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, separatorPos);
        const std::string value = line.substr(separatorPos + 1);

        if (key == "centerX")
        {
            double val = std::stod(value);
            mandelbrotConstants.centerX = static_cast<float>(val);
            centerX_hp = val;
            centerX_qp = ToDD(val);
        }
        else if (key == "centerY")
        {
            double val = std::stod(value);
            mandelbrotConstants.centerY = static_cast<float>(val);
            centerY_hp = val;
            centerY_qp = ToDD(val);
        }
        else if (key == "scale") mandelbrotConstants.scale = std::stof(value);
        else if (key == "maxIterations") maxIterationsCap = static_cast<uint32_t>(std::stoul(value));
        else if (key == "minIterations") minIterations = static_cast<uint32_t>(std::stoul(value));
        else if (key == "iterationsPerZoomOctave") iterationsPerZoomOctave = std::stof(value);
        else if (key == "zoomFactorPerSecond") zoomFactorPerSecond = std::stof(value);
        else if (key == "minScale") minScale = std::stof(value);
        else if (key == "panSpeed") panSpeed = std::stof(value);
        else if (key == "enableDoublePrecision") requestDoublePrecision = std::stoul(value) != 0;
        else if (key == "enableQuadPrecision") requestQuadPrecision = std::stoul(value) != 0;
        else if (key == "perturbationHintScale") perturbationHintScale = std::stof(value);
        else if (key == "paletteCycle") mandelbrotConstants.paletteCycle = std::stof(value);
        else if (key == "colorA_R") mandelbrotConstants.colorA_R = std::stof(value);
        else if (key == "colorA_G") mandelbrotConstants.colorA_G = std::stof(value);
        else if (key == "colorA_B") mandelbrotConstants.colorA_B = std::stof(value);
        else if (key == "colorB_R") mandelbrotConstants.colorB_R = std::stof(value);
        else if (key == "colorB_G") mandelbrotConstants.colorB_G = std::stof(value);
        else if (key == "colorB_B") mandelbrotConstants.colorB_B = std::stof(value);
        else if (key == "colorC_R") mandelbrotConstants.colorC_R = std::stof(value);
        else if (key == "colorC_G") mandelbrotConstants.colorC_G = std::stof(value);
        else if (key == "colorC_B") mandelbrotConstants.colorC_B = std::stof(value);
        else if (key == "deaaStrength") mandelbrotConstants.deaaStrength = std::stof(value);
        else if (key == "colorPeriod") mandelbrotConstants.colorPeriod = std::stof(value);
        else if (key == "captureEnabled") captureEnabled = std::stoul(value) != 0;
        else if (key == "captureEveryNFrames") captureEveryNFrames = static_cast<uint32_t>(std::stoul(value));
        else if (key == "captureDirectory") captureDirectory = value;
        else if (key == "resolutionX") configuredWindowWidth = (std::max)(1u, static_cast<UINT>(std::stoul(value)));
        else if (key == "resolutionY") configuredWindowHeight = (std::max)(1u, static_cast<UINT>(std::stoul(value)));
    }

    if (minIterations > maxIterationsCap)
    {
        minIterations = maxIterationsCap;
    }

    if (mandelbrotConstants.scale < minScale)
    {
        mandelbrotConstants.scale = minScale;
    }

    if (mandelbrotConstants.deaaStrength < 0.0f)
    {
        mandelbrotConstants.deaaStrength = 0.0f;
    }

    if (mandelbrotConstants.deaaStrength > 4.0f)
    {
        mandelbrotConstants.deaaStrength = 4.0f;
    }

    if (mandelbrotConstants.colorPeriod < 1.0f)
    {
        mandelbrotConstants.colorPeriod = 1.0f;
    }

    clientWidth = configuredWindowWidth;
    clientHeight = configuredWindowHeight;
    pendingWidth = configuredWindowWidth;
    pendingHeight = configuredWindowHeight;
    mandelbrotConstants.resolutionX = static_cast<float>(configuredWindowWidth);
    mandelbrotConstants.resolutionY = static_cast<float>(configuredWindowHeight);

    initialScale = mandelbrotConstants.scale;
    mandelbrotConstants.maxIterations = minIterations;
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

    D3D_SHADER_MACRO shaderDefines[] = {
        { "USE_FP64", (useDoublePrecisionShader || useQuadPrecisionShader) ? "1" : "0" },
        { "USE_FP128", useQuadPrecisionShader ? "1" : "0" },
        { nullptr, nullptr }
    };

    ComPtr<ID3DBlob> vsBytecode = CompileShaderFromFile(L"shaders/mandelbrot.hlsl", "VSMain", "vs_5_0", shaderDefines);
    ComPtr<ID3DBlob> psBytecode = CompileShaderFromFile(L"shaders/mandelbrot.hlsl", "PSMain", "ps_5_0", shaderDefines);

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

void WaitForFrame(UINT frameIndex)
{
    const UINT64 valueToWaitFor = frameFenceValues[frameIndex];
    if (valueToWaitFor == 0)
    {
        return;
    }

    if (fence->GetCompletedValue() < valueToWaitFor)
    {
        fence->SetEventOnCompletion(valueToWaitFor, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void Render(GLFWwindow* window, float deltaTime)
{
    ResizeSwapChainIfNeeded();

    UINT frameIndex = swapchain->GetCurrentBackBufferIndex();
    WaitForFrame(frameIndex);

    // reset command system
    commandAllocators[frameIndex]->Reset();
    commandList->Reset(commandAllocators[frameIndex].Get(), pipelineState.Get());

    // get current backbuffer RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandle =
        rtvHeap->GetCPUDescriptorHandleForHeapStart();
    backBufferRtvHandle.ptr += frameIndex * rtvDescriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE offscreenRtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    offscreenRtvHandle.ptr += 2 * rtvDescriptorSize;

    D3D12_RESOURCE_BARRIER offscreenToRenderBarrier = {};
    offscreenToRenderBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    offscreenToRenderBarrier.Transition.pResource = offscreenRenderTarget.Get();
    offscreenToRenderBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    offscreenToRenderBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    offscreenToRenderBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &offscreenToRenderBarrier);

    // bind offscreen render target
    commandList->OMSetRenderTargets(1, &offscreenRtvHandle, FALSE, nullptr);

    // viewport + scissor (required)
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(clientWidth);
    viewport.Height = static_cast<float>(clientHeight);
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    // clear screen
    const float color[4] = { 0.1f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(offscreenRtvHandle, color, 0, nullptr);

    UpdateCenterFromInput(window, deltaTime);
    UpdateAnimation(deltaTime);
    UpdateAdaptiveIterations();

    if (!perturbationHintEmitted && mandelbrotConstants.scale <= perturbationHintScale)
    {
        OutputDebugStringA("Deep-zoom precision limit zone reached; if artifacts appear, perturbation rendering is the next step.\n");
        perturbationHintEmitted = true;
    }

    UpdateConstantBuffer();

    commandList->SetGraphicsRootSignature(rootSignature.Get());
    ID3D12DescriptorHeap* descriptorHeaps[] = { cbvHeap.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    commandList->SetGraphicsRootDescriptorTable(0, cbvHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER offscreenToCopySourceBarrier = {};
    offscreenToCopySourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    offscreenToCopySourceBarrier.Transition.pResource = offscreenRenderTarget.Get();
    offscreenToCopySourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    offscreenToCopySourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    offscreenToCopySourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &offscreenToCopySourceBarrier);

    D3D12_RESOURCE_BARRIER backBufferToCopyDestBarrier = {};
    backBufferToCopyDestBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    backBufferToCopyDestBarrier.Transition.pResource = backBuffers[frameIndex].Get();
    backBufferToCopyDestBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    backBufferToCopyDestBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    backBufferToCopyDestBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &backBufferToCopyDestBarrier);

    commandList->CopyResource(backBuffers[frameIndex].Get(), offscreenRenderTarget.Get());

    const bool shouldCapture = captureEnabled && captureEveryNFrames > 0 && (frameCounter % captureEveryNFrames == 0);
    if (shouldCapture)
    {
        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = readbackBuffer.Get();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint.Offset = 0;
        dstLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dstLocation.PlacedFootprint.Footprint.Width = clientWidth;
        dstLocation.PlacedFootprint.Footprint.Height = clientHeight;
        dstLocation.PlacedFootprint.Footprint.Depth = 1;
        dstLocation.PlacedFootprint.Footprint.RowPitch = captureRowPitch;

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = offscreenRenderTarget.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = 0;

        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
    }

    D3D12_RESOURCE_BARRIER backBufferToPresentBarrier = {};
    backBufferToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    backBufferToPresentBarrier.Transition.pResource = backBuffers[frameIndex].Get();
    backBufferToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    backBufferToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    backBufferToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &backBufferToPresentBarrier);

    // execute
    commandList->Close();

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    const UINT64 signalValue = fenceValue;
    commandQueue->Signal(fence.Get(), signalValue);
    frameFenceValues[frameIndex] = signalValue;
    fenceValue++;

    // present
    swapchain->Present(1, 0);

    CaptureCurrentFrameIfRequested(shouldCapture, frameIndex);
    frameCounter++;
}

int main()
{
    LoadConfig("mandelbrot.ini");

    // create window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(configuredWindowWidth), static_cast<int>(configuredWindowHeight), "DX12", nullptr, nullptr);
    int frameWidth = static_cast<int>(configuredWindowWidth);
    int frameHeight = static_cast<int>(configuredWindowHeight);
    glfwGetFramebufferSize(window, &frameWidth, &frameHeight);
    clientWidth = static_cast<UINT>(frameWidth);
    clientHeight = static_cast<UINT>(frameHeight);
    pendingWidth = clientWidth;
    pendingHeight = clientHeight;
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

    EnableDebugLayer();
    CreateDevice();
    SetupCommandQueue();
    CreateSwapChain(window);
    RenderTargetView();
    CreateCommandAllocator();
    CreateFenceObjects();
    CreateConstantBufferResources();
    CreateOffscreenResources();
    CreateReadbackResources();
    CreateFullscreenPipeline();

    // main loop
    float previousTime = static_cast<float>(glfwGetTime());
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        const float currentTime = static_cast<float>(glfwGetTime());
        const float deltaTime = currentTime - previousTime;
        previousTime = currentTime;
        Render(window, deltaTime);
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