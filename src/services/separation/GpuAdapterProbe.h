#pragma once

#include <cstdint>
#include <string>

namespace samplar {

// Windows GPU helpers for DirectML stem separation. Kept out of JUCE translation units so
// <windows.h>/<dxgi.h> never meet JUCE headers. On other platforms both return false.

struct GpuAdapterInfo
{
    int           index = -1;          // DXGI EnumAdapters1 index == DirectML device_id
    std::wstring  name;
    std::uint64_t dedicatedVideoMemory = 0;
};

// Picks the non-UMA hardware adapter (no integrated GPU / APU, checked via D3D12) with the
// most DEDICATED video memory and accepts it only if it has at least `minDedicatedBytes`. Integrated GPUs report a small dedicated carve-out
// (e.g. Intel HD 530: 128 MB) and measured slower than the CPU for htdemucs, so they are
// rejected; discrete cards (GTX 970M: 3 GB) pass. Software adapters are skipped.
bool pickDiscreteGpuAdapter (std::uint64_t minDedicatedBytes, GpuAdapterInfo& out);

// Loads DirectML.dll from the folder of SAMPLAR's own module (exe folder, or the VST3 bundle's
// Contents/x86_64-win), where it ships next to onnxruntime.dll. ONNX Runtime loads DirectML by name; Windows
// also ships an old System32\DirectML.dll (1.0) that this ORT build cannot use, and a
// module that is already loaded wins the name lookup. Returns the loaded path on success.
// Returns false if a DIFFERENT DirectML.dll already owns the name in this process.
bool preloadBundledDirectML (std::wstring& loadedPath);

// --- SAMPLAR additions (Win32-only helpers kept out of JUCE TUs; callers log the results) ---

// Full path of an already-loaded module (e.g. L"onnxruntime.dll"), empty when not loaded.
// Used to log WHICH onnxruntime.dll / DirectML.dll is live inside a DAW.
std::wstring loadedModulePath (const wchar_t* moduleName);

// Current thread's scheduling priority (Win32 THREAD_PRIORITY_* value; 0 elsewhere) and a
// setter. The separation worker runs below normal so the DAW's audio/UI threads win.
int  getCurrentThreadPriorityLevel();
void setCurrentThreadPriorityLevel (int level);
int  belowNormalThreadPriorityLevel();

// Peak / current working set of this process in bytes (0 when unavailable).
std::uint64_t processPeakWorkingSetBytes();
std::uint64_t processWorkingSetBytes();

} // namespace samplar
