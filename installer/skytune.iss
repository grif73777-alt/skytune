; skytune installer (Inno Setup 6). Built by GitHub Actions, see .github/workflows/build.yml
; Expects the plugin bundle at ..\dist\skytune.vst3

#define MyAppName      "skytune"
#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif
#define MyPublisher    "tripleskyluv corp"

[Setup]
AppId={{B7D2A5F1-3C94-4E0B-8A66-1F2D9C5E7A30}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyPublisher}
VersionInfoVersion={#MyAppVersion}
; the VST3 bundle goes to the standard system folder that every DAW scans
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateAppDir=yes
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\setup-out
OutputBaseFilename=skytune-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} (VST3)
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[InstallDelete]
; remove an older install of the same plugin before copying the new one
Type: filesandordirs; Name: "{commoncf64}\VST3\skytune.vst3"

[Files]
Source: "..\dist\skytune.vst3\*"; DestDir: "{commoncf64}\VST3\skytune.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=skytune has been installed to the VST3 folder.%n%nIn FL Studio: Options > Manage plugins > Find installed plugins, then add skytune to a mixer insert (on the vocal track).
