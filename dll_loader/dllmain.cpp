#include "pch.h"
#include <d3d9.h>
#include <windows.h>
#include <psapi.h>
#include "mINI.h"
#include <string>
#pragma comment(lib, "d3d9.lib")
//#pragma comment(lib, "d3dx9.lib")

TCHAR szDllPath[MAX_PATH] = { 0 };
typedef IDirect3D9* (WINAPI* FAA)(UINT a);
FAA Direct3DCreate9_out;

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    return Direct3DCreate9_out(SDKVersion);
}



DWORD WINAPI MainThread(LPVOID hModule)
{

    Sleep(2000);
    mINI::INIFile file("dll_loader.ini");
    mINI::INIStructure ini;

    file.read(ini);


    for (auto const& it2 : ini["dll_list"])
    {
        std::string s = it2.second;
        std::wstring p(s.begin(), s.end());
        HMODULE hDll2 = LoadLibrary(p.c_str());
        if (hDll2 == NULL)
        {
        }
    }
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /* lpReserved */)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(0, 0, MainThread, hModule, 0, 0);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    GetSystemDirectory(szDllPath, MAX_PATH);


    std::wstring fPath = szDllPath;
    std::wstring tmp = L"\\d3d9.dll";
    fPath = fPath + tmp;

    HMODULE hDll = LoadLibrary(fPath.c_str());

    if (hDll == NULL)
    {
        MessageBoxA(NULL, "failed to load d3d9.dll from system directory", "Error", MB_OK);
        return FALSE;
    }

    // Pointer to the original function

    Direct3DCreate9_out = (FAA)GetProcAddress(hDll, "Direct3DCreate9");
    if (Direct3DCreate9_out == NULL)
    {
        MessageBoxA(NULL, "failed to get Direct3DCreate9 address", "Error", MB_OK);
        FreeLibrary(hDll);
        return FALSE;
    }
    return TRUE;
}
