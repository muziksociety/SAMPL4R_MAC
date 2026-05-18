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
;         onnxruntime.dll     (delay-loaded by TLS callback)
;         models\             (htdemucs.onnx + beat_this.onnx)
;         data\               (pattern JSONs)
;         BACKGROUNDS\        (UI assets)
;         Samples\            (808 + GM kit)
;         tools\ffmpeg.exe    (non-WAV audio decode)
;
; Build:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" samplar.iss
;   Or use build_installer.ps1 which orchestrates cmake + ISCC.
; ==========================================================================

#define MyAppName       "SAMPLAR"
#define MyAppVersion    "1.0.0"
#define MyAppPublisher  "Sample Monsta"
#define MyAppURL        "https://samplemonsta.com"

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
Source: "..\..\build\SamplarPlugin_artefacts\Release\VST3\Samplar.vst3\*"; \
    DestDir: "{app}"; Flags: recursesubdirs ignoreversion

; --- VC++ 2022 Redistributable (silent install, deleted after run) ---
Source: "..\..\portable_cache\vc_redist.x64.exe"; \
    DestDir: "{tmp}"; Flags: deleteafterinstall

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
