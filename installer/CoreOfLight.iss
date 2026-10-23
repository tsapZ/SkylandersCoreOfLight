; The installer of the launcher itself (#17): coreoflight.exe with its runtime DLLs into
; %LOCALAPPDATA%\Programs\Core of Light, a Start menu entry, uninstall through Windows. The games'
; setups come from their GitHub Releases (the launcher downloads them, updates.h); a bundle for a
; PC without internet takes them along into {app}\games (package_installer.ps1 -Games), where the
; launcher looks first.
;
; Uninstalling asks what goes along: the games (and their discs), the saves, the figures, the
; launcher's pictures, HD textures, backups and settings. Nothing of that by default.
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

[CustomMessages]
english.UninstallTitle=Uninstall Core of Light
english.UninstallIntro=Core of Light itself is removed. What should go along with it?
english.UninstallGames=The games too (all six Recompiled games and the discs set up for them)
english.UninstallSaves=Saves, achievements and profiles
english.UninstallFigures=The figures (Documents\Skylanders\Figures: levels, gold, hats)
english.UninstallData=Pictures, HD textures, save backups and the launcher's settings
english.UninstallKeep=Whatever isn't ticked stays.
german.UninstallTitle=Core of Light deinstallieren
german.UninstallIntro=Core of Light selbst wird entfernt. Was soll mit weg?
german.UninstallGames=Auch die Spiele (alle sechs Recompiled-Spiele und ihre eingelesenen Discs)
german.UninstallSaves=Spielstände, Erfolge und Profile
german.UninstallFigures=Die Figuren (Dokumente\Skylanders\Figures: Level, Gold, Hüte)
german.UninstallData=Bilder, HD-Texturen, Sicherungen der Spielstände und die Hub-Einstellungen
german.UninstallKeep=Was nicht angehakt ist, bleibt.

[Files]
Source: "staging\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; The Visual C++ runtime next to the exe (copied from the maintainer's Visual Studio install by
; the packaging script, redistributable under Microsoft's VC++ Redistributable license).
Source: "staging\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; The fallback font where Windows' Segoe UI isn't there (Wine / Proton): Noto Sans, SIL Open Font
; License (fonts\OFL.txt).
Source: "staging\fonts\*"; DestDir: "{app}\fonts"; Flags: ignoreversion
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
; An update the launcher started (/SILENT /RELAUNCH=1): it quit for it, so it comes back.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: Relaunch

[UninstallDelete]
Type: dirifempty; Name: "{app}\games"
; The launcher's own caches: the releases it knew, the setups it downloaded, the exports' scratch.
Type: filesandordirs; Name: "{localappdata}\CoreOfLight"
Type: filesandordirs; Name: "{%TEMP}\CoreOfLight"

[Code]
function Relaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

// --- Uninstall: what goes along (the owner's wish, 2026-10-23) -----------------------------------

var
  RemoveGames, RemoveSaves, RemoveFigures, RemoveData: Boolean;

// The six games: their setups' AppIds (games.cpp kGames) and their user data folders in Documents.
function GameAppId(I: Integer): String;
begin
  case I of
    0: Result := '{2B356600-5297-425A-A646-8498A7AAC307}';
    1: Result := '{1F12B332-CD84-471A-BE9F-7F3BFA614A6D}';
    2: Result := '{C3E00C3F-6CE0-4100-BD9B-13F9B4A8CCBA}';
    3: Result := '{9A774956-7256-4E2C-A3D6-418903FC71B6}';
    4: Result := '{44F04CDA-C049-4558-AAA6-25A6E7932D71}';
  else
    Result := '{3D80E212-E302-4422-9B7B-6AFAF712400E}';
  end;
end;

function GameDataFolder(I: Integer): String;
begin
  case I of
    0: Result := 'ssarecomp';
    1: Result := 'giantsrecomp';
    2: Result := 'swapforcerecomp';
    3: Result := 'trapteamrecomp';
    4: Result := 'superchargersrecomp';
  else
    Result := 'imaginatorsrecomp';
  end;
end;

function AddBox(Form: TSetupForm; var Top: Integer; const Caption: String): TNewCheckBox;
begin
  Result := TNewCheckBox.Create(Form);
  Result.Parent := Form;
  Result.Left := ScaleX(20);
  Result.Top := Top;
  Result.Width := Form.ClientWidth - ScaleX(40);
  Result.Height := ScaleY(22);
  Result.Caption := Caption;
  Top := Top + ScaleY(30);
end;

// Asked first (not in a silent uninstall, which keeps everything): the choices, all off.
function InitializeUninstall: Boolean;
var
  Form: TSetupForm;
  Text: TNewStaticText;
  Games, Saves, Figures, Data: TNewCheckBox;
  OkButton, CancelButton: TNewButton;
  Top: Integer;
begin
  Result := True;
  if UninstallSilent then Exit;
  Form := CreateCustomForm(ScaleX(560), ScaleY(270), False, False);
  try
    Form.Caption := CustomMessage('UninstallTitle');
    Text := TNewStaticText.Create(Form);
    Text.Parent := Form;
    Text.Left := ScaleX(20);
    Text.Top := ScaleY(18);
    Text.Caption := CustomMessage('UninstallIntro');
    Top := ScaleY(54);
    Games := AddBox(Form, Top, CustomMessage('UninstallGames'));
    Saves := AddBox(Form, Top, CustomMessage('UninstallSaves'));
    Figures := AddBox(Form, Top, CustomMessage('UninstallFigures'));
    Data := AddBox(Form, Top, CustomMessage('UninstallData'));
    Text := TNewStaticText.Create(Form);
    Text.Parent := Form;
    Text.Left := ScaleX(20);
    Text.Top := Top + ScaleY(4);
    Text.Caption := CustomMessage('UninstallKeep');
    OkButton := TNewButton.Create(Form);
    OkButton.Parent := Form;
    OkButton.Caption := SetupMessage(msgButtonOK);
    OkButton.ModalResult := mrOk;
    OkButton.Default := True;
    OkButton.Width := ScaleX(100);
    OkButton.Height := ScaleY(26);
    OkButton.Top := Form.ClientHeight - ScaleY(42);
    OkButton.Left := Form.ClientWidth - ScaleX(230);
    CancelButton := TNewButton.Create(Form);
    CancelButton.Parent := Form;
    CancelButton.Caption := SetupMessage(msgButtonCancel);
    CancelButton.ModalResult := mrCancel;
    CancelButton.Cancel := True;
    CancelButton.Width := ScaleX(100);
    CancelButton.Height := ScaleY(26);
    CancelButton.Top := OkButton.Top;
    CancelButton.Left := Form.ClientWidth - ScaleX(120);
    if Form.ShowModal <> mrOk then begin
      Result := False;
      Exit;
    end;
    RemoveGames := Games.Checked;
    RemoveSaves := Saves.Checked;
    RemoveFigures := Figures.Checked;
    RemoveData := Data.Checked;
  finally
    Form.Free;
  end;
end;

// A game's own uninstaller, silently, then its folder with the disc it was given (its
// uninstaller keeps rom\ in a silent run). Inno's uninstaller hands over to a copy of itself, so
// the uninstall entry going away is what says it is done.
procedure UninstallGame(I: Integer);
var
  Key, Command, Folder: String;
  Code, Waited: Integer;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + GameAppId(I) + '_is1';
  if not RegQueryStringValue(HKCU, Key, 'UninstallString', Command) then Exit;
  RegQueryStringValue(HKCU, Key, 'InstallLocation', Folder);
  if Exec(RemoveQuotes(Command), '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Waited := 0;
    while RegKeyExists(HKCU, Key) and (Waited < 120) do begin
      Sleep(500);
      Waited := Waited + 1;
    end;
  end;
  if Folder <> '' then DelTree(RemoveBackslashUnlessRoot(Folder), True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  I: Integer;
  Docs, Skylanders: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  Docs := ExpandConstant('{userdocs}');
  Skylanders := Docs + '\Skylanders';
  if RemoveGames then
    for I := 0 to 5 do UninstallGame(I);
  if RemoveSaves then begin
    for I := 0 to 5 do DelTree(Docs + '\' + GameDataFolder(I), True, True, True);
    DelTree(Skylanders + '\Profiles', True, True, True);
  end;
  if RemoveFigures then DelTree(Skylanders + '\Figures', True, True, True);
  if RemoveData then begin
    DelTree(Skylanders + '\Pictures', True, True, True);
    DelTree(Skylanders + '\Textures', True, True, True);
    DelTree(Skylanders + '\Backups', True, True, True);
    DeleteFile(Skylanders + '\coreoflight.toml');
    DeleteFile(Skylanders + '\playtime.txt');
  end;
  RemoveDir(Skylanders);  // only when nothing is left in it
end;
