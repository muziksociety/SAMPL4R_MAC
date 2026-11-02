#include "GpuAdapterProbe.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dxgi.h>
 #include <d3d12.h>
 #include <psapi.h>
 #pragma comment(lib, "psapi.lib")
 #pragma comment(lib, "dxgi.lib")
 #pragma comment(lib, "d3d12.lib")
#endif

namespace samplar {

#if defined(_WIN32)

// True when the adapter shares system memory with the CPU (integrated GPU / APU). AMD APUs
// report their BIOS carve-out as "dedicated" memory, so the VRAM size alone does not catch them.
static bool isUnifiedMemoryAdapter (IDXGIAdapter1* adapter)
{
    ID3D12Device* device = nullptr;
    if (FAILED (D3D12CreateDevice (adapter, D3D_FEATURE_LEVEL_11_0, __uuidof (ID3D12Device),
                                   reinterpret_cast<void**> (&device))) || device == nullptr)
        return true;   // no D3D12 device -> DirectML cannot use it either
    D3D12_FEATURE_DATA_ARCHITECTURE arch {};
    const bool uma = FAILED (device->CheckFeatureSupport (D3D12_FEATURE_ARCHITECTURE, &arch, sizeof (arch)))
                     || arch.UMA;
    device->Release();
    return uma;
}

bool pickDiscreteGpuAdapter (std::uint64_t minDedicatedBytes, GpuAdapterInfo& out)
{
    out = {};
    IDXGIFactory1* factory = nullptr;
    if (FAILED (CreateDXGIFactory1 (__uuidof (IDXGIFactory1), reinterpret_cast<void**> (&factory))) || factory == nullptr)
        return false;

    GpuAdapterInfo best;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1 (i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc {};
        if (SUCCEEDED (adapter->GetDesc1 (&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
            && (std::uint64_t) desc.DedicatedVideoMemory > best.dedicatedVideoMemory
            && ! isUnifiedMemoryAdapter (adapter))
        {
            best.index = (int) i;
            best.name = desc.Description;
            best.dedicatedVideoMemory = (std::uint64_t) desc.DedicatedVideoMemory;
        }
        adapter->Release();
    }
    factory->Release();

    out = best;   // reported even when rejected, for logging
    return best.index >= 0 && best.dedicatedVideoMemory >= minDedicatedBytes;
}

bool preloadBundledDirectML (std::wstring& loadedPath)
{
    loadedPath.clear();
    // The folder of OUR module (SamplarApp.exe, or the SAMPLAR.vst3 binary in the bundle's
    // Contents/x86_64-win): CMake places DirectML.dll + onnxruntime.dll there. Not
    // GetModuleHandle("onnxruntime.dll"): inside a DAW another plugin's onnxruntime.dll may
    // have been loaded first and would point at the wrong folder.
    HMODULE self = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR> (&preloadBundledDirectML), &self))
        return false;
    wchar_t path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW (self, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return false;
    std::wstring dir (path, len);
    const auto slash = dir.find_last_of (L"\\/");
    if (slash == std::wstring::npos)
        return false;
    const std::wstring dmlPath = dir.substr (0, slash + 1) + L"DirectML.dll";
    HMODULE dml = LoadLibraryExW (dmlPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (dml == nullptr)
        return false;
    // ONNX Runtime resolves "DirectML.dll" by name: if another copy (System32's old 1.0, or a
    // different plugin's) was loaded first, the name lookup returns THAT one, not ours.
    if (GetModuleHandleW (L"DirectML.dll") != dml)
        return false;
    loadedPath = dmlPath;
    return true;
}

std::wstring loadedModulePath (const wchar_t* moduleName)
{
    HMODULE m = GetModuleHandleW (moduleName);
    if (m == nullptr)
        return {};
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameW (m, path, MAX_PATH * 2);
    return (len == 0 || len >= MAX_PATH * 2) ? std::wstring() : std::wstring (path, len);
}

int  getCurrentThreadPriorityLevel()                { return GetThreadPriority (GetCurrentThread()); }
void setCurrentThreadPriorityLevel (int level)      { SetThreadPriority (GetCurrentThread(), level); }
int  belowNormalThreadPriorityLevel()               { return THREAD_PRIORITY_BELOW_NORMAL; }

std::uint64_t processPeakWorkingSetBytes()
{
    PROCESS_MEMORY_COUNTERS pmc {};
    return GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)) ? (std::uint64_t) pmc.PeakWorkingSetSize : 0;
}

std::uint64_t processWorkingSetBytes()
{
    PROCESS_MEMORY_COUNTERS pmc {};
    return GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)) ? (std::uint64_t) pmc.WorkingSetSize : 0;
}

#else

bool pickDiscreteGpuAdapter (std::uint64_t, GpuAdapterInfo& out) { out = {}; return false; }
bool preloadBundledDirectML (std::wstring& loadedPath) { loadedPath.clear(); return false; }
std::wstring loadedModulePath (const wchar_t*) { return {}; }
int  getCurrentThreadPriorityLevel() { return 0; }
void setCurrentThreadPriorityLevel (int) {}
int  belowNormalThreadPriorityLevel() { return 0; }
std::uint64_t processPeakWorkingSetBytes() { return 0; }
std::uint64_t processWorkingSetBytes() { return 0; }

#endif

} // namespace samplar
