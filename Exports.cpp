#include "GdiFontManager.h"

// Each addon loads its own copy of this DLL, so the warmup state is a named mapping
// 0 idle, 1 running, 2 finished.
LONG* GetFontWarmupState()
{
    static LONG* pState = nullptr;
    if (pState)
        return pState;

    wchar_t name[64];
    swprintf_s(name, 64, L"Local\\gdifonttexture.fontwarmup.%lu", ::GetCurrentProcessId());

    HANDLE mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG), name);
    if (!mapping)
        return nullptr;

    auto pView = (LONG*)::MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(LONG));
    if (!pView)
    {
        ::CloseHandle(mapping);
        return nullptr;
    }

    // Leave the handle open so other copies can open the same name
    pState = pView;
    return pState;
}

DWORD WINAPI FontWarmupThread(LPVOID lpParameter)
{
    auto pState = (LONG*)lpParameter;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiplusStartupInput, NULL);

    // Arial is only a probe name. FontFamilyNotFound still finishes the warmup
    Gdiplus::GpFontFamily* pFamily = NULL;
    Gdiplus::DllExports::GdipCreateFontFamilyFromName(L"Arial", NULL, &pFamily);

    ::InterlockedExchange(pState, 2);
    return 0;
}

int CALLBACK EnumFontFamExProc(const LOGFONT* lpelfe, const TEXTMETRIC* lpntme, DWORD FontType, LPARAM lParam)
{
    *((LPARAM*)lParam) = 1;
    return 1;
}

extern "C"
{
    extern __declspec(dllexport) GdiFontManager* CreateFontManager(IDirect3DDevice8* pDirect3DDevice)
    {
        return new GdiFontManager(pDirect3DDevice);
    }
    extern __declspec(dllexport) void DestroyFontManager(GdiFontManager* pFontManager)
    {
        delete pFontManager;
    }
    extern __declspec(dllexport) GdiFontReturn_t CreateTexture(GdiFontManager* pFontManager, GdiFontData_t* data)
    {
        return pFontManager->CreateFontTexture(*data);
    }
    extern __declspec(dllexport) GdiFontReturn_t CreateRectTexture(GdiFontManager* pFontManager, GdiRectData_t* data)
    {
        return pFontManager->CreateRectTexture(*data);
    }
    extern __declspec(dllexport) bool GetFontAvailable(const char* font)
    {
        LOGFONT lf = {0};
        lf.lfCharSet = DEFAULT_CHARSET;
        swprintf_s(lf.lfFaceName, 32, L"%S", font);
        LPARAM lParam = 0;
        ::EnumFontFamiliesEx(GetDC(nullptr), &lf, EnumFontFamExProc, (LPARAM)&lParam, 0);
        return lParam ? true : false;
    }
    extern __declspec(dllexport) void EnableTextureDump(GdiFontManager* pFontManager, const char* folder)
    {
        pFontManager->EnableTextureDump(folder);
    }
    extern __declspec(dllexport) void DisableTextureDump(GdiFontManager* pFontManager)
    {
        pFontManager->DisableTextureDump();
    }
    extern __declspec(dllexport) void BeginFontWarmup()
    {
        auto pState = GetFontWarmupState();
        if (!pState)
            return;

        if (::InterlockedCompareExchange(pState, 1, 0) != 0)
            return;

        HANDLE thread = ::CreateThread(nullptr, 0, FontWarmupThread, pState, 0, nullptr);
        if (!thread)
        {
            ::InterlockedCompareExchange(pState, 0, 1);
            return;
        }
        ::CloseHandle(thread);
    }
    extern __declspec(dllexport) int FontWarmupState()
    {
        auto pState = GetFontWarmupState();
        if (!pState)
            return 0;
        return (int)*pState;
    }
}
