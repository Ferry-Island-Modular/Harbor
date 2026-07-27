Unicode True
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef APP_VERSION
    !error "APP_VERSION is required"
!endif
!ifndef APP_FILE_VERSION
    !error "APP_FILE_VERSION is required"
!endif
!ifndef STAGE_DIR
    !error "STAGE_DIR is required"
!endif
!ifndef OUTPUT_DIR
    !error "OUTPUT_DIR is required"
!endif
!ifndef ICON_PATH
    !error "ICON_PATH is required"
!endif

!define APP_NAME "Harbor"
!define APP_PUBLISHER "Ferry Island Modular"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Harbor"

Name "${APP_NAME}"
Caption "${APP_NAME} ${APP_VERSION} Setup"
OutFile "${OUTPUT_DIR}\Harbor-${APP_VERSION}-windows-x64-setup.exe"
InstallDir "$LocalAppData\Programs\Harbor"
InstallDirRegKey HKCU "Software\Ferry Island Modular\Harbor" "InstallDir"
BrandingText "Ferry Island Modular"
ShowInstDetails show
ShowUninstDetails show
CRCCheck force

VIProductVersion "${APP_FILE_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Ferry Island Modular"

!include "MUI2.nsh"
!define MUI_ABORTWARNING
!define MUI_ICON "${ICON_PATH}"
!define MUI_UNICON "${ICON_PATH}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\Harbor.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Harbor (required)" SEC_HARBOR
    SectionIn RO
    SetShellVarContext current
    SetOutPath "$INSTDIR"
    File /r "${STAGE_DIR}\*"

    IfFileExists "$INSTDIR\_prerequisites\vc_redist.x64.exe" 0 redist_done
    DetailPrint "Installing Microsoft Visual C++ Runtime..."
    ExecWait '"$INSTDIR\_prerequisites\vc_redist.x64.exe" /install /quiet /norestart'
    RMDir /r "$INSTDIR\_prerequisites"
redist_done:

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Ferry Island Modular"
    CreateShortcut "$SMPROGRAMS\Ferry Island Modular\Harbor.lnk" "$INSTDIR\Harbor.exe"

    WriteRegStr HKCU "Software\Ferry Island Modular\Harbor" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Harbor.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SEC_DESKTOP
    SetShellVarContext current
    CreateShortcut "$DESKTOP\Harbor.lnk" "$INSTDIR\Harbor.exe"
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    Delete "$DESKTOP\Harbor.lnk"
    Delete "$SMPROGRAMS\Ferry Island Modular\Harbor.lnk"
    RMDir "$SMPROGRAMS\Ferry Island Modular"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\Ferry Island Modular\Harbor"
    RMDir /r "$INSTDIR"
SectionEnd
