
// Standard imports
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <format>
#include <stdexcept>
#include <list>

//Mod loader
#include <PluginAPI.h>


// Detours imports
#include <detours.h>
#pragma comment(lib, "detours.lib")

// DX11 imports
#include "D3D_VMT_Indices.h"
#include <d3d11.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#pragma comment(lib, "D3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")


//ImGUI imports
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <fnt.h>

#define VMT_PRESENT (UINT)IDXGISwapChainVMT::Present


// D3X HOOK DEFINITIONS
typedef HRESULT(__fastcall *IDXGISwapChainPresent)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
BOOL g_bInitialised = false;
// Main D3D11 Objects
ID3D11DeviceContext *pContext = NULL;
ID3D11Device *pDevice = NULL;
ID3D11RenderTargetView *mainRenderTargetView;
static IDXGISwapChain*  pSwapChain = NULL;
static WNDPROC OriginalWndProcHandler = nullptr;
HWND window = nullptr;
IDXGISwapChainPresent fnIDXGISwapChainPresent;

//Constants
constexpr auto DAMAGE_DISPLAY_TIME = 180;
constexpr auto DAMAGE_CHANGE_TIME = 20;
//These are scaled according to game resolution
constexpr auto DAMAGE_NUMBER_FONT_SIZE = 32.0f;
constexpr auto DAMAGE_NUMBER_POS_X = 1344.0f;
constexpr auto DAMAGE_NUMBER_POS_Y = 896.0f;
constexpr auto DAMAGE_NUMBER_VEHICLE_POS_X = 500.0f;
constexpr auto DAMAGE_NUMBER_VEHICLE_POS_Y = 940.0f;

struct Damage{
	float value;
	int time;
};
int gameTime = 0;
uintptr_t baseAddress;
float windowScale;
int winWidth;
int winHeight;
std::list<Damage*> damageNumbers;
ImFont* defaultFont;


extern "C" {
	void __fastcall recordPlayerDamage();
	uintptr_t playerAddress;
	uintptr_t hookRetAddress;
	float damage_tmp = 0;

	BOOL __declspec(dllexport) EML4_Load(PluginInfo* pluginInfo) {
		return false;
	}

	BOOL __declspec(dllexport) EML5_Load(PluginInfo* pluginInfo) {
		pluginInfo->infoVersion = PluginInfo::MaxInfoVer;
		pluginInfo->name = "EDF5 Damage Number Display";
		pluginInfo->version = PLUG_VER(1, 0, 1, 0);
		return true;
	}
}
HRESULT GetDeviceAndCtxFromSwapchain(IDXGISwapChain *pSwapChain, ID3D11Device **ppDevice, ID3D11DeviceContext **ppContext)
{
	HRESULT ret = pSwapChain->GetDevice(__uuidof(ID3D11Device), (PVOID*)ppDevice);

	if (SUCCEEDED(ret))
		(*ppDevice)->GetImmediateContext(ppContext);

	return ret;
}
uintptr_t GetPointerAddress(const uintptr_t base, std::initializer_list<int> offsets) {
	uintptr_t out = base;
	const int* it = offsets.begin();
	for (int i = 0; i < offsets.size(); i++) {
		out = *(uintptr_t*)(out+*(it+i));
		if (out == 0) {
			return 0;
		}
	}
	return out;
}

HRESULT __fastcall Present(IDXGISwapChain *pChain, UINT SyncInterval, UINT Flags)
{
	if (!g_bInitialised) {
		if (FAILED(GetDeviceAndCtxFromSwapchain(pChain, &pDevice, &pContext)))
			return fnIDXGISwapChainPresent(pChain, SyncInterval, Flags);
		pSwapChain = pChain;
		DXGI_SWAP_CHAIN_DESC sd;
		pChain->GetDesc(&sd);
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		defaultFont = io.Fonts->AddFontFromMemoryCompressedTTF(tahomabd_data, tahomabd_size, DAMAGE_NUMBER_FONT_SIZE);


		window = sd.OutputWindow;

		winWidth = *((int*)(baseAddress+0x1256C00));
		winHeight = *((int*)(baseAddress + 0x1256C04));
		windowScale = winWidth / 1920.0f;


		ImGui_ImplWin32_Init(window);
		ImGui_ImplDX11_Init(pDevice, pContext);

		ID3D11Texture2D* pBackBuffer;

		pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
		pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
		pBackBuffer->Release();

		g_bInitialised = true;
	}
	gameTime++;
	playerAddress = GetPointerAddress(baseAddress, { 0x0125AB68,0x238,0x290,0x10 });
	if (damage_tmp != 0) {
		if (damageNumbers.size() > 0) {
			Damage* lastDamage = damageNumbers.back();
			if (gameTime - lastDamage->time > DAMAGE_CHANGE_TIME) {
				Damage* d = new Damage();
				d->value = -damage_tmp;
				d->time = gameTime;
				damageNumbers.push_back(d);
			}
			else {
				lastDamage->value -= damage_tmp;
				lastDamage->time = gameTime;
			}
		}
		else {
			Damage* d = new Damage();
			d->value = -damage_tmp;
			d->time = gameTime;
			damageNumbers.push_back(d);
		}
		damage_tmp = 0;
	}
	if (!damageNumbers.empty()) {
		std::list<Damage*>::iterator it = damageNumbers.begin();
		while (it != damageNumbers.end()) {
			Damage* d = *it;
			it++;
			if (gameTime - d->time > DAMAGE_DISPLAY_TIME) {
				damageNumbers.pop_front();
				delete d;
			}
		}
		
	}

	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX11_NewFrame();

	ImGui::NewFrame();
	bool bShow = true;
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::SetNextWindowPos(ImVec2(-1000, -1000));
	ImGui::SetNextWindowSize(ImVec2(100000, 100000));

	ImGui::Begin("EDF hook", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
	ImGui::PushFont(defaultFont);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	int num = damageNumbers.size();
	if(num > 0 && playerAddress != 0){
		bool isInVehicle = (*(uintptr_t*)(playerAddress + 0x1168)) != 0;

		for (std::list<Damage*>::iterator it = damageNumbers.begin(); it != damageNumbers.end(); it++) {
			Damage* d = *it;
			std::string displayText;
			if (d->value >= 100) {
				displayText = std::format("{:.0f}", d->value);
			}
			else if (d->value >= 10) {
				displayText = std::format("{:.1f}", d->value);
			}
			else {
				displayText = std::format("{:.2f}", d->value);
			}

			float text_width = ImGui::CalcTextSize(displayText.c_str()).x;
			float fontScale = 0;
			if (gameTime - d->time < 20) {
				fontScale = 0.8f * (20 - (gameTime - d->time)) / 20.0f;
			}
			float fontScale2 = (isInVehicle?1.3f:1.0f);
			float fontSize = (1.0f + fontScale) * fontScale2 * DAMAGE_NUMBER_FONT_SIZE * windowScale;
			float posX;
			float posY;
			if (isInVehicle) {
				posX = (DAMAGE_NUMBER_VEHICLE_POS_X - text_width * fontScale2 * (fontScale * 0.1f)) * windowScale;
				posY = (DAMAGE_NUMBER_VEHICLE_POS_Y - num * 1.04f * DAMAGE_NUMBER_FONT_SIZE * fontScale2 * (1.0f + fontScale * 0.9f)) * windowScale + (winHeight - winWidth * 9.0f / 16) / 2;
			}
			else {
				posX = (DAMAGE_NUMBER_POS_X - text_width * fontScale2 * (1.0f + fontScale * 0.9f)) * windowScale;
				posY = (DAMAGE_NUMBER_POS_Y - num * 1.04f * DAMAGE_NUMBER_FONT_SIZE * fontScale2 * (1.0f + fontScale * 0.9f)) * windowScale + (winHeight - winWidth * 9.0f / 16) / 2;
			}
			

			//This is not an ideal way to create font shadow
			dl->AddText(defaultFont, fontSize, ImVec2(posX - 1, posY - 1), ImColor(0, 0, 0, 255), displayText.c_str());
			dl->AddText(defaultFont, fontSize, ImVec2(posX - 1, posY + 1), ImColor(0, 0, 0, 255), displayText.c_str());
			dl->AddText(defaultFont, fontSize, ImVec2(posX + 1, posY - 1), ImColor(0, 0, 0, 255), displayText.c_str());
			dl->AddText(defaultFont, fontSize, ImVec2(posX + 1, posY + 1), ImColor(0, 0, 0, 255), displayText.c_str());


			dl->AddText(defaultFont, fontSize, ImVec2(posX, posY), ImColor(255, 255, 255, 255), displayText.c_str());
			num--;
		}
	}
	ImGui::PopFont();	
	ImGui::End();
	ImGui::EndFrame();
	ImGui::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return fnIDXGISwapChainPresent(pChain, SyncInterval, Flags);
}


void detourDirectX()
{
	Sleep(10000);
	void** pVMT = (void**)GetPointerAddress(baseAddress, { 0x1256c98, 0x0 });
	fnIDXGISwapChainPresent = (IDXGISwapChainPresent)(pVMT[VMT_PRESENT]);
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(LPVOID&)fnIDXGISwapChainPresent, (PBYTE)Present); 
	DetourTransactionCommit();
}

bool get_module_bounds(const std::wstring name, uintptr_t* start, uintptr_t* end)
{
	const auto module = GetModuleHandle(name.c_str());
	if (module == nullptr)
		return false;

	MODULEINFO info;
	GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
	*start = (uintptr_t)(info.lpBaseOfDll);
	*end = *start + info.SizeOfImage;
	return true;
}

// Scan for a byte pattern with a mask in the form of "xxx???xxx".
uintptr_t sigscan(const std::wstring name, const char* sig, const char* mask)
{
	uintptr_t start, end;
	if (!get_module_bounds(name, &start, &end))
		throw std::runtime_error("Module not loaded");

	const auto last_scan = end - strlen(mask) + 1;

	for (auto addr = start; addr < last_scan; addr++) {
		for (size_t i = 0;; i++) {
			if (mask[i] == '\0')
				return addr;
			if (mask[i] != '?' && sig[i] != *(char*)(addr + i))
				break;
		}
	}

	return NULL;
}
void* AllocatePageNearAddress(void* targetAddr)
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	const uint64_t PAGE_SIZE = sysInfo.dwPageSize;

	uint64_t startAddr = (uint64_t(targetAddr) & ~(PAGE_SIZE - 1)); //round down to nearest page boundary
	uint64_t minAddr = min(startAddr - 0x7FFFFF00, (uint64_t)sysInfo.lpMinimumApplicationAddress);
	uint64_t maxAddr = max(startAddr + 0x7FFFFF00, (uint64_t)sysInfo.lpMaximumApplicationAddress);

	uint64_t startPage = (startAddr - (startAddr % PAGE_SIZE));

	uint64_t pageOffset = 1;
	while (1)
	{
		uint64_t byteOffset = pageOffset * PAGE_SIZE;
		uint64_t highAddr = startPage + byteOffset;
		uint64_t lowAddr = (startPage > byteOffset) ? startPage - byteOffset : 0;

		bool needsExit = highAddr > maxAddr && lowAddr < minAddr;

		if (highAddr < maxAddr)
		{
			void* outAddr = VirtualAlloc((void*)highAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr)
				return outAddr;
		}

		if (lowAddr > minAddr)
		{
			void* outAddr = VirtualAlloc((void*)lowAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr != nullptr)
				return outAddr;
		}

		pageOffset++;

		if (needsExit)
		{
			break;
		}
	}

	return nullptr;
}

void hookDamagefunction() {
	void* originalFunctionAddr = (void*)(sigscan(
		L"EDF5.exe",
		"\xF3\x0F\x58\x87\xFC\x01\x00\x00",
		"xxxxxxxx"));
	hookRetAddress = (uint64_t)originalFunctionAddr + 0x8;

	void* memoryBlock = AllocatePageNearAddress(originalFunctionAddr);


	uint8_t hookFunction[] =
	{
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //mov rax, addr
		0xFF, 0xE0 //jmp rax
	};
	uint64_t addrToJumpTo64 = (uint64_t)recordPlayerDamage;

	memcpy(&hookFunction[2], &addrToJumpTo64, sizeof(addrToJumpTo64));
	memcpy(memoryBlock, hookFunction, sizeof(hookFunction));


	DWORD oldProtect;
	VirtualProtect(originalFunctionAddr, 1024, PAGE_EXECUTE_READWRITE, &oldProtect);
	uint8_t jmpInstruction[5] = { 0xE9, 0x0, 0x0, 0x0, 0x0 };


	const uint64_t relAddr = (uint64_t)memoryBlock - ((uint64_t)originalFunctionAddr + sizeof(jmpInstruction));
	memcpy(jmpInstruction + 1, &relAddr, 4);

	memcpy(originalFunctionAddr, jmpInstruction, sizeof(jmpInstruction));
}

int WINAPI main()
{
	baseAddress = (uintptr_t)GetModuleHandle(L"EDF5.exe");
	detourDirectX();
	hookDamagefunction();
}


BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)main, NULL, NULL, NULL);
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

