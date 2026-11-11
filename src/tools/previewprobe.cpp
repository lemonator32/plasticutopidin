// previewprobe: feed a .clip file to Clip Studio's own Explorer preview handler
// (ClipPreview.dll, CLSID {9E6FA56B-AE86-49F9-AA1F-2517D81DE85C}) and report
// what it rendered.  The handler opens the container and the SQLite payload
// through CSP's PWSQLiteVFS and draws CanvasPreview.ImageData, so a rendered
// image means those layers are compatible with CSP's reader.  The handler
// returns S_OK even for unreadable files, so the pixel count is the signal.
//
//   previewprobe <file.clip> [path\to\ClipPreview.dll]
#include <windows.h>
#include <shobjidl.h>
#include <propsys.h>
#include <shlwapi.h>

#include <cstdint>
#include <cstdio>
#include <string>

static const CLSID kClsid = {0x9E6FA56B, 0xAE86, 0x49F9, {0xAA, 0x1F, 0x25, 0x17, 0xD8, 0x1D, 0xE8, 0x5C}};

static std::wstring widen(const char* s) {
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    w.pop_back();
    return w;
}

static void pump(DWORD ms) {
    MSG msg;
    DWORD until = GetTickCount() + ms;
    while (GetTickCount() < until) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        Sleep(20);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("usage: previewprobe <file.clip> [ClipPreview.dll]"); return 2; }
    const char* dllPath = argc > 2 ? argv[2] : "C:\\Program Files\\CELSYS\\CLIP STUDIO 1.5\\CLIP STUDIO PAINT\\ClipPreview.dll";
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE dll = LoadLibraryA(dllPath);
    if (!dll) { std::printf("cannot load %s (error %lu)\n", dllPath, GetLastError()); return 3; }
    typedef HRESULT(WINAPI * GetClassObjectFn)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    if (!getClassObject) { std::puts("no DllGetClassObject"); return 3; }
    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(kClsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) { std::printf("DllGetClassObject: 0x%08lx\n", hr); return 3; }
    IPreviewHandler* handler = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IPreviewHandler, reinterpret_cast<void**>(&handler));
    factory->Release();
    if (FAILED(hr)) { std::printf("CreateInstance: 0x%08lx\n", hr); return 3; }

    std::wstring path = widen(argv[1]);
    IInitializeWithFile* initFile = nullptr;
    if (SUCCEEDED(handler->QueryInterface(IID_IInitializeWithFile, reinterpret_cast<void**>(&initFile)))) {
        hr = initFile->Initialize(path.c_str(), STGM_READ);
        std::printf("IInitializeWithFile::Initialize: 0x%08lx\n", hr);
        initFile->Release();
    } else {
        std::puts("handler does not support IInitializeWithFile");
    }

    HWND host = CreateWindowExW(0, L"STATIC", L"previewprobe", WS_OVERLAPPEDWINDOW, 0, 0, 400, 400, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    RECT rc{0, 0, 400, 400};
    handler->SetWindow(host, &rc);
    hr = handler->DoPreview();
    std::printf("DoPreview: 0x%08lx\n", hr);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    pump(2000);

    HWND child = GetWindow(host, GW_CHILD);
    HWND target = child ? child : host;
    RECT wr;
    GetClientRect(target, &wr);
    int w = wr.right, h = wr.bottom;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(target, mem, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
    size_t coloured = 0, nonWhite = 0;
    const uint32_t* px = static_cast<const uint32_t*>(bits);
    for (int i = 0; i < w * h; ++i) {
        uint32_t v = px[i] & 0xFFFFFF;
        if (v != 0xF0F0F0 && v != 0xFFFFFF && v != 0) ++coloured;
        if (v != 0xFFFFFF) ++nonWhite;
    }
    std::printf("rendered %dx%d (child window: %s): %zu coloured pixels, %zu non-white => %s\n", w, h, child ? "yes" : "no", coloured,
                nonWhite, nonWhite > 0 ? "PREVIEW DRAWN" : "BLANK");
    std::string bmpPath = std::string(argv[1]) + ".preview.bmp";
    if (FILE* f = std::fopen(bmpPath.c_str(), "wb")) {
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + w * h * 4;
        std::fwrite(&fh, sizeof fh, 1, f);
        std::fwrite(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        std::fwrite(bits, 1, static_cast<size_t>(w) * h * 4, f);
        std::fclose(f);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    handler->Unload();
    handler->Release();
    DestroyWindow(host);
    CoUninitialize();
    return nonWhite > 0 ? 0 : 1;
}
