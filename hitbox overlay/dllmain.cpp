#include "pch.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <Windows.h>
#include <psapi.h>
#include "detours.h"
#include <string>

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
DWORD p1_address_offset = 0x00028178;
int* pause;
DWORD pause_address_offset = 0x67FAF8;
DWORD pause_address2;
DWORD pause_address2_offset = 0x122202;

DWORD objList_address;
int* objCount;
DWORD objList_offset = 0x686424;
DWORD objCount_offset = 0x686420;

int* resolutionX;
int* resolutionY;
DWORD resolution_offset = 0x638578;

signed int* cameraPosX;
signed int* cameraPosY;
float* cameraZoom;
DWORD camera_offset = 0x668EC8;


// asm codes to patch the exe to pause the game
BYTE je[6] = { 0x0F,0x84,0xC3,0x06,0x00,0x00 };
BYTE jmp[6] = { 0xE9,0xC4,0x06,0x00,0x00,0x90 };


// variables for d3d hook
typedef HRESULT(_stdcall* f_Present)(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
f_Present oPresent;
void* d3d9Device[119];


// other global variables
int keyPressed = 0;
bool toggleHitbox = true;
//DWORD pause = 0;
bool frameStep = false;
LPD3DXFONT m_font;


void DrawRectangle(IDirect3DDevice9* pDevice, const float x1, const float x2, const float y1, const float y2,const float z, D3DCOLOR innerColor, D3DCOLOR outerColor) {
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
	pDevice->GetTexture(0,&texture);

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
void drawBox(IDirect3DDevice9* pDevice,int start, int end,DWORD boxAddr,float rx, float ry,BYTE facing, D3DCOLOR innerColor, D3DCOLOR outerColor) {
	for (int i = start; i < end; i++) {
		DWORD clsnAddress = *(DWORD*)(boxAddr + i * 4);
		if (clsnAddress != 0) {
			signed short *x1, *x2, *y1, *y2;
			x1 = (signed short*)clsnAddress;
			y1 = (signed short*)(clsnAddress + 0x2);
			x2 = (signed short*)(clsnAddress + 0x4);
			y2 = (signed short*)(clsnAddress + 0x6);
			float fx1, fx2, fy1, fy2;
			fx1 = ((*x1 * (facing == 0 ? 1.0f : -1.0f) + rx) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
			fx2 = ((*x2 * (facing == 0 ? 1.0f : -1.0f) + rx) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
			fy1 = ((*y1 + ry) * (*cameraZoom) + 638.0f) * (*resolutionY) / 720.0f;
			fy2 = ((*y2 + ry) * (*cameraZoom) + 638.0f) * (*resolutionY) / 720.0f;
			DrawRectangle(pDevice, fx1, fx2, fy1, fy2, 0, innerColor, outerColor);
		}
		
	}
}
void drawFrameData(IDirect3DDevice9* pDevice, DWORD objData, float rx, float ry) {
	bool isHurt = *(DWORD*)(objData + 0x2d4);
	int frameNum = 0;
	int totalFrames = 0;
	if (isHurt) {
		totalFrames = *(DWORD*)(objData + 0x2dc);
		if (totalFrames == -1) {
			totalFrames = *(BYTE*)(objData + 0x2c8)-1;
			frameNum = *(BYTE*)(objData + 0x2be);
		}
		else {
			frameNum = 1 + totalFrames - *(DWORD*)(objData + 0x2d8);
		}
	}
	else {
		DWORD state = *(DWORD*)((*(DWORD*)(objData + 0x6ac)) + 0x30);
		DWORD elem = *(DWORD*)(objData + 0x20);
		DWORD elemTime = *(DWORD*)(objData + 0x30);
		int i = 0;
		bool end = false;
		while (end == false && (*(DWORD*)(state + i * 0x4) != 0)) {
			if (i == elem) {
				frameNum = totalFrames + elemTime;
			}
			DWORD fa = (*(DWORD*)(state + i * 0x4) + 0x96);
			totalFrames += *(BYTE*)fa;//*(BYTE*)(*(DWORD*)(state+i) + 0x96);
			DWORD ea = (*(DWORD*)(state + i * 0x4) + 0x98);
			end = *(bool*)ea != 1;//(*(BYTE*)(*(DWORD*)(state + i) + 0x98)) == 2;
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
	
	if (m_font == NULL) {
		D3DXCreateFont(pDevice, 17, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Arial"), &m_font);
	}
	D3DCOLOR fontColor = D3DCOLOR_ARGB(255, 255, 255, 255);
	RECT rct; //Font
	rct.left = ((rx - 400.0f) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
	rct.right = ((rx + 400.0f) * (*cameraZoom) + 640.0f) * (*resolutionX) / 1280.0f;
	rct.top = ((10.0f + ry) * (*cameraZoom) + 638.0f) * (*resolutionY) / 720.0f;
	rct.bottom = ((60.0f + ry) * (*cameraZoom) + 638.0f) * (*resolutionY) / 720.0f;
	int k = 1;
	
	std::string text = std::to_string(frameNum)+"/"+ std::to_string(totalFrames);
	m_font->DrawTextA(NULL, text.c_str(), -1, &rct, DT_CENTER, fontColor);

}
void drawObj(IDirect3DDevice9* pDevice, DWORD objData, int drawBlue,DWORD state, bool drawFrame) {
	signed int *posX, *posX2, *posY, *posY2;
	BYTE* facing;
	posX = (signed int*)(objData + 0x64);
	posY = (signed int*)(objData + 0x68);
	posX2 = (signed int*)(objData + 0x70);
	posY2 = (signed int*)(objData + 0x74);
	facing = (BYTE*)(objData + 0x6A0);
	BYTE* numBox1;
	BYTE* numBox2;
	numBox1 = (BYTE*)(state + 0xb7);
	numBox2 = (BYTE*)(state + 0xb6);
	float rx, ry;
	rx = (*posX + *posX2 - (*cameraPosX)) / 128.0f;
	ry = (*posY + *posY2 - (*cameraPosY)) / 128.0f;
	if (*numBox2 > 0) {
		DWORD *boxAddress = (DWORD*)(state + 0xc0);
		if (drawBlue) {
			
			if (drawBlue == 2) {
				drawBox(pDevice, 1, (*numBox2 > 8 ? 8 : *numBox2), *boxAddress, rx, ry, *facing, hurtbox_armored_color1, hurtbox_armored_color2);
			}
			else {
				drawBox(pDevice, 1, (*numBox2 > 8 ? 8 : *numBox2), *boxAddress, rx, ry, *facing, hurtbox_color1, hurtbox_color2);
			}
			drawBox(pDevice, 0, 1, *boxAddress, rx, ry, *facing, pushbox_color1, pushbox_color2);
			if (*numBox2 >= 0xC) {
				drawBox(pDevice, 0, 1, *boxAddress +0xB*4, rx, ry, *facing, clashbox_color1, clashbox_color2);
			}
		}
	}
	if (*numBox1 > 0) {
		DWORD *boxAddress = (DWORD*)(state + 0xc4);
		drawBox(pDevice, 0, *numBox1, *boxAddress, rx, ry, *facing, hitbox_color1, hitbox_color2);
	}
	if (drawFrame) {
		drawFrameData(pDevice, objData, rx, ry);
	}
	
}
HRESULT _stdcall Hooked_Present(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
	if (!p1_address) {
		p1_address = *(DWORD*)(base_address + p1_address_offset);
	}
	
	if (toggleHitbox) {
		pDevice->BeginScene();
		DWORD state;
		DWORD obj_addrress = p1_address;
		state = *(DWORD*)(obj_addrress + 0x6b0);
		for (int i = 0; i < 4; i++) {
			if (state != 0) {
				DWORD c;
				int drawBlue = 1;
				int armor = 0;
				c = *(DWORD*)(obj_addrress + 0x5b4);
				if (c > 0) {
					c = *(DWORD*)(obj_addrress + 0x5a8);
					drawBlue = c != 0;
				}
				else {
					c = *(DWORD*)(obj_addrress + 0x2a2);
					drawBlue = c != 1;
				}
				if (drawBlue == 1) {
					c = *(DWORD*)(obj_addrress + 0x5d8);
					if (c > 0) {
						c = *(DWORD*)(obj_addrress + 0x5cc);
						if (c != 0) {
							c = *(DWORD*)(obj_addrress + 0x6b8);
							if (c != 0) {
								c = *(DWORD*)(obj_addrress + 0x970);
								armor = !c;
							}
						}
					}
				}


				drawObj(pDevice, obj_addrress, drawBlue + armor, state, true);
			}
			obj_addrress = obj_addrress + 0xbf0;
			state = *(DWORD*)(obj_addrress + 0x6b0);
		}
		if (*objCount > 0) {
			for (int i = 0; i < *objCount; i++) {
				obj_addrress = *(DWORD*)(objList_address + i * 4);
				if (obj_addrress != 0) {
					state = *(DWORD*)(obj_addrress + 0x6b0);
					if (state != 0) {
						DWORD c;
						int drawBlue = 1;
						c = *(DWORD*)(obj_addrress + 0x5b4);
						if (c > 0) {
							c = *(DWORD*)(obj_addrress + 0x5a8);
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
						drawObj(pDevice, obj_addrress, drawBlue, state,false);
					}
				}
			}
		}
		pDevice->EndScene();
	}

	if (!GetAsyncKeyState(VK_F5) && !GetAsyncKeyState(VK_F6) && !GetAsyncKeyState(VK_F7)) {
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
				if (GetAsyncKeyState(VK_F7) && (keyPressed == 1 || (keyPressed > 40 && keyPressed%10 == 0))) {
					frameStep = true;
				}
	}

	
	return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}


//Simple hook functions that are copied from somewhere
bool GetD3D9Device(void** pTable, size_t Size)
{

	if (!pTable)
	{
		return false;
	}


	IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D)
	{
		return false;
	}

	D3DPRESENT_PARAMETERS d3dpp = { 0 };
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = GetForegroundWindow();
	d3dpp.Windowed = ((GetWindowLong(d3dpp.hDeviceWindow, GWL_STYLE) & WS_POPUP) != 0) ? FALSE : TRUE;;


	IDirect3DDevice9* pDummyDevice = nullptr;
	HRESULT create_device_ret = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);

	if (!pDummyDevice || FAILED(create_device_ret))
	{
		//failed
		pD3D->Release();
		return false;
	}

	//CreateDevice successfull

	memcpy(pTable, *reinterpret_cast<void***>(pDummyDevice), Size);

	pDummyDevice->Release();
	pD3D->Release();

	return true;
}
HMODULE GetModule(HANDLE pHandle)
{
	HMODULE hMods[1024];
	DWORD cbNeeded;
	unsigned int i;

	if (EnumProcessModules(pHandle, hMods, sizeof(hMods), &cbNeeded))
	{
		for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
		{
			TCHAR szModName[MAX_PATH];
			if (GetModuleFileNameEx(pHandle, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
			{
				std::wstring wstrModName = szModName;
				std::wstring wstrModContain = L"MBTL.exe";
				if (wstrModName.find(wstrModContain) != std::string::npos)
				{
					return hMods[i];
				}
			}
		}
	}
	return nullptr;
}
DWORD WINAPI MainThread(LPVOID hModule)
{
	phandle = GetCurrentProcess();
	if (!phandle) {
		exit(0);
	}
	base_address = (DWORD)GetModule(phandle);
	if (!base_address) {
		exit(0);
	}
	pause = (int*)(base_address + pause_address_offset);
	pause_address2 = base_address + pause_address2_offset;
	objList_address = base_address + objList_offset;
	objCount = (int*)(base_address + objCount_offset);

	resolutionX = (int*)(base_address + resolution_offset + 0x4);
	resolutionY = (int*)(base_address + resolution_offset);

	cameraPosX = (int*)(base_address + camera_offset);
	cameraPosY = (int*)(base_address + camera_offset + 0x4);
	cameraZoom = (float*)(base_address + camera_offset + 0xc);



	Sleep(1000);
	
	if (GetD3D9Device(d3d9Device, sizeof(d3d9Device)))
	{
			// Hook Present
			oPresent = (f_Present)d3d9Device[17];

			DetourRestoreAfterWith();

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			DetourAttach(&(LPVOID&)oPresent, Hooked_Present);
			DetourTransactionCommit();

	}
	else {
		exit(0);
	}	
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