; SAMPLAR - Inno Setup Installer Script
; This creates a professional Windows installer for SAMPLAR
;
; Prerequisites:
; 1. Install Inno Setup from https://jrsoftware.org/isinfo.php
; 2. Build SAMPLAR in Release mode
; 3. (Optional) Set up code signing certificate
;
; To build installer:
; "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss

#define MyAppName "SAMPLAR"
; Version is passed via /D flag from 001installer.ps1 (synced with git tag)
#ifndef MyAppVersion
  #define MyAppVersion "1.3.0"
#endif
#define MyAppPublisher "SAMPLAR"
#define MyAppURL "https://yourwebsite.com"
#define MyAppExeName "SAMPLAR.exe"
#define MyAppDescription "Audio Sample Processing & Variation Engine"

[Setup]
; Basic app info
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/support
AppUpdatesURL={#MyAppURL}/updates
AppCopyright=Copyright (C) 2025 {#MyAppPublisher}

; Installation directories
; UsePreviousAppDir=no forces the installer to ALWAYS use DefaultDirName,
; ignoring any stale registry entry from a previous install at a different path
; (e.g., ToneAndBeats). Without this, Inno Setup silently installs to the old
; location found via the AppId registry key.
DefaultDirName={autopf}\{#MyAppName}
UsePreviousAppDir=no
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes

; Output settings
OutputDir=installer_output
OutputBaseFilename=samplar_Setup_v{#MyAppVersion}
DiskSpanning=yes
DiskSliceSize=2100000000
; SetupIconFile=samplar.ico  ; Temporarily disabled - icon locked
UninstallDisplayIcon={app}\{#MyAppExeName}

; Logging — creates Setup Log YYYY-MM-DD #NNN.txt in user's TEMP folder
SetupLogging=yes

; Compression
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4

; Architecture
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Visual appearance
WizardStyle=modern
; WizardImageFile=assets\installer_banner.bmp
; WizardSmallImageFile=assets\installer_icon.bmp

; Privileges
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

; Code signing (uncomment when you have a certificate)
; SignTool=signtool /f "YourCertificate.pfx" /p "YourPassword" /t http://timestamp.digicert.com /d $q{#MyAppName}$q $f
; SignedUninstaller=yes

; License and info files
; LicenseFile=LICENSE.txt
; InfoBeforeFile=docs\INSTALLATION_NOTES.txt

; Version info
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppDescription}
VersionInfoCopyright=Copyright (C) 2025 {#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
; Quick Launch removed — obsolete since Windows 7

[Files]
; Main executable
Source: "SAMPLAR_Portable\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Background image for UI
Source: "SAMPLAR_Portable\REF.png"; DestDir: "{app}"; Flags: ignoreversion

; Portable configuration
Source: "SAMPLAR_Portable\SAMPLAR_portable.json"; DestDir: "{app}"; Flags: ignoreversion

; Python bridge (deprecated - SAMPLAR_Portable uses embedded Python instead)
Source: "SAMPLAR_Portable\python_bridge\*"; DestDir: "{app}\python_bridge"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Embedded Python environment (LARGE - 1.44 GB with PyTorch + audio-separator)
Source: "SAMPLAR_Portable\python\*"; DestDir: "{app}\python"; Flags: ignoreversion recursesubdirs createallsubdirs

; Python Scripts (CRITICAL - explicitly include to ensure audio-separator.exe is copied)
Source: "SAMPLAR_Portable\python\Scripts\*"; DestDir: "{app}\python\Scripts"; Flags: ignoreversion recursesubdirs createallsubdirs

; UVR/Demucs models (pre-packed for offline operation)
Source: "SAMPLAR_Portable\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs

; Logo/branding assets (optional)
Source: "SAMPLAR_Portable\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; VC++ 2022 Redistributable (x64) - extract to temp for installation
Source: "installer_dependencies\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; VST3 Plugin (installed to system VST3 folder for all DAWs)
Source: "SAMPLAR_Portable\VST3\SAMPLAR.vst3\*"; DestDir: "{commoncf}\VST3\SAMPLAR.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; Game assets (optional)
Source: "SAMPLAR_Portable\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; UI background assets (GIFs, PNGs — tileset, animated backgrounds)
Source: "SAMPLAR_Portable\BACKGROUNDS\*"; DestDir: "{app}\BACKGROUNDS"; Flags: ignoreversion recursesubdirs createallsubdirs

; Drum samples (808 preview kit + GM samples) — used by PatternLibraryDialog preview
Source: "SAMPLAR_Portable\Samples\*"; DestDir: "{app}\Samples"; Flags: ignoreversion recursesubdirs createallsubdirs

; Pattern databases (rhythmic patterns for step sequencer)
Source: "SAMPLAR_Portable\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

; Documentation (optional)
Source: "SAMPLAR_Portable\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme skipifsourcedoesntexist

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; Quick Launch icon removed — obsolete since Windows 7

[Dirs]
; Create models folder (required for audio-separator model downloads)
Name: "{app}\models"
Name: "{app}\models\huggingface"

[Registry]
; File associations (optional - for .sm5 project files)
Root: HKA; Subkey: "Software\Classes\.sm5"; ValueType: string; ValueName: ""; ValueData: "samplarProject"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\samplarProject"; ValueType: string; ValueName: ""; ValueData: "SAMPLAR Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\samplarProject\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\samplarProject\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

; App settings registry
Root: HKA; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"

[Run]
; Install VC++ 2022 Redistributable (x64) - REQUIRED for C++ apps
; Download from: https://aka.ms/vs/17/release/vc_redist.x64.exe
; Place in installer_dependencies folder
; Parameters: /install = install, /quiet = no UI, /norestart = don't restart
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime (required)..."; Flags: waituntilterminated runhidden; Check: VCRedistNeedsInstall

; Launch app after installation
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[InstallDelete]
; ── Clean up stale DLLs/exes from previous installs ──────────────────────
; v1.2.0 removed LibTorch, demucs.cpp CLI, and OpenBLAS from the build.
; But upgrading from an older install leaves these files in {app} because
; Inno Setup only ADDS files — it never deletes old ones automatically.
; Windows DLL loader finds them in the same directory as the exe, causing
; "entry point not found" crashes (caffe2/torch symbol resolution failures).

; LibTorch DLLs (LARS drum separator — removed in v1.2.0)
Type: files; Name: "{app}\c10.dll"
Type: files; Name: "{app}\torch.dll"
Type: files; Name: "{app}\torch_cpu.dll"
Type: files; Name: "{app}\torch_global_deps.dll"
Type: files; Name: "{app}\asmjit.dll"
Type: files; Name: "{app}\fbgemm.dll"
Type: files; Name: "{app}\fbjni.dll"
Type: files; Name: "{app}\pytorch_jni.dll"
Type: files; Name: "{app}\uv.dll"
Type: files; Name: "{app}\libiomp5md.dll"
Type: files; Name: "{app}\libiompstubs5md.dll"

; OpenBLAS (demucs.cpp CLI dependency — removed in v1.2.0)
Type: files; Name: "{app}\libopenblas.dll"

; Demucs C++ CLI executables (removed in v1.2.0 — UVR/audio-separator is the only backend now)
Type: files; Name: "{app}\demucs_mt_openblas.exe"
Type: files; Name: "{app}\demucs_mt_mkl.exe"
Type: files; Name: "{app}\demucs_mt.cpp.main.exe"

; LARS TorchScript models (removed in v1.2.0)
Type: filesandordirs; Name: "{app}\models\drumsep"

; ── Clean up OLD install locations (product name variants) ────────────────
; Clean up any "SAMPLAR" variant (old product name with "5")
Type: filesandordirs; Name: "{commonpf}\SAMPLAR"

[UninstallDelete]
; Clean up log files and cache on uninstall
Type: files; Name: "{app}\logs\*.log"
Type: files; Name: "{app}\*.log"
Type: dirifempty; Name: "{app}\logs"
; Clean up VST3 plugin on uninstall
Type: filesandordirs; Name: "{commoncf}\VST3\SAMPLAR.vst3"
Type: filesandordirs; Name: "{commoncf}\VST3\SAMPLAR.vst3"
; Clean up BACKGROUNDS, Samples, and data in AppData on uninstall
Type: filesandordirs; Name: "{userappdata}\Samplar\BACKGROUNDS"
Type: filesandordirs; Name: "{userappdata}\Samplar\Samples"
Type: filesandordirs; Name: "{userappdata}\Samplar\data"

[Code]
// ── Auto-uninstall previous versions before installing ──────────────────
// Finds any previous SAMPLAR uninstaller via registry and runs it
// silently. Then nukes known old install directories.
// This prevents stale DLLs (LibTorch, demucs, etc.) from causing
// "Entry Point Not Found" crashes after upgrade.

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  UninstallString: String;
  ResultCode: Integer;
begin
  Result := '';  // empty = no error, continue install

  // 1. Run the previous version's uninstaller if found (same AppId)
  //    Check HKLM (admin install) then HKCU (per-user install)
  UninstallString := '';
  if RegQueryStringValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}_is1',
                         'UninstallString', UninstallString) then
  begin
    Log('Found previous HKLM install, running uninstaller silently...');
    Exec(RemoveQuotes(UninstallString), '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE,
         ewWaitUntilTerminated, ResultCode);
    Log('Uninstaller exited with code: ' + IntToStr(ResultCode));
  end
  else if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}_is1',
                              'UninstallString', UninstallString) then
  begin
    Log('Found previous HKCU install, running uninstaller silently...');
    Exec(RemoveQuotes(UninstallString), '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE,
         ewWaitUntilTerminated, ResultCode);
    Log('Uninstaller exited with code: ' + IntToStr(ResultCode));
  end;

  // 2. Nuke known old install directories that may have stale DLLs.
  //    These may remain even after the uninstaller runs (incomplete cleanup).
  if DirExists(ExpandConstant('{commonpf}\SAMPLAR')) then
  begin
    Log('Deleting old "SAMPLAR" directory...');
    DelTree(ExpandConstant('{commonpf}\SAMPLAR'), True, True, True);
  end;
end;

// Copy all files from BACKGROUNDS/ to AppData for VST3 access
procedure CopyBackgroundsToAppData();
var
  FindRec: TFindRec;
  SrcDir, DestDir: String;
begin
  SrcDir := ExpandConstant('{app}\BACKGROUNDS\');
  DestDir := ExpandConstant('{userappdata}\Samplar\BACKGROUNDS\');
  if FindFirst(SrcDir + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          CopyFile(SrcDir + FindRec.Name, DestDir + FindRec.Name, False);
          Log('Copied background: ' + FindRec.Name);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

// Copy all files from Samples/ to AppData for VST3 access (808 kit + GM samples)
procedure CopySamplesToAppData();
var
  FindRec: TFindRec;
  SrcDir, DestDir: String;
begin
  SrcDir := ExpandConstant('{app}\Samples\');
  DestDir := ExpandConstant('{userappdata}\Samplar\Samples\');
  if FindFirst(SrcDir + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          CopyFile(SrcDir + FindRec.Name, DestDir + FindRec.Name, False);
          Log('Copied sample: ' + FindRec.Name);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

// Check if VC++ 2022 Redistributable (x64) is already installed
function VCRedistNeedsInstall: Boolean;
var
  Version: String;
  Installed: Cardinal;
begin
  Result := True; // Default to needing installation

  // Check registry for VC++ 2015-2022 x64 (they share the same runtime)
  // Registry path: HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64', 'Version', Version) then
  begin
    // Check if Installed flag is set to 1
    if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64', 'Installed', Installed) then
    begin
      if Installed = 1 then
      begin
        // Already installed
        Result := False;
        Log('VC++ Redistributable already installed: ' + Version);
      end;
    end;
  end;

  if Result then
    Log('VC++ Redistributable needs installation');
end;

// Check if Python is installed (optional - for better Demucs performance)
function IsPythonInstalled: Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec('python', '--version', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// Check available disk space
function CheckDiskSpace: Boolean;
var
  RequiredSpace: Int64;
begin
  RequiredSpace := 500 * 1024 * 1024; // 500 MB
  // Note: Inno Setup automatically checks disk space, this is just for custom messaging
  Result := True; // Let Inno Setup handle disk space checks
end;

// Check Windows version
function IsWindows10OrLater: Boolean;
begin
  Result := (GetWindowsVersion >= $0A000000); // Windows 10 = version 10.0
end;

// Pre-installation checks
function InitializeSetup: Boolean;
begin
  Result := True;
  
  // Check Windows version
  if not IsWindows10OrLater then
  begin
    MsgBox('SAMPLAR requires Windows 10 or later.', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  
  // Check disk space
  if not CheckDiskSpace then
  begin
    Result := False;
    Exit;
  end;
  
  // Warn if Python not installed (optional)
  if not IsPythonInstalled then
  begin
    if MsgBox('Python was not detected on your system.' + #13#10 +
              'SAMPLAR includes a Python bridge for advanced features.' + #13#10 +
              'The app will work, but some features may be slower.' + #13#10#13#10 +
              'Continue installation?', mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

// Post-installation message
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Create logs directory in AppData (writable — Program Files is not)
    CreateDir(ExpandConstant('{userappdata}\Samplar'));
    CreateDir(ExpandConstant('{userappdata}\Samplar\logs'));
    CreateDir(ExpandConstant('{userappdata}\Samplar\models'));
    CreateDir(ExpandConstant('{userappdata}\Samplar\models\huggingface'));
    Log('Created AppData directories for logs and models');

    // Copy prepacked model files to AppData (audio-separator needs them
    // in a writable location when running from Program Files)
    CopyFile(
      ExpandConstant('{app}\models\htdemucs_ft.yaml'),
      ExpandConstant('{userappdata}\Samplar\models\htdemucs_ft.yaml'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\f7e0c4bc-ba3fe64a.th'),
      ExpandConstant('{userappdata}\Samplar\models\f7e0c4bc-ba3fe64a.th'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\d12395a8-e57c48e6.th'),
      ExpandConstant('{userappdata}\Samplar\models\d12395a8-e57c48e6.th'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\92cfc3b6-ef3bcb9c.th'),
      ExpandConstant('{userappdata}\Samplar\models\92cfc3b6-ef3bcb9c.th'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\04573f0d-f3cf25b2.th'),
      ExpandConstant('{userappdata}\Samplar\models\04573f0d-f3cf25b2.th'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\download_checks.json'),
      ExpandConstant('{userappdata}\Samplar\models\download_checks.json'),
      False);
    // 6-stem model (htdemucs_6s) — required by S-Mode 6-stem separation.
    // Previously missing from AppData, causing "model not found" for users
    // whose FullSongService code path pointed at AppData instead of {app}\models.
    CopyFile(
      ExpandConstant('{app}\models\htdemucs_6s.yaml'),
      ExpandConstant('{userappdata}\Samplar\models\htdemucs_6s.yaml'),
      False);
    CopyFile(
      ExpandConstant('{app}\models\5c90dfd2-34c22ccb.th'),
      ExpandConstant('{userappdata}\Samplar\models\5c90dfd2-34c22ccb.th'),
      False);
    Log('Copied prepacked model files to AppData\Samplar\models (incl. htdemucs_ft + htdemucs_6s)');

    // Copy pattern data files to AppData (VST3 plugin reads from UserData)
    CreateDir(ExpandConstant('{userappdata}\Samplar\data'));
    CopyFile(
      ExpandConstant('{app}\data\patterns_with_rolls.json'),
      ExpandConstant('{userappdata}\Samplar\data\patterns_with_rolls.json'),
      False);
    CopyFile(
      ExpandConstant('{app}\data\latin_patterns.json'),
      ExpandConstant('{userappdata}\Samplar\data\latin_patterns.json'),
      False);
    Log('Copied pattern databases to AppData\Samplar\data for VST3 access');

    // Copy BACKGROUNDS to AppData (VST3 plugin reads from UserData)
    CreateDir(ExpandConstant('{userappdata}\Samplar\BACKGROUNDS'));
    CopyBackgroundsToAppData();
    // Copy Samples to AppData (VST3 plugin reads from UserData for 808 preview kit)
    CreateDir(ExpandConstant('{userappdata}\Samplar\Samples'));
    CopySamplesToAppData();
    // Also copy REF.png to AppData for VST3
    CopyFile(
      ExpandConstant('{app}\REF.png'),
      ExpandConstant('{userappdata}\Samplar\REF.png'),
      False);
    // Copy SAMPLAR_portable.json to AppData (VST3 needs it for portable Python detection)
    CopyFile(
      ExpandConstant('{app}\SAMPLAR_portable.json'),
      ExpandConstant('{userappdata}\Samplar\SAMPLAR_portable.json'),
      False);
    Log('Copied BACKGROUNDS, REF.png, and SAMPLAR_portable.json to AppData\Samplar for VST3 access');

    // Also keep the Program Files logs dir for backward compat / portable hybrid
    CreateDir(ExpandConstant('{app}\logs'));

    // Log VC++ installation status
    if VCRedistNeedsInstall then
    begin
      Log('WARNING: VC++ Redistributable may not have installed correctly');
    end
    else
    begin
      Log('VC++ Redistributable is installed');
    end;
  end;
end;

// Uninstall confirmation
function InitializeUninstall: Boolean;
var
  Response: Integer;
begin
  Response := MsgBox('Are you sure you want to uninstall SAMPLAR?' + #13#10 +
                     'Your license and settings will be preserved.', 
                     mbConfirmation, MB_YESNO);
  Result := (Response = IDYES);
end;

// Custom uninstall page
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if MsgBox('Do you want to remove all user data and settings?' + #13#10 +
              '(This will delete your license and preferences)', 
              mbConfirmation, MB_YESNO) = IDYES then
    begin
      // Remove user data
      DelTree(ExpandConstant('{userappdata}\{#MyAppPublisher}\{#MyAppName}'), True, True, True);
      
      // Remove registry settings
      RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\{#MyAppPublisher}\{#MyAppName}');
    end;
  end;
end;

