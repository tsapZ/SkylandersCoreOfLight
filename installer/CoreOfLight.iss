; The installer of the launcher itself (#17, step 1): coreoflight.exe with its runtime DLLs into
; %LOCALAPPDATA%\Programs\Core of Light, a Start menu entry, uninstall through Windows. The six
; games' setups go along into {app}\games, where the launcher looks for them first (FindSetup);
; they install the games themselves when the first-run setup or a card asks for it.
;
; Built by tools/package_installer.ps1, which stages the files and passes the version from
; CMakeLists.txt (/DMyAppVersion=...).

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppName "Core of Light"
#define MyAppExeName "coreoflight.exe"

[Setup]
; Its own AppId, unlike any game's: Windows keeps one uninstall entry per AppId.
AppId={{1FA9E74A-00AA-4AFF-9258-23027F2EB131}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=CoreOfLightSetup-{#MyAppVersion}
SetupIconFile=..\src\coreoflight.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; The launcher may be running when an update comes: Inno asks to close it.
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "disclaimer.txt"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"; InfoBeforeFile: "disclaimer_de.txt"

[Files]
Source: "staging\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; The Visual C++ runtime next to the exe (copied from the maintainer's Visual Studio install by
; the packaging script, redistributable under Microsoft's VC++ Redistributable license).
Source: "staging\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "staging\games\*.exe"; DestDir: "{app}\games"; Flags: ignoreversion skipifsourcedoesntexist

[InstallDelete]
; The setups of an older version: the new ones replace them (FindSetup takes the highest version
; anyway, this only keeps the folder from growing).
Type: files; Name: "{app}\games\*Setup-*.exe"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}\games"
