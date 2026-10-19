; Purrlude's Windows installer (Inno Setup 6). Made by make_windows_installer.py, which passes:
;   AppVersion   the version from CMakeLists.txt
;   SourceDir    the Release build folder (Purrlude.exe and what it finds next to itself)
;   WorkDir      the wizard images, the icon and the Visual C++ runtime installer
;   OutputDir    where the installer goes
;   RedistMajor, RedistMinor, RedistBuild   the bundled Visual C++ runtime's version

#define AppName "Purrlude"
#define RepoRoot AddBackslash(SourcePath) + "..\.."

[Setup]
; Never change AppId: it's how upgrades and the uninstaller find an installed Purrlude.
AppId={{0834E1B0-7553-4DCD-BFD4-79B8D3104AE9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Clint Christopher Canada
AppCopyright=Copyright (C) 2026 Clint Christopher Canada
AppPublisherURL=https://github.com/clintcan/Purrlude
AppSupportURL=https://github.com/clintcan/Purrlude/issues
AppUpdatesURL=https://github.com/clintcan/Purrlude/releases
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#RepoRoot}\LICENSE
InfoBeforeFile={#RepoRoot}\NOTICE
OutputDir={#OutputDir}
OutputBaseFilename=Purrlude-{#AppVersion}-Windows-x64-Setup
SetupIconFile={#WorkDir}\purrlude.ico
UninstallDisplayIcon={app}\Purrlude.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile={#WorkDir}\wizard-164.bmp,{#WorkDir}\wizard-246.bmp,{#WorkDir}\wizard-328.bmp
WizardSmallImageFile={#WorkDir}\small-55.bmp,{#WorkDir}\small-83.bmp,{#WorkDir}\small-110.bmp
WizardImageStretch=no
WizardImageBackColor=$180A04
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1903 is the first with DirectML in System32.
MinVersion=10.0.18362
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The separation model isn't included: the app downloads it when it's first needed.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "models,*.running-old.exe,*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepoRoot}\NOTICE"; DestDir: "{app}"; DestName: "NOTICE.txt"; Flags: ignoreversion
; ONNX Runtime needs the Visual C++ runtime; it's installed only when missing or older.
Source: "{#WorkDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsVCRedist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Purrlude.exe"
Name: "{group}\{#AppName} User Guide"; Filename: "https://github.com/clintcan/Purrlude/blob/main/docs/user-guide/README.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Purrlude.exe"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: NeedsVCRedist
Filename: "{app}\Purrlude.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// True when the installed Visual C++ 2015-2022 x64 runtime is older than the bundled one.
function NeedsVCRedist: Boolean;
var
  Key: String;
  Major, Minor, Build: Cardinal;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not (RegQueryDWordValue(HKLM64, Key, 'Installed', Major) and (Major = 1)) then
  begin
    Result := True;
    exit;
  end;
  RegQueryDWordValue(HKLM64, Key, 'Major', Major);
  RegQueryDWordValue(HKLM64, Key, 'Minor', Minor);
  RegQueryDWordValue(HKLM64, Key, 'Bld', Build);
  if Major <> {#RedistMajor} then
    Result := Major < {#RedistMajor}
  else if Minor <> {#RedistMinor} then
    Result := Minor < {#RedistMinor}
  else
    Result := Build < {#RedistBuild};
end;
