#pragma once

// GpuTeardownOffload - keeps JUCE's Direct2D / D3D11 teardown off the host's
// message thread when the LAST plugin instance is deleted.
//
// Evidence (impl_unload.md): when the last instance goes, JUCE's
// ~ScopedJuceInitialiser_GUI -> DeletedAtShutdown::deleteAll -> ~VBlankDispatcher
// drops the last SharedResourcePointer<DirectX>, and ~DxgiAdapters releases one
// D3D11 + D2D device per GPU (NtGdiDdDDIDestroyContext / DestroyAllocation2 in the
// NVIDIA and Intel drivers). On a loaded machine that kernel call blocked the
// host's message thread for 1.7 s .. 57 s while SAMPLAR's own destructor took
// 13-41 ms.
//
// Fix: a DeletedAtShutdown holder, registered in the processor constructor (so
// it is created BEFORE JUCE's VBlankDispatcher and therefore deleted AFTER it),
// keeps one 1x1 native (Direct2D) image = one extra DirectX reference. Its
// destructor hands that image to a detached thread that pins the module
// (GetModuleHandleEx), drops the image (the D3D/D2D release happens there) and
// exits through FreeLibraryAndExitThread, so module code is never run after the
// module is unmapped. The release thread never logs and touches nothing else.
namespace samplar::GpuTeardownOffload
{
    /** Message thread, every processor constructor: registers the holder once per
        JUCE lifetime (no-op when it already exists). Cheap: no GPU objects yet. */
    void registerHolder();

    /** Message thread, editor constructor: the holder takes its DirectX reference
        (JUCE creates the DirectX object for every editor window anyway). */
    void retainGpuState();
}
