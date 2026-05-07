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
#define MyAppVersion "2.0.0"
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
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes

; Output settings
OutputDir=installer_output
OutputBaseFilename=samplar_Setup_v{#MyAppVersion}
SetupIconFile=samplar.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

; Compression
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4

; Architecture
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

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
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executable
Source: "Samplar_Portable\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Required DLLs
Source: "Samplar_Portable\libopenblas.dll"; DestDir: "{app}"; Flags: ignoreversion

; LibTorch DLLs (CRITICAL - required for AI drum separation / LARS)
Source: "Samplar_Portable\c10.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\torch.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\torch_cpu.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\torch_global_deps.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\fbgemm.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\fbjni.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\asmjit.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\pytorch_jni.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\libiomp5md.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\libiompstubs5md.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Samplar_Portable\uv.dll"; DestDir: "{app}"; Flags: ignoreversion

; Demucs C++ CLI executable (CRITICAL - required for audio separation)
Source: "Samplar_Portable\demucs_mt.cpp.main.exe"; DestDir: "{app}"; Flags: ignoreversion

; Python bridge (deprecated but kept for compatibility)
Source: "Samplar_Portable\python_bridge\*"; DestDir: "{app}\python_bridge"; Flags: ignoreversion recursesubdirs createallsubdirs

; Demucs models (large files - required for vocal separation)
Source: "Samplar_Portable\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs

; Logo/branding assets
Source: "Samplar_Portable\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

; VC++ 2022 Redistributable (x64) - extract to temp for installation
Source: "installer_dependencies\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; Game assets
Source: "Samplar_Portable\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

; Pattern databases (rhythmic patterns for step sequencer)
Source: "Samplar_Portable\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

; Documentation
Source: "Samplar_Portable\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

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

[UninstallDelete]
; Clean up log files and cache on uninstall
Type: files; Name: "{app}\logs\*.log"
Type: files; Name: "{app}\*.log"
Type: dirifempty; Name: "{app}\logs"

[Code]
// Custom installation logic

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
    // Create logs directory
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

