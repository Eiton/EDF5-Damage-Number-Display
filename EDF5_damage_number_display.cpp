
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
#include <map>

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

//mINI
#include <ini.h>



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


int FIXED_POSITION_DISPLAY = 1;
int FIXED_POSITION_DISPLAY_TIME = 180;
int FIXED_POSITION_CHANGE_TIME = 20;
float FIXED_POSITION_FONT_SIZE = 32.0f;
float FIXED_POSITION_POS_X = 1344.0f;
float FIXED_POSITION_POS_Y = 896.0f;
float FIXED_POSITION_VEHICLE_FONT_SIZE = 48.0f;
float FIXED_POSITION_VEHICLE_POS_X = 500.0f;
float FIXED_POSITION_VEHICLE_POS_Y = 940.0f;

int WORLD_POSITION_DISPLAY = 0;
int WORLD_POSITION_DISPLAY_MODE = 0;
int WORLD_POSITION_DISPLAY_TIME = 0;
float WORLD_POSITION_FONT_SIZE = 32.0f;
struct vec2
{
	float x;
	float y;
};

struct vec3
{
	float x;
	float y;
	float z;
};

struct vec4
{
	float x;
	float y;
	float z;
	float w;
};

struct Damage {
	vec3 pos;
	float value;
	int time;
	int type;
	float life;
};

int gameTime = 0;
uintptr_t baseAddress;
float windowScale;
int winWidth;
int winHeight;
std::list<Damage*> damageNumbersFixed;
std::list<Damage*> damageNumbersByHit;
std::map<uintptr_t,Damage*> damageNumbersByTarget;
ImFont* defaultFont;

float* viewMatrixT;
float* projectionMatrixT;
float viewProjectionMatrix[16];

//void __fastcall AddDamage(float damage, uintptr_t ptr, uintptr_t target);

uintptr_t GetPointerAddress(const uintptr_t base, std::initializer_list<int> offsets) {
	uintptr_t out = base;
	const int* it = offsets.begin();
	for (int i = 0; i < offsets.size(); i++) {
		out = *(uintptr_t*)(out + *(it + i));
		if (out == 0) {
			return 0;
		}
	}
	return out;
}
extern "C" {
	void __fastcall recordPlayerDamage();
	void __fastcall add_damage(float dmg, uintptr_t ptr, uintptr_t target) {
		if (FIXED_POSITION_DISPLAY != 0) {
			if (damageNumbersFixed.size() > 0) {
				Damage* lastDamage = damageNumbersFixed.back();
				if (gameTime - lastDamage->time > FIXED_POSITION_CHANGE_TIME) {
					Damage* d = new Damage();
					d->value = -dmg;
					d->time = gameTime;
					damageNumbersFixed.push_back(d);
				}
				else {
					lastDamage->value -= dmg;
					lastDamage->time = gameTime;
				}
			}
			else {
				Damage* d = new Damage();
				d->value = -dmg;
				d->time = gameTime;
				damageNumbersFixed.push_back(d);
			}
		}
		if (WORLD_POSITION_DISPLAY != 0 && WORLD_POSITION_DISPLAY_MODE == 1) {
			//Use the bullet position
			Damage* d = new Damage();
			d->value = -dmg;
			d->time = gameTime;
			d->pos.x = *(float*)(ptr + 0x30);
			d->pos.y = *(float*)(ptr + 0x34);
			d->pos.z = *(float*)(ptr + 0x38);
			d->type = *(BYTE*)(target + 0x218);
			damageNumbersByHit.push_back(d);
		}
		else if (WORLD_POSITION_DISPLAY != 0 && WORLD_POSITION_DISPLAY_MODE >= 2){
			// Try to get the lock on position. 
			uintptr_t pos = GetPointerAddress(target, { 0x268,0x8 });
			// Some objects do not have lock on position, so use the object position instead
			if (pos == 0) {
				pos = target + 0x94;
			}
			if (damageNumbersByTarget.contains(target)) {
				Damage* d = damageNumbersByTarget.find(target)->second;
				d->value -= dmg;
				d->time = gameTime;
				d->pos.x = *(float*)(pos + 0x10);
				d->pos.y = *(float*)(pos + 0x14);
				d->pos.z = *(float*)(pos + 0x18);
				d->life = *(float*)(target + 0x1fc) + dmg;
				if (d->life < 0) {
					d->life = 0;
				}
			}
			else {
				Damage* d = new Damage();
				d->value = -dmg;
				d->time = gameTime;
				d->pos.x = *(float*)(pos + 0x10);
				d->pos.y = *(float*)(pos + 0x14);
				d->pos.z = *(float*)(pos + 0x18);
				d->type = *(BYTE*)(target + 0x218);
				d->life = *(float*)(target + 0x1fc) + dmg;
				if (d->life < 0) {
					d->life = 0;
				}
				damageNumbersByTarget.insert(std::pair{ target,d });
			}
		}
	}
	uintptr_t playerAddress;
	uintptr_t hookRetAddress;

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
void GetViewProjectionMatrix() {
	if (viewMatrixT != 0 && projectionMatrixT != 0) {
		viewProjectionMatrix[0] = viewMatrixT[0] * projectionMatrixT[0];
		viewProjectionMatrix[1] = viewMatrixT[1] * projectionMatrixT[0];
		viewProjectionMatrix[2] = viewMatrixT[2] * projectionMatrixT[0];
		viewProjectionMatrix[3] = viewMatrixT[3] * projectionMatrixT[0];
		viewProjectionMatrix[4] = viewMatrixT[4] * projectionMatrixT[5];
		viewProjectionMatrix[5] = viewMatrixT[5] * projectionMatrixT[5];
		viewProjectionMatrix[6] = viewMatrixT[6] * projectionMatrixT[5];
		viewProjectionMatrix[7] = viewMatrixT[7] * projectionMatrixT[5];
		viewProjectionMatrix[8] = viewMatrixT[8] * projectionMatrixT[10];
		viewProjectionMatrix[9] = viewMatrixT[9] * projectionMatrixT[10];
		viewProjectionMatrix[10] = viewMatrixT[10] * projectionMatrixT[10];
		viewProjectionMatrix[11] = viewMatrixT[11] * projectionMatrixT[10] + projectionMatrixT[11];
		viewProjectionMatrix[12] = viewMatrixT[8] * projectionMatrixT[14];
		viewProjectionMatrix[13] = viewMatrixT[9] * projectionMatrixT[14];
		viewProjectionMatrix[14] = viewMatrixT[10] * projectionMatrixT[14];
		viewProjectionMatrix[15] = viewMatrixT[11] * projectionMatrixT[14];
	}
}
bool WorldToScreen(const vec3 pos, vec2& screen, const int windowWidth, const int windowHeight)
{
	vec4 clipCoords;
	clipCoords.x = pos.x * viewProjectionMatrix[0] + pos.y * viewProjectionMatrix[1] + pos.z * viewProjectionMatrix[2] + viewProjectionMatrix[3];
	clipCoords.y = pos.x * viewProjectionMatrix[4] + pos.y * viewProjectionMatrix[5] + pos.z * viewProjectionMatrix[6] + viewProjectionMatrix[7];
	clipCoords.z = pos.x * viewProjectionMatrix[8] + pos.y * viewProjectionMatrix[9] + pos.z * viewProjectionMatrix[10] + viewProjectionMatrix[11];
	clipCoords.w = pos.x * viewProjectionMatrix[12] + pos.y * viewProjectionMatrix[13] + pos.z * viewProjectionMatrix[14] + viewProjectionMatrix[15];

	if (clipCoords.w < 0.1f)
		return false;

	vec3 NDC;
	NDC.x = clipCoords.x / clipCoords.w;
	NDC.y = clipCoords.y / clipCoords.w;
	NDC.z = clipCoords.z / clipCoords.w;

	//screen.x = (windowWidth / 2 * NDC.x) + (NDC.x + windowWidth / 2);
	//screen.y = -(windowHeight / 2 * NDC.y) + (NDC.y + windowHeight / 2);
	screen.x = -(windowWidth / 2 * NDC.x) + (windowWidth / 2);
	screen.y = -(windowHeight / 2 * NDC.y) + (windowHeight / 2);
	return true;
}
std::string FormatDamageNumber(const float dmg) {
	if (dmg >= 100) {
		return std::format("{:.0f}", dmg);
	}
	else if (dmg >= 10) {
		return std::format("{:.1f}", dmg);
	}
	else {
		return std::format("{:.2f}", dmg);
	}
}
void DrawDamageNumber(ImDrawList* dl, const vec2 pos, const float fontSize, const char* str, const vec3 color) {
	//This is not an ideal way to create font shadow
	dl->AddText(defaultFont, fontSize, ImVec2(pos.x - 1, pos.y - 1), ImColor(0, 0, 0, 255), str);
	dl->AddText(defaultFont, fontSize, ImVec2(pos.x - 1, pos.y + 1), ImColor(0, 0, 0, 255), str);
	dl->AddText(defaultFont, fontSize, ImVec2(pos.x + 1, pos.y - 1), ImColor(0, 0, 0, 255), str);
	dl->AddText(defaultFont, fontSize, ImVec2(pos.x + 1, pos.y + 1), ImColor(0, 0, 0, 255), str);

	dl->AddText(defaultFont, fontSize, ImVec2(pos.x, pos.y), ImColor(int(color.x), int(color.y), int(color.z), 255), str);
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
		defaultFont = io.Fonts->AddFontFromMemoryCompressedTTF(tahomabd_data, tahomabd_size, 100.0f);


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

	if (!damageNumbersFixed.empty()) {
		std::list<Damage*>::iterator it = damageNumbersFixed.begin();
		while (it != damageNumbersFixed.end()) {
			Damage* d = *it;
			it++;
			if (gameTime - d->time > FIXED_POSITION_DISPLAY_TIME) {
				damageNumbersFixed.pop_front();
				delete d;
			}
		}
	}
	if (!damageNumbersByHit.empty()) {
		std::list<Damage*>::iterator it = damageNumbersByHit.begin();
		while (it != damageNumbersByHit.end()) {
			Damage* d = *it;
			it++;
			if (gameTime - d->time > WORLD_POSITION_DISPLAY_TIME) {
				damageNumbersByHit.pop_front();
				delete d;
			}
		}
	}
	if (!damageNumbersByTarget.empty()) {
		for (std::map<uintptr_t, Damage*>::iterator it = damageNumbersByTarget.begin(); it != damageNumbersByTarget.end();) {
			uintptr_t target = it->first;
			Damage* d = it->second;
			if (gameTime - d->time > WORLD_POSITION_DISPLAY_TIME) {
				damageNumbersByTarget.erase(it++);
				delete d;
			}
			else {
				++it;
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

	vec2 pos;
	pos.x = 500;
	pos.y = 500;
	if (playerAddress != 0) {
		if (FIXED_POSITION_DISPLAY != 0 && damageNumbersFixed.size() > 0) {
			bool isInVehicle = (*(uintptr_t*)(playerAddress + 0x1168)) != 0;
			int num = damageNumbersFixed.size();
			for (std::list<Damage*>::iterator it = damageNumbersFixed.begin(); it != damageNumbersFixed.end(); it++) {
				Damage* d = *it;
				std::string displayText = FormatDamageNumber(d->value);
				float text_width = ImGui::CalcTextSize(displayText.c_str()).x*0.01f;
				float fontSize;
				vec2 pos;
				float fontScale = 0;
				if (gameTime - d->time < 20) {
					fontScale = 0.8f * (20 - (gameTime - d->time)) / 20.0f;
				}
				if (isInVehicle) {
					fontSize = (1.0f + fontScale) * FIXED_POSITION_VEHICLE_FONT_SIZE * windowScale;
					pos.x = (FIXED_POSITION_VEHICLE_POS_X - text_width * FIXED_POSITION_VEHICLE_FONT_SIZE * (fontScale * 0.1f)) * windowScale;
					pos.y = (FIXED_POSITION_VEHICLE_POS_Y - num * 1.04f * FIXED_POSITION_VEHICLE_FONT_SIZE * (1.0f + fontScale * 0.9f)) * windowScale + (winHeight - winWidth * 9.0f / 16) / 2;
				}
				else {
					fontSize = (1.0f + fontScale) * FIXED_POSITION_FONT_SIZE * windowScale;
					pos.x = (FIXED_POSITION_POS_X - text_width * FIXED_POSITION_FONT_SIZE * (1.0f + fontScale * 0.9f)) * windowScale;
					pos.y = (FIXED_POSITION_POS_Y - num * 1.04f * FIXED_POSITION_FONT_SIZE * (1.0f + fontScale * 0.9f)) * windowScale + (winHeight - winWidth * 9.0f / 16) / 2;
				}
				DrawDamageNumber(dl, pos, fontSize, displayText.c_str(),vec3(255.0,255.0,255.0));

				num--;
			}
		}
		if (WORLD_POSITION_DISPLAY != 0 && damageNumbersByTarget.size() + damageNumbersByHit.size() > 0) {
			viewMatrixT = (float*)(GetPointerAddress(baseAddress, { 0x0125B080,0x8,0x8,0x8 }) + 0x80);
			projectionMatrixT = (float*)(GetPointerAddress(baseAddress, { 0x0125B080,0x8,0x8,0x8 }) + 0xE0);
			GetViewProjectionMatrix();
			for (std::list<Damage*>::iterator it = damageNumbersByHit.begin(); it != damageNumbersByHit.end(); it++) {
				Damage* d = *it;
				std::string displayText = FormatDamageNumber(d->value);

				float text_width = ImGui::CalcTextSize(displayText.c_str()).x * 0.01f;
				float fontSize = WORLD_POSITION_FONT_SIZE * windowScale;
				vec2 pos;
				if (!WorldToScreen(d->pos, pos, winWidth, winHeight)) {
					continue;
				}
				pos.x -= text_width * WORLD_POSITION_FONT_SIZE / 4;
				pos.y -= fontSize / 2;
				DrawDamageNumber(dl, pos, fontSize, displayText.c_str(), d->type!=1? vec3(255.0, 0, 0): vec3(255.0, 255.0, 255.0));
			}
			for (std::map<uintptr_t, Damage*>::iterator it = damageNumbersByTarget.begin(); it != damageNumbersByTarget.end(); it++) {
				Damage* d = it->second;
				std::string displayText = FormatDamageNumber(WORLD_POSITION_DISPLAY_MODE==2?d->value:d->life);

				float text_width = ImGui::CalcTextSize(displayText.c_str()).x*0.01f;
				float fontSize = WORLD_POSITION_FONT_SIZE * windowScale;
				vec2 pos;
				if (!WorldToScreen(d->pos, pos, winWidth, winHeight)) {
					continue;
				}
				pos.x -= text_width * WORLD_POSITION_FONT_SIZE / 2 * windowScale;
				pos.y -= fontSize * 2.0f;
				DrawDamageNumber(dl, pos, fontSize, displayText.c_str(), d->type != 1 ? vec3(255.0, 0, 0) : vec3(255.0, 255.0, 255.0));
			}
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
	void** pVMT = (void**)GetPointerAddress(baseAddress, { 0x1256c98, 0x0 });
	while (pVMT == 0) {
		Sleep(1000);
		pVMT = (void**)GetPointerAddress(baseAddress, { 0x1256c98, 0x0 });
	}
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
	mINI::INIFile file("EDF5-Damage-Number-Display.ini");
	mINI::INIStructure ini;
	if (file.read(ini)) {
		if (ini.has("FIXED_POSITION_DISPLAY")) {
			if (ini["FIXED_POSITION_DISPLAY"].has("Enabled")) {
				FIXED_POSITION_DISPLAY = ini["FIXED_POSITION_DISPLAY"]["Enabled"] == "1";
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Display_Time")) {
				FIXED_POSITION_DISPLAY_TIME = stoi(ini["FIXED_POSITION_DISPLAY"]["Display_Time"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Change_Time")) {
				FIXED_POSITION_CHANGE_TIME = stoi(ini["FIXED_POSITION_DISPLAY"]["Change_Time"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Font_Size")) {
				FIXED_POSITION_FONT_SIZE = stof(ini["FIXED_POSITION_DISPLAY"]["Font_Size"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Pos_X")) {
				FIXED_POSITION_POS_X = stof(ini["FIXED_POSITION_DISPLAY"]["Pos_X"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Pos_Y")) {
				FIXED_POSITION_POS_Y = stof(ini["FIXED_POSITION_DISPLAY"]["Pos_Y"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Vehicle_Font_Size")) {
				FIXED_POSITION_VEHICLE_FONT_SIZE = stof(ini["FIXED_POSITION_DISPLAY"]["Vehicle_Font_Size"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Vehicle_Pos_X")) {
				FIXED_POSITION_VEHICLE_POS_X = stof(ini["FIXED_POSITION_DISPLAY"]["Vehicle_Pos_X"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Vehicle_Pos_Y")) {
				FIXED_POSITION_VEHICLE_POS_Y = stof(ini["FIXED_POSITION_DISPLAY"]["Vehicle_Pos_Y"]);
			}
		}
		if (ini.has("WORLD_POSITION_DISPLAY")) {
			if (ini["WORLD_POSITION_DISPLAY"].has("Enabled")) {
				WORLD_POSITION_DISPLAY = ini["WORLD_POSITION_DISPLAY"]["Enabled"] == "1";
			}
			if (ini["WORLD_POSITION_DISPLAY"].has("Mode")) {
				WORLD_POSITION_DISPLAY_MODE = stoi(ini["WORLD_POSITION_DISPLAY"]["Mode"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Display_Time")) {
				WORLD_POSITION_DISPLAY_TIME = stoi(ini["WORLD_POSITION_DISPLAY"]["Display_Time"]);
			}
			if (ini["FIXED_POSITION_DISPLAY"].has("Font_Size")) {
				WORLD_POSITION_FONT_SIZE = stof(ini["WORLD_POSITION_DISPLAY"]["Font_Size"]);
			}

		}
	}
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

