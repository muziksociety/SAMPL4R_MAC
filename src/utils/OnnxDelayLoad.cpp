// OnnxDelayLoad.cpp - delay-load failure hook for onnxruntime.dll
// PURE Win32 only - no CRT, no std::, no STL. The hook may run before CRT
// init completes, so anything dependent on the C runtime is unsafe here.
//
// Linked into SamplarPlugin_VST3 via CMakeLists (`/DELAYLOAD:onnxruntime.dll`
// + delayimp.lib).

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <delayimp.h>

// -----------------------------------------------------------------------
// Minimal raw-Win32 logger. Writes ASCII line to a HARDCODED path so we
// don't depend on env var resolution or CRT.
// -----------------------------------------------------------------------
// OPT-IN diagnostic only: nothing is written unless the environment variable
// SAMPLAR_DLL_LOAD_TRACE=1 is set (support / dev machines). Customer machines
// never get this file.
static const wchar_t kLogPath[] = L"C:\\Users\\Public\\samplar_dll_load.log";

static bool samplar_traceEnabled()
{
    wchar_t value[4] = {};
    const DWORD n = GetEnvironmentVariableW(L"SAMPLAR_DLL_LOAD_TRACE", value, 4);
    return n == 1 && value[0] == L'1';
}

static void samplar_writeLog(const char* msg)
{
    if (!samplar_traceEnabled()) return;
    HANDLE h = CreateFileW(kLogPath, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    int len = 0; while (msg[len]) ++len;
    WriteFile(h, msg, (DWORD)len, &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

// Strip case-insensitive ASCII compare (no _stricmp dependency)
static int samplar_streqi(const char* a, const char* b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return (*a == 0 && *b == 0);
}

// -----------------------------------------------------------------------
// Helper: resolve the full path to our co-located onnxruntime.dll
// -----------------------------------------------------------------------
static HMODULE samplar_loadCoLocatedOnnx(const char* tag)
{
    HMODULE hSelf = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&samplar_loadCoLocatedOnnx),
            &hSelf))
    {
        samplar_writeLog("loadCoLocatedOnnx: GetModuleHandleExW FAILED");
        return nullptr;
    }

    wchar_t modulePath[MAX_PATH * 2] = { 0 };
    DWORD len = GetModuleFileNameW(hSelf, modulePath, MAX_PATH * 2);
    if (len == 0 || len >= MAX_PATH * 2) {
        samplar_writeLog("loadCoLocatedOnnx: GetModuleFileNameW FAILED");
        return nullptr;
    }

    int lastSlash = -1;
    for (int i = 0; modulePath[i] != 0 && i < MAX_PATH * 2; ++i) {
        if (modulePath[i] == L'\\' || modulePath[i] == L'/') lastSlash = i;
    }
    if (lastSlash < 0) return nullptr;

    static const wchar_t kOrtName[] = L"onnxruntime.dll";
    int p = lastSlash + 1;
    for (int i = 0; kOrtName[i] != 0 && p < MAX_PATH * 2 - 1; ++i)
        modulePath[p++] = kOrtName[i];
    modulePath[p] = 0;

    // Narrow-encode for logging
    char narrow[MAX_PATH * 2];
    int np = 0;
    while (tag[np] && np < 128) { narrow[np] = tag[np]; ++np; }
    narrow[np++] = ' ';
    for (int i = 0; modulePath[i] != 0 && np < MAX_PATH * 2 - 1; ++i)
        narrow[np++] = (modulePath[i] < 0x80) ? (char)modulePath[i] : '?';
    narrow[np] = 0;
    samplar_writeLog(narrow);

    HMODULE hMod = LoadLibraryExW(modulePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (hMod == nullptr) {
        samplar_writeLog("loadCoLocatedOnnx: LoadLibraryExW FAILED");
    }
    return hMod;
}

// -----------------------------------------------------------------------
// Delay-load NOTIFY hook - fires for SUCCESS-path events BEFORE the helper
// does any work. By returning our HMODULE at dliNotePreLoadLibrary we
// pre-empt the broken System32 stub.
// -----------------------------------------------------------------------
static FARPROC WINAPI samplar_dliNotifyHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (pdli == nullptr || pdli->szDll == nullptr) return nullptr;

    if (!samplar_streqi(pdli->szDll, "onnxruntime.dll")) return nullptr;

    if (dliNotify == dliNotePreLoadLibrary) {
        samplar_writeLog("notify: dliNotePreLoadLibrary for onnxruntime.dll");
        HMODULE h = samplar_loadCoLocatedOnnx("notify-preload:");
        if (h != nullptr) {
            samplar_writeLog("notify: returning our co-located HMODULE");
            return reinterpret_cast<FARPROC>(h);
        }
        samplar_writeLog("notify: co-located load failed, falling back to default");
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// Safe-failure stub for OrtGetApiBase (audit S-H2). Returned by the failure
// hook when no usable onnxruntime.dll exports it: the first ORT call then
// gets a null API base and samplar::ensureOrtApi() reports "runtime
// unavailable" instead of the delay-load helper raising 0xC06D007F.
// A DLL that cannot be LOADED at all (dliFailLoadLib) cannot be faked with a
// module handle; that exception is caught by the __try/__except in
// src/services/separation/OrtRuntime.cpp, the ONLY place that triggers the
// load (ORT_API_MANUAL_INIT: no static initialiser touches ORT any more).
// -----------------------------------------------------------------------
static const void* samplar_nullOrtGetApiBase(void)
{
    return nullptr;
}

// -----------------------------------------------------------------------
// Delay-load FAILURE hook - fires when LoadLibrary or GetProcAddress fails.
// Catches dliFailLoadLib AND dliFailGetProc (the System32 stub case).
// -----------------------------------------------------------------------
static FARPROC WINAPI samplar_dliFailureHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (pdli == nullptr || pdli->szDll == nullptr) return nullptr;
    if (!samplar_streqi(pdli->szDll, "onnxruntime.dll")) return nullptr;

    if (dliNotify == dliFailLoadLib) {
        samplar_writeLog("failure: dliFailLoadLib for onnxruntime.dll");
        HMODULE h = samplar_loadCoLocatedOnnx("failure-load:");
        return reinterpret_cast<FARPROC>(h);
    }
    if (dliNotify == dliFailGetProc) {
        samplar_writeLog("failure: dliFailGetProc - System32 stub case");
        if (pdli->dlp.fImportByName && pdli->dlp.szProcName != nullptr) {
            char line[160] = "failure: missing proc ";
            int n = 22;
            for (int i = 0; pdli->dlp.szProcName[i] && n < 158; ++i) line[n++] = pdli->dlp.szProcName[i];
            line[n] = 0;
            samplar_writeLog(line);
        } else {
            samplar_writeLog("failure: missing proc imported by ordinal");
        }
        // Load the real DLL and resolve the missing proc through it.
        HMODULE h = samplar_loadCoLocatedOnnx("failure-getproc:");
        if (h != nullptr && pdli->dlp.fImportByName) {
            FARPROC p = GetProcAddress(h, pdli->dlp.szProcName);
            if (p != nullptr) {
                samplar_writeLog("failure: resolved proc through co-located DLL");
                return p;
            }
            samplar_writeLog("failure: GetProcAddress on real DLL still NULL");
        }
        // onnxruntime.lib imports OrtGetApiBase BY ORDINAL (1); with ORT_API_MANUAL_INIT it is
        // the only onnxruntime.dll import of the module (verified with dumpbin /imports).
        const bool isOrtGetApiBase = pdli->dlp.fImportByName
            ? (pdli->dlp.szProcName != nullptr && samplar_streqi(pdli->dlp.szProcName, "OrtGetApiBase"))
            : (pdli->dlp.dwOrdinal == 1);
        if (isOrtGetApiBase)
        {
            samplar_writeLog("failure: returning null OrtGetApiBase stub (runtime unavailable)");
            return reinterpret_cast<FARPROC>(&samplar_nullOrtGetApiBase);
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// TLS callback: pre-loads onnxruntime.dll BEFORE _DllMainCRTStartup runs.
//
// Why TLS: callbacks here fire at DLL_PROCESS_ATTACH BEFORE CRT init,
// static constructors, or any user code. By the time delay-load tries to
// resolve onnxruntime.dll, Windows already has our real DLL in process
// memory and returns its HMODULE - bypassing the System32 WinML stub.
// -----------------------------------------------------------------------
static VOID NTAPI samplar_tls_callback(PVOID /*DllHandle*/, DWORD reason, PVOID /*Reserved*/)
{
    if (reason != DLL_PROCESS_ATTACH) return;

    OutputDebugStringA("[SAMPLAR] TLS callback firing - pre-loading onnxruntime.dll\n");
    samplar_writeLog("TLS callback: DLL_PROCESS_ATTACH - pre-loading onnxruntime.dll");

    HMODULE h = samplar_loadCoLocatedOnnx("TLS preload:");
    if (h != nullptr) {
        samplar_writeLog("TLS callback: ORT pre-loaded OK - SAMPLAR can now use ONNX safely");
    } else {
        samplar_writeLog("TLS callback: ORT pre-load FAILED - separation/BeatThis will report 'runtime unavailable'");
    }
}

// Register the TLS callback in .CRT$XLB section (TLS callback array).
// .CRT$XLA = start, .CRT$XLB-XLY = callbacks, .CRT$XLZ = end.
#pragma section(".CRT$XLB", long, read)
extern "C" __declspec(allocate(".CRT$XLB"))
    PIMAGE_TLS_CALLBACK samplar_tls_callback_ptr = samplar_tls_callback;

// Force the linker to keep our TLS callback AND emit a TLS directory.
// /INCLUDE:_tls_used pulls in the linker-generated TLS directory header,
// which is what tells Windows there are TLS callbacks to run.
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:samplar_tls_callback_ptr")

// Define both hooks. delayimp.h declares them as
// `extern const PfnDliHook __pfnDliNotifyHook2;`  (pre-load notification)
// `extern const PfnDliHook __pfnDliFailureHook2;` (failure recovery)
extern "C" const PfnDliHook __pfnDliNotifyHook2  = samplar_dliNotifyHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = samplar_dliFailureHook;

// Force the linker to keep this object file. Without these directives MSVC
// dead-strips our .obj because no other code references our hooks, and the
// default null hooks from delayimp.lib win.
#pragma comment(linker, "/INCLUDE:__pfnDliNotifyHook2")
#pragma comment(linker, "/INCLUDE:__pfnDliFailureHook2")

#endif // _WIN32
