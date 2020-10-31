#include <iostream>
#include <string>

// Direct X
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

// Windows, COM and Detours
#include <Windows.h>
#include <comdef.h>
#include <detours.h>
#pragma comment(lib, "detours.lib")

// ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// ImGui definitions
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Direct X definitions
typedef HRESULT(__stdcall* IDXGISwapChainPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
IDXGISwapChainPresent fnIDXGISwapChainPresent;

// Render pipeline
ID3D11Device* pDevice;
IDXGISwapChain* pSwapchain;
ID3D11DeviceContext* pContext;
ID3D11RenderTargetView* pRenderTargetView;
HWND hWindow;
bool bGraphicsInitialised = false;

// Window and input
WNDPROC wndProcHandlerOriginal;
LRESULT CALLBACK hWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

bool InitDirectXAndInput(IDXGISwapChain* pChain);

HRESULT __stdcall IDXGISwapChain_Present(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags)
{
	// Get device and swapchain
	if (!bGraphicsInitialised)
	{
		// Init Direct X
		if (!InitDirectXAndInput(pChain)) return fnIDXGISwapChainPresent(pChain, SyncInterval, Flags);

		// Init ImGui
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		
		// Setup rendering and IO
		ImGui_ImplWin32_Init(hWindow);
		ImGui_ImplDX11_Init(pDevice, pContext);
		ImGui::GetIO().ImeWindowHandle = hWindow;

		// Create render target view for rendering
		ID3D11Texture2D* pBackBuffer;
		pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
		HRESULT hr = pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRenderTargetView);
		if (FAILED(hr))
		{ 
			std::cerr << "Failed to create render target view" << std::endl; 
			return fnIDXGISwapChainPresent(pChain, SyncInterval, Flags);
		}
		pBackBuffer->Release();

		bGraphicsInitialised = true;
	}
	
	// Render ImGui
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();
	bool bShow = true;
	ImGui::ShowDemoWindow(&bShow);
	ImGui::EndFrame();

	ImGui::Render();

	pContext->OMSetRenderTargets(1, &pRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return fnIDXGISwapChainPresent(pChain, SyncInterval, Flags);
}

bool InitDirectXAndInput(IDXGISwapChain* pChain)
{
	// Get swapchain
	pSwapchain = pChain;

	// Get device and context
	HRESULT hr = pSwapchain->GetDevice(__uuidof(ID3D11Device), (PVOID*)&pDevice);
	if (FAILED(hr))
	{
		std::cerr << "Failed to get device from swapchain" << std::endl;
		return false;
	}
	pDevice->GetImmediateContext(&pContext);

	// Get window from swapchain description
	DXGI_SWAP_CHAIN_DESC swapchainDescription;
	pSwapchain->GetDesc(&swapchainDescription);
	hWindow = swapchainDescription.OutputWindow;

	// Use SetWindowLongPtr to modify window behaviour and get input
	wndProcHandlerOriginal = (WNDPROC)SetWindowLongPtr(hWindow, GWLP_WNDPROC, (LONG_PTR)hWndProc);

	std::cout << "Successfully initialised DirectX - resolution " << swapchainDescription.BufferDesc.Width << "x" << swapchainDescription.BufferDesc.Height << std::endl;
	return true;
}

LRESULT CALLBACK hWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Process input and pass to ImGui
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(wndProcHandlerOriginal, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI SpawnConsoleThread(HMODULE hModule)
{
	// Allocate a new console for the calling process
	AllocConsole();

	// Redirect stdout and stderr
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);

	// Find DirectX 11 DLL
	HANDLE processHandle = GetModuleHandle(L"d3d11");
	if (!processHandle) return false;

	// Get address of fnIDXGISwapChainPresent from VMT by creating dummy device
	// and swapchain and going from there to get the function address
	DXGI_SWAP_CHAIN_DESC sd{ 0 };
	sd.BufferCount = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.OutputWindow = GetDesktopWindow();
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.SampleDesc.Count = 1;

	ID3D11Device* pDummyDevice = nullptr;
	IDXGISwapChain* pDummySwapchain = nullptr;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &pDummySwapchain, &pDummyDevice, nullptr, nullptr);

	// Error checking, save for one small error related to output window
	if (GetLastError() == 0x594) SetLastError(0);
	if (FAILED(hr))
	{
		_com_error error(hr);
		std::cout << "Failed to create dummy device and swapchain - " << error.ErrorMessage() << std::endl;
		return false;
	}

	// Calculate address of IDXGISwapChain::Present from address of swapchain
	void** pVMT = *(void***)pDummySwapchain;
	fnIDXGISwapChainPresent = (IDXGISwapChainPresent)(pVMT[8]);

	// Clean up DirectX
	pDummyDevice->Release();
	pDummySwapchain->Release();

	// Detour IDXGISwapChain::Present
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(LPVOID&)fnIDXGISwapChainPresent, (PBYTE)IDXGISwapChain_Present);
	DetourTransactionCommit();

	while (true)
	{
		
	}

	// Clean up console and thread
	fclose(fp);
	FreeConsole();
	FreeLibraryAndExitThread(hModule, 0);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved)
{
	// Sanity check
	if (!GetModuleHandle(L"SaintsRowTheThird_DX11.exe")) return FALSE;

    switch (ulReasonForCall)
    {
        case DLL_PROCESS_ATTACH:
            CloseHandle(CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)SpawnConsoleThread, hModule, 0, nullptr));
        break;
        case DLL_THREAD_ATTACH:

        break;
        case DLL_THREAD_DETACH:

        break;
        case DLL_PROCESS_DETACH:

        break;
    }

    return TRUE;
}

