#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// when creating window
//glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);



// DX12 debug layer
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

void EnableDebugLayer() {
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
    }
#endif
}

int main()
{
	// enable debug layer
	EnableDebugLayer();

    // create device for GPU connection
    ComPtr<ID3D12Device> device;
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));

    // setup command queue
    ComPtr<ID3D12CommandQueue> queue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));

	// create window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "DX12", nullptr, nullptr);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}