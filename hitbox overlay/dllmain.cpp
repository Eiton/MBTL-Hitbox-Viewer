﻿#include "pch.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <Windows.h>
#include <psapi.h>
#include "detours.h"
#include <string>
#include <stdexcept>
#include "mINI.h"

#pragma comment(lib,"d3d9.lib")
#pragma comment(lib,"d3dx9.lib")
#pragma comment(lib, "detours.lib")

// box color
D3DCOLOR hurtbox_color1 = D3DCOLOR_ARGB(96, 30, 30, 255);
D3DCOLOR hurtbox_color2 = D3DCOLOR_ARGB(255, 0, 0, 255);
D3DCOLOR hurtbox_armored_color1 = D3DCOLOR_ARGB(96, 255, 255, 255);
D3DCOLOR hurtbox_armored_color2 = D3DCOLOR_ARGB(255, 255, 255, 255);
D3DCOLOR pushbox_color1 = D3DCOLOR_ARGB(96, 255, 255, 0);
D3DCOLOR pushbox_color2 = D3DCOLOR_ARGB(255, 255, 255, 0);
D3DCOLOR clashbox_color1 = D3DCOLOR_ARGB(96, 30, 255, 30);
D3DCOLOR clashbox_color2 = D3DCOLOR_ARGB(255, 0, 255, 0);
D3DCOLOR hitbox_color1 = D3DCOLOR_ARGB(96, 255, 30, 30);
D3DCOLOR hitbox_color2 = D3DCOLOR_ARGB(255, 255, 0, 0);


// handle, address and offset
HANDLE phandle;
DWORD base_address;
DWORD p1_address;

int* pause;
DWORD pause_address2;

DWORD objList_address;
int* objCount;

int* resolutionX;
int* resolutionY;

signed int* cameraPosX;
signed int* cameraPosY;
float* cameraZoom;


// asm codes to patch the exe 

//pause
const BYTE je[6] = { 0x0F,0x84,0xC3,0x06,0x00,0x00 };
const BYTE jmp[6] = { 0xE9,0xC4,0x06,0x00,0x00,0x90 };

int pal_num = 0x1e;




// variables for d3d hook
typedef HRESULT(_stdcall* f_Present)(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
f_Present oPresent;
void** vtable;


// other global variables
int keyPressed = 0;
bool toggleHitbox = true;
bool unlockColorSlots = false;
bool frameStep = false;
LPD3DXFONT m_font;

void DrawRectangle(IDirect3DDevice9* pDevice, const float x1, const float x2, const float y1, const float y2, const float z, D3DCOLOR innerColor, D3DCOLOR outerColor) {
	struct vertex
	{
		float x, y, z, rhw;
		DWORD color;
	};
	DWORD alphaBlendEnable;
	DWORD destBlend;
	DWORD srcBlend;
	DWORD destBlendAlpha;
	DWORD srcBlendAlpha;
	DWORD pFVF;
	IDirect3DPixelShader9* pixelShader;
	IDirect3DBaseTexture9* texture;
	pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlendEnable);
	pDevice->GetRenderState(D3DRS_DESTBLEND, &destBlend);
	pDevice->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
	pDevice->GetRenderState(D3DRS_DESTBLENDALPHA, &destBlendAlpha);
	pDevice->GetRenderState(D3DRS_SRCBLENDALPHA, &srcBlendAlpha);
	pDevice->GetPixelShader(&pixelShader);
	pDevice->GetFVF(&pFVF);
	pDevice->GetTexture(0, &texture);

	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
	pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	pDevice->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
	pDevice->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
	pDevice->SetPixelShader(nullptr);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	pDevice->SetTexture(0, nullptr);

	vertex vertices[] =
	{
		{ x1, y1, z, 1.F, innerColor },
		{ x1, y2, z, 1.F, innerColor },
		{ x2, y1, z, 1.F, innerColor },
		{ x2, y2, z, 1.F, innerColor },
	};
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(vertex));

	vertex outline[] =
	{
		{ x1, y1, z, 1.F, outerColor },
		{ x1, y2, z, 1.F, outerColor },
		{ x2, y2, z, 1.F, outerColor },
		{ x2, y1, z, 1.F, outerColor },
		{ x1, y1, z, 1.F, outerColor },
	};
	pDevice->DrawPrimitiveUP(D3DPT_LINESTRIP, 4, outline, sizeof(vertex));

	
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, alphaBlendEnable);
	pDevice->SetRenderState(D3DRS_DESTBLEND, destBlend);
	pDevice->SetRenderState(D3DRS_SRCBLEND, srcBlend);
	pDevice->SetRenderState(D3DRS_DESTBLENDALPHA, destBlendAlpha);
	pDevice->SetRenderState(D3DRS_SRCBLENDALPHA, srcBlendAlpha);
	pDevice->SetPixelShader(pixelShader);
	pDevice->SetFVF(pFVF);
	pDevice->SetTexture(0, texture);
	if (texture != nullptr)
		texture->Release();
}
void drawBox(IDirect3DDevice9* pDevice, int start, int end, DWORD boxAddr, float rx, float ry, BYTE facing, D3DCOLOR innerColor, D3DCOLOR outerColor) {
	for (int i = start; i < end; i++) {
		DWORD clsnAddress = *(DWORD*)(boxAddr + i * 4);
		if (clsnAddress != 0) {
			signed short* x1, * x2, * y1, * y2;
			x1 = (signed short*)clsnAddress;
			y1 = (signed short*)(clsnAddress + 0x2);
			x2 = (signed short*)(clsnAddress + 0x4);
			y2 = (signed short*)(clsnAddress + 0x6);
			float fx1, fx2, fy1, fy2;
			fx1 = ((*x1 * (facing == 0 ? 1.0f : -1.0f) + rx) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
			fx2 = ((*x2 * (facing == 0 ? 1.0f : -1.0f) + rx) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
			fy1 = ((*y1 + ry) * (*cameraZoom) + 640.0f) * (*resolutionY) / 720.0f;
			fy2 = ((*y2 + ry) * (*cameraZoom) + 640.0f) * (*resolutionY) / 720.0f;
			DrawRectangle(pDevice, fx1, fx2, fy1, fy2, 0, innerColor, outerColor);
		}

	}
}
void drawFrameData(IDirect3DDevice9* pDevice, DWORD objData, float rx, float ry) {
	bool isHurt = *(DWORD*)(objData + 0x2d8);
	int frameNum = 0;
	int totalFrames = 0;
	if (isHurt) {
		totalFrames = *(DWORD*)(objData + 0x2e0);
		if (totalFrames == -1) {
			totalFrames = *(BYTE*)(objData + 0x2cc);
			frameNum = *(BYTE*)(objData + 0x2c2) + 1;
		}
		else {
			frameNum = 1 + totalFrames - *(DWORD*)(objData + 0x2dc);
		}
	}
	else {
		DWORD state = *(DWORD*)((*(DWORD*)(objData + 0x6f8)) + 0x30);
		DWORD elem = *(DWORD*)(objData + 0x20);
		DWORD elemTime = *(DWORD*)(objData + 0x30);
		int i = 0;
		bool end = false;
		while (end == false && (*(DWORD*)(state + i * 0x4) != 0)) {
			if (i == elem) {
				frameNum = totalFrames + elemTime;
			}
			DWORD fa = (*(DWORD*)(state + i * 0x4) + 0x96);
			totalFrames += *(BYTE*)fa;
			DWORD ea = (*(DWORD*)(state + i * 0x4) + 0x98);
			end = *(bool*)ea != 1;
			i++;
			if (end && i <= elem) {
				end = false;
				state += i * 0x4;
				elem -= i;
				totalFrames = 1;
				i = 0;
			}
		}
	}
	BYTE dInvTime = *(BYTE*)(objData + 0x2b8);
	BYTE tInvTime = *(BYTE*)(objData + 0x2b9);
	//84 c1 b8 01 00 00
	BYTE invFlag = *(BYTE*)(objData + 0x554);
	BYTE invFlagTime = *(BYTE*)(objData + 0x560);

	BYTE invFlag2 = *(BYTE*)(*(DWORD*)(*(DWORD*)(objData + 0x6fc) + 0xAC) + 0xD);

	BYTE invFlag3 = *(BYTE*)(objData + 0x2a5);
	BYTE invFlag3_2 = *(BYTE*)(objData + 0x2a7);

	if (m_font == NULL) {
		D3DXCreateFont(pDevice, 17, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Arial"), &m_font);
	}
	D3DCOLOR fontColor = D3DCOLOR_ARGB(255, 255, 255, 255);
	RECT rct;
	rct.left = ((rx - 400.0f) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
	rct.right = ((rx + 400.0f) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
	rct.top = ((10.0f + ry) * (*cameraZoom) + 640.0f) * (*resolutionY) / 720.0f;
	rct.bottom = ((60.0f + ry) * (*cameraZoom) + 640.0f) * (*resolutionY) / 720.0f;
	int k = 1;

	std::string text = std::to_string(frameNum) + "/" + std::to_string(totalFrames) + '\n';
	if (dInvTime || invFlag2 == 3 || invFlag2 == 5 || (invFlag3 >= 3 && invFlag3_2 == 0)) {
		text += "S";//invincible to strikes
	}
	else if (invFlag != 0) {
		if ((invFlag & 1) > 0) {
			text += "A";//invincible to air attacks
		}
		if ((invFlag & 2) > 0) {
			text += "G";//invincible to ground attacks
		}
		if ((invFlag & 8) > 0) {
			text += "P";//invincible to projectiles
		}
	}
	if (tInvTime || invFlag2 == 4 || invFlag2 == 5 || (invFlag3 >= 3 && invFlag3_2 == 0)) {
		text += "T";//invincible to throws
	}
	m_font->DrawTextA(NULL, text.c_str(), -1, &rct, DT_CENTER, fontColor);
	
}
void drawObj(IDirect3DDevice9* pDevice, DWORD objData, int drawBlue, DWORD state, bool drawFrame) {
	signed int* posX, * posX2, * posY, * posY2;
	BYTE* facing;
	posX = (signed int*)(objData + 0x64);
	posY = (signed int*)(objData + 0x68);
	posX2 = (signed int*)(objData + 0x70);
	posY2 = (signed int*)(objData + 0x74);
	facing = (BYTE*)(objData + 0x6ec);
	BYTE* numBox1;
	BYTE* numBox2;
	numBox1 = (BYTE*)(state + 0xb7);
	numBox2 = (BYTE*)(state + 0xb6);
	float rx, ry;
	rx = (*posX + *posX2 - (*cameraPosX)) / 128.0f;
	ry = (*posY + *posY2 - (*cameraPosY)) / 128.0f;
	if (*numBox2 > 0) {
		DWORD* boxAddress = (DWORD*)(state + 0xc0);
		if (drawBlue) {
			if (drawBlue == 2) {
				drawBox(pDevice, 1, (*numBox2 > 8 ? 8 : *numBox2), *boxAddress, rx, ry, *facing, hurtbox_armored_color1, hurtbox_armored_color2);
			}
			else {
				drawBox(pDevice, 1, (*numBox2 > 8 ? 8 : *numBox2), *boxAddress, rx, ry, *facing, hurtbox_color1, hurtbox_color2);
			}
			drawBox(pDevice, 0, 1, *boxAddress, rx, ry, *facing, pushbox_color1, pushbox_color2);
			if (*numBox2 >= 0xC) {
				drawBox(pDevice, 0, 1, *boxAddress + 0xB * 4, rx, ry, *facing, clashbox_color1, clashbox_color2);
			}
		}
	}
	if (*numBox1 > 0) {
		DWORD* boxAddress = (DWORD*)(state + 0xc4);
		drawBox(pDevice, 0, *numBox1, *boxAddress, rx, ry, *facing, hitbox_color1, hitbox_color2);
	}
	if (drawFrame) {
		drawFrameData(pDevice, objData, rx, ry);
	}
}

HRESULT _stdcall Hooked_Present(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {

	if (toggleHitbox) {
		pDevice->BeginScene();
		DWORD state;
		DWORD obj_addrress = p1_address;
		state = *(DWORD*)(obj_addrress + 0x6fc);
		for (int i = 0; i < 4; i++) {
			if (state != 0) {
				DWORD c;
				int drawBlue = 1;
				int armor = 0;
				c = *(DWORD*)(obj_addrress + 0x5e0);
				if (c > 0) {
					c = *(DWORD*)(obj_addrress + 0x5d4);
					drawBlue = c != 0;
				}
				else {
					c = *(DWORD*)(obj_addrress + 0x2a4);
					drawBlue = c != 1;
				}
				if (drawBlue == 1) {
					c = *(DWORD*)(obj_addrress + 0x624);
					if (c > 0) {
						c = *(DWORD*)(obj_addrress + 0x618);
						if (c != 0) {
							c = *(DWORD*)(obj_addrress + 0x704);
							if (c != 0) {
								c = *(DWORD*)(obj_addrress + 0x9ac);
								armor = !c;
							}
						}
					}
				}


				drawObj(pDevice, obj_addrress, drawBlue + armor, state, true);
			}
			obj_addrress = obj_addrress + 0xc44;
			state = *(DWORD*)(obj_addrress + 0x6fc);
		}
		if (*objCount > 0) {
			for (int i = 0; i < *objCount; i++) {
				obj_addrress = *(DWORD*)(objList_address + i * 4);
				if (obj_addrress != 0) {
					state = *(DWORD*)(obj_addrress + 0x6fc);
					if (state != 0) {
						DWORD c;
						int drawBlue = 1;
						c = *(DWORD*)(obj_addrress + 0x5ec);
						if (c > 0) {
							c = *(DWORD*)(obj_addrress + 0x5e0);
							drawBlue = c != 0;
						}
						else {
							c = *(DWORD*)(obj_addrress + 0x2a2);
							drawBlue = c != 1;
						}
						c = *(DWORD*)(obj_addrress + 0x78);
						if (c != 0) {
							drawBlue = 0;
						}
						c = *(DWORD*)(obj_addrress + 0x84);
						if ((c & 0x100) != 0) {
							drawBlue = 0;
						}
						drawObj(pDevice, obj_addrress, drawBlue, state, false);
					}
				}
			}
		}
		pDevice->EndScene();
	}

	if (!GetAsyncKeyState(VK_F5) &&
		!GetAsyncKeyState(VK_F6) &&
		!GetAsyncKeyState(VK_F7)) {
		keyPressed = 0;
	}
	else {
		keyPressed++;
	}
	if (frameStep == true) {
		if (*pause == 0) {
			*pause = 1;
			WriteProcessMemory(phandle, (LPVOID)pause_address2, !*pause ? &je : &jmp, 6, 0);
			frameStep = 0;
		}
		else {
			*pause = 0;
			WriteProcessMemory(phandle, (LPVOID)pause_address2, !*pause ? &je : &jmp, 6, 0);
		}

	}
	else {

		if (GetAsyncKeyState(VK_F5) && keyPressed == 1) {
			toggleHitbox = !toggleHitbox;
		}
		else
			if (GetAsyncKeyState(VK_F6) && keyPressed == 1) {
				*pause = !*pause;
				WriteProcessMemory(phandle, (LPVOID)pause_address2, !*pause ? &je : &jmp, 6, 0);
			}
			else
				if (GetAsyncKeyState(VK_F7) && (keyPressed == 1 || (keyPressed > 40 && keyPressed % 10 == 0))) {
					frameStep = true;
				}
	}


	return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}


//Hook functions copied from AltimorTASDK
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
DWORD WINAPI MainThread(LPVOID hModule)
{
	phandle = GetCurrentProcess();
	if (!phandle) {
		exit(0);
	}
	base_address = (DWORD)GetModuleHandle(L"MBTL.exe");
	if (!base_address) {
		exit(0);
	}
	
	p1_address = *(DWORD*)(sigscan(
		L"MBTL.exe",
		"\x7D\x27\x69\xc8",
		"xxxx") + 0xA);

	pause = (int*)(*(DWORD*)(sigscan(
		L"MBTL.exe",
		"\x33\xF6\x3B\x0D",
		"xxxx") + 0x4));
	pause_address2 = sigscan(
		L"MBTL.exe",
		"\x0F\x84\x5b\x07\x00\x00",
		"xxxxxx");


	objCount = (int*)(*(DWORD*)(sigscan(
		L"MBTL.exe",
		"\x8D\x4D\xDC\xA3",
		"xxxx") + 0x4));
	objList_address = (DWORD)(objCount)+0x4;

	DWORD temp = sigscan(
		L"MBTL.exe",
		"\xC7\x47\x10\x00\x00\x00\x00\xff\x35",
		"xxxxxxxxx");
	resolutionY = (int*)(*(DWORD*)(temp + 0x9));
	resolutionX = (int*)(*(DWORD*)(temp + 0x9 + 0x6));

	cameraPosX = (int*)(*(DWORD*)(sigscan(
		L"MBTL.exe",
		"\xC1\xE7\x07\xBE",
		"xxxx") + 0xA));

	cameraPosY = cameraPosX + 0x1;

	cameraZoom = (float*)(cameraPosX + 0x3);
	
	mINI::INIFile file("dll_loader.ini");
	mINI::INIStructure ini;
	if (file.read(ini)) {
		if (ini.has("hitbox_viewer")) {
			if (ini["hitbox_viewer"].has("toggle_hitbox_on_launch")) {
				toggleHitbox = ini["hitbox_viewer"]["toggle_hitbox_on_launch"] == "1";
			}
			if (ini["hitbox_viewer"].has("unlock_additional_color_slots")) {
				unlockColorSlots = ini["hitbox_viewer"]["unlock_additional_color_slots"] == "1";
			}
			if (ini["hitbox_viewer"].has("color_slot_numbers")) {
				pal_num = std::stoi(ini["hitbox_viewer"]["color_slot_numbers"]);// maximum is 42
			}
		}
	}
	if (unlockColorSlots) {
		DWORD p = (DWORD)(sigscan(
			L"MBTL.exe",
			"\xb9\x20\x00\x00\x00\xc6\x00\x00",
			"xxxxxxxx") + 0x5);
		WriteProcessMemory(phandle, (LPVOID)(p), new BYTE[3]{ 0x90,0x90,0x90 }, 3, 0);

		BYTE pal[42];
		for (int i = 0; i < 42; i++) {
			if (i < pal_num) {
				pal[i] = 1;
			}
			else {
				pal[i] = 0;
			}
		}
		DWORD address = *(DWORD*)(sigscan(
			L"MBTL.exe",
			"\x8a\x84\xc7",
			"xxx") + 0x3);

		for (int i = 0; i < 0x20; i++) {
			WriteProcessMemory(phandle, (LPVOID)(address+i*0x30), pal, 42, 0);
		}
	}
	
	TCHAR szDllPath[MAX_PATH] = { 0 };
	GetSystemDirectory(szDllPath, MAX_PATH);
	std::wstring sPath = szDllPath;

	while (!vtable)
	{
		Sleep(1000);
		
		DWORD* ptr = (DWORD*)(sigscan(L"MBTL.exe", "\x89\x7d\xf8\x8b\x47", "xxxxx"));
		if (ptr != nullptr) {
			ptr = (DWORD*)*(ptr - 1);
			if (ptr != nullptr) {
				ptr = (DWORD*)*(ptr);
				if (ptr != nullptr) {
					ptr = (DWORD*)*(ptr + 1);
					if (ptr != nullptr) {
						ptr = (DWORD*)*(ptr + 1);
					}
				}
			}

		}
		if (ptr != nullptr) {
			vtable = *(void***)ptr;
		}
		/*

		vtable = *(void***)(sigscan(
			sPath + L"\\d3d9.dll",
			"\xC7\x06\x00\x00\x00\x00\x89\x86\x00\x00\x00\x00\x89\x86",
			"xx????xx????xx") + 0x2);
		*/
	}

	// Hook Present
	oPresent = (f_Present)vtable[17];

	DetourRestoreAfterWith();

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach(&(LPVOID&)oPresent, Hooked_Present);
	DetourTransactionCommit();
	

	return false;

}



BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		CreateThread(0, 0, MainThread, hModule, 0, 0);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}