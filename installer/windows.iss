; Inno Setup script — builds the Windows installer for the plugin.
; Installs into %ProgramData%\obs-studio\plugins\<name>\ (the modern OBS plugin
; layout, OBS 30+), which needs no admin rights and survives OBS updates.
; Defines passed by CI: PluginName, PluginVersion, SourceDir (unzipped package root).
#ifndef PluginName
  #define PluginName "globe-overlay-for-obs"
#endif
#ifndef PluginVersion
  #define PluginVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release\globe-overlay-for-obs"
#endif

[Setup]
AppId={{E6280963-7A01-48DC-B997-E3A29DE4B081}
AppName=Globe Overlay for OBS
AppVersion={#PluginVersion}
AppPublisher=Vince Olshove
AppPublisherURL=https://github.com/dfigravity/globe-overlay-for-obs
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\release
OutputBaseFilename={#PluginName}-{#PluginVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=Globe Overlay for OBS
WizardStyle=modern

[Files]
Source: "{#SourceDir}\bin\64bit\*"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion recursesubdirs
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs

[Messages]
FinishedLabel=Globe Overlay for OBS is installed.%n%nStart OBS Studio, then add the "Globe Overlay (check-in globe)" source, pick a look and paste your Triode check-in link.

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if CheckForMutexes('OBSStudioCore') then
  begin
    MsgBox('Please quit OBS Studio before installing.', mbError, MB_OK);
    Result := False;
  end;
end;
