Unicode True
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "1.0"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "dist\KTGameBooster-v${APP_VERSION}-Setup.exe"
!endif
!ifndef APP_FILE_VERSION
  !define APP_FILE_VERSION "1.0.0.0"
!endif

!define APP_NAME "KT Game Booster"
!define APP_EXE "KTGameBooster.exe"
!define COMPANY "KT TWEAKS"
!define APP_URL "https://github.com/KT-TWEAKS/kt-game-booster"
!define APP_KEY "Software\KT TWEAKS\KT Game Booster"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\KTGameBooster"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\KT Game Booster"
InstallDirRegKey HKLM "${APP_KEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 64
BrandingText "KT TWEAKS · Enhance. Build. Optimize."
Icon "assets\kt-tweaks.ico"
UninstallIcon "assets\kt-tweaks.ico"
VIProductVersion "${APP_FILE_VERSION}"
VIAddVersionKey /LANG=1046 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1046 "CompanyName" "${COMPANY}"
VIAddVersionKey /LANG=1046 "FileDescription" "Instalador do otimizador de jogos KT TWEAKS"
VIAddVersionKey /LANG=1046 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1046 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1046 "LegalCopyright" "kelvenapk · KT TWEAKS"

!define MUI_ABORTWARNING
!define MUI_ICON "assets\kt-tweaks.ico"
!define MUI_UNICON "assets\kt-tweaks.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir o KT Game Booster"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "O KT Game Booster requer Windows 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
  FindWindow $0 "" "KT Game Booster"
  StrCmp $0 0 done
    MessageBox MB_ICONEXCLAMATION|MB_OK "Feche o KT Game Booster pelo ícone ao lado do relógio antes de instalar ou atualizar."
    Abort
  done:
FunctionEnd

Section "KT Game Booster (obrigatório)" Core
  SectionIn RO
  SetShellVarContext all
  SetRegView 64
  SetOutPath "$INSTDIR"
  SetOverwrite try

  File /r /x gamemode.toml /x profiles "..\release\*.*"
  IfFileExists "$INSTDIR\gamemode.toml" config_done 0
    File /oname=gamemode.toml "..\release\gamemode.toml"
  config_done:
  CreateDirectory "$INSTDIR\profiles"
  IfFileExists "$INSTDIR\profiles\bg_only.toml" bg_done 0
    File /oname=profiles\bg_only.toml "..\release\profiles\bg_only.toml"
  bg_done:
  IfFileExists "$INSTDIR\profiles\tournament.toml" tournament_done 0
    File /oname=profiles\tournament.toml "..\release\profiles\tournament.toml"
  tournament_done:
  IfFileExists "$INSTDIR\profiles\streaming.toml" streaming_done 0
    File /oname=profiles\streaming.toml "..\release\profiles\streaming.toml"
  streaming_done:

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${APP_URL}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\KT TWEAKS"
  CreateShortcut "$SMPROGRAMS\KT TWEAKS\KT Game Booster.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\KT TWEAKS\Desinstalar KT Game Booster.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Atalho na área de trabalho" DesktopShortcut
  SetShellVarContext all
  CreateShortcut "$DESKTOP\KT Game Booster.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  Delete "$DESKTOP\KT Game Booster.lnk"
  RMDir /r "$SMPROGRAMS\KT TWEAKS"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\KTGameBooster.exe.config"
  Delete "$INSTDIR\ktengine.dll"
  Delete "$INSTDIR\ktgam.ico"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\Assets"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "${APP_KEY}"
  MessageBox MB_ICONQUESTION|MB_YESNO "Remover também suas configurações e perfis personalizados?" IDNO keep_data
    Delete "$INSTDIR\gamemode.toml"
    RMDir /r "$INSTDIR\profiles"
  keep_data:
  RMDir "$INSTDIR"
SectionEnd
