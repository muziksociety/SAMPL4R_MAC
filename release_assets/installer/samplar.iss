; SAMPLAR Installer - Inno Setup script
; ==========================================================================
; Builds a single-file Windows installer for SAMPLAR.
;
; Prerequisites (the installer handles these):
;   - Microsoft Visual C++ 2022 Redistributable x64 (bundled, installed silently)
;
; Layout produced on disk:
;   C:\Program Files\Common Files\VST3\Samplar.vst3\
;     Contents\
;       moduleinfo.json
;       Resources\moduleinfo.json
;       x86_64-win\
;         SAMPLAR.vst3        (plugin binary)
;         onnxruntime.dll     (ONNX Runtime 1.19.2, DirectML build; delay-loaded)
;         DirectML.dll        (DirectML 1.15.1 redistributable; GPU stem separation)
;         models\             (htdemucs_body.onnx + beat_this.onnx)
;         data\               (install-location marker - never delete)
;       Resources\licenses\   (ONNX Runtime, DirectML, PFFFT notices)
;
; Build:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" samplar.iss
;   Or use build_installer.ps1 which orchestrates cmake + ISCC.
; ==========================================================================

#define MyAppName       "SAMPLAR"
#ifndef MyAppVersion
  #define MyAppVersion  "1.0.1"
#endif
#define MyAppPublisher  "Sample Monsta"
#define MyAppURL        "https://samplemonsta.com"

; --- Release guards: the staged bundle must hold the GPU runtime + exactly one separation
;     model (see CMakeLists.txt VST3 staging). Fail the compile instead of shipping a broken
;     or bloated installer. ---
#define BundleDir AddBackslash(SourcePath) + "..\..\build\SamplarPlugin_artefacts\Release\VST3\Samplar.vst3"
#if !FileExists(BundleDir + "\Contents\x86_64-win\onnxruntime.dll")
  #error Bundle is missing Contents\x86_64-win\onnxruntime.dll
#endif
#if !FileExists(BundleDir + "\Contents\x86_64-win\DirectML.dll")
  #error Bundle is missing Contents\x86_64-win\DirectML.dll (GPU stem separation)
#endif
#if !FileExists(BundleDir + "\Contents\x86_64-win\models\htdemucs_body.onnx")
  #error Bundle is missing models\htdemucs_body.onnx (stem separation model)
#endif
#if !FileExists(BundleDir + "\Contents\x86_64-win\models\beat_this.onnx")
  #error Bundle is missing models\beat_this.onnx
#endif
#if FileExists(BundleDir + "\Contents\x86_64-win\models\htdemucs.onnx")
  #error Stale full model models\htdemucs.onnx (290 MB) is in the bundle - only htdemucs_body.onnx may ship
#endif
#if !FileExists(BundleDir + "\Contents\Resources\licenses\DirectML-LICENSE.txt")
  #error Bundle is missing Contents\Resources\licenses\DirectML-LICENSE.txt
#endif

[Setup]
; --- App identity ---
AppId={{B3F4D7A2-9C18-4E5A-A0F2-1D6B8E7C3F91}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; --- Install location ---
; VST3 plugins live under Common Files\VST3 per the VST3 spec. This is the
; system-wide location every DAW scans (per-user fallback is also scanned by
; most DAWs but the system path is the universal canonical home).
DefaultDirName={cf}\VST3\Samplar.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no

; --- Admin required (writing to Program Files\Common Files\VST3) ---
PrivilegesRequired=admin

; --- 64-bit only ---
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

; --- Output ---
OutputDir=output
OutputBaseFilename=SAMPLAR-Setup-{#MyAppVersion}
SetupIconFile=
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4

; --- Wizard cosmetics ---
WizardStyle=modern
DisableWelcomePage=no
ShowLanguageDialog=no

; --- Uninstall ---
UninstallDisplayName={#MyAppName} {#MyAppVersion}
UninstallDisplayIcon={app}\Contents\x86_64-win\SAMPLAR.vst3

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; --- The entire Samplar.vst3 bundle (CMake POST_BUILD already populated it) ---
; Debug artefacts (*.map linker map, *.pdb) stay on the build machine: they
; make reverse-engineering the licensing code trivial.
Source: "..\..\build\SamplarPlugin_artefacts\Release\VST3\Samplar.vst3\*"; \
    DestDir: "{app}"; Excludes: "*.map,*.pdb,*.ilk,*.exp,*.lib,logs"; \
    Flags: recursesubdirs ignoreversion

; --- VC++ 2022 Redistributable (silent install, deleted after run) ---
Source: "..\..\portable_cache\vc_redist.x64.exe"; \
    DestDir: "{tmp}"; Flags: deleteafterinstall

[InstallDelete]
; Upgrades from 1.0.x. Runs before [Files] are copied.
; The 290 MB full separation model was replaced by models\htdemucs_body.onnx (GPU + CPU).
Type: files; Name: "{app}\Contents\x86_64-win\models\htdemucs.onnx"
Type: files; Name: "{app}\Contents\x86_64-win\onnxruntime_providers_shared.dll"
; Payload older installers shipped with zero runtime references (ffmpeg, UI backgrounds,
; sample kits, pattern JSONs). data\ itself stays: it is the install-location marker.
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\tools"
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\BACKGROUNDS"
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\Samples"
Type: files; Name: "{app}\Contents\x86_64-win\data\*.json"
Type: files; Name: "{app}\Contents\data\*.json"

[Run]
; Run VC++ Redist silently. Exit codes:
;   0    = success
;   1638 = newer version already installed
;   3010 = success, reboot required
Filename: "{tmp}\vc_redist.x64.exe"; \
    Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing Microsoft Visual C++ 2022 runtime..."; \
    Flags: waituntilterminated

[Registry]
; Register install path so other tools / our own diagnostic can find SAMPLAR
Root: HKLM; Subkey: "Software\SAMPLAR\SAMPLAR"; \
    ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; \
    Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\SAMPLAR\SAMPLAR"; \
    ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"

[UninstallDelete]
; Remove the entire bundle on uninstall
Type: filesandordirs; Name: "{app}"
; A stale portable pointer would otherwise redirect a future install to a
; folder that may no longer match the binary.
Type: files; Name: "{userappdata}\Samplar\portable_root.txt"
