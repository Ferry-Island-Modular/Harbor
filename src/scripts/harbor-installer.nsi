Unicode True
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
!define APP_REG_KEY "Software\Ferry Island Modular\Harbor"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Harbor"

; MultiUser gives the installer both a per-user and an all-users mode. It sets
; the execution level itself, so RequestExecutionLevel must NOT appear here.
; Every MULTIUSER_ define must precede the include.
;
; Previously this script was per-user only (RequestExecutionLevel user) while
; the directory page still let people browse to Program Files, where the file
; copy then failed with write errors.
;
; Note the UAC tradeoff of MULTIUSER_EXECUTIONLEVEL Highest: it compiles to
; RequestExecutionLevel highest, so on an administrator account Windows
; elevates at launch, before the mode is chosen — an admin sees a UAC prompt
; even when installing just for themselves. Standard accounts are unaffected
; and simply do not get the all-users option. Stock MultiUser.nsh cannot
; elevate on demand (a mixed-mode install requires Admin, Power or Highest);
; that needs the third-party UAC plugin, or shipping two separate installers.
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_INSTDIR "${APP_NAME}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "${APP_REG_KEY}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallDir"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "${APP_REG_KEY}"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!include "MultiUser.nsh"

Name "${APP_NAME}"
Caption "${APP_NAME} ${APP_VERSION} Setup"
OutFile "${OUTPUT_DIR}\Harbor-${APP_VERSION}-windows-x64-setup.exe"
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
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\Harbor.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; No install-dir override is needed. MULTIUSER_INSTALLMODE_INSTDIR makes
; per-user resolve to FOLDERID_UserProgramFiles (%LocalAppData%\Programs,
; with that path as an explicit fallback) and all-users to $PROGRAMFILES64 —
; so the per-user location is byte-for-byte what Harbor used before. The
; INSTDIR_REGISTRY defines additionally reuse the directory of an existing
; install, read from HKLM or HKCU to match the mode.

Function .onInit
    !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
    !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "Harbor (required)" SEC_HARBOR
    SectionIn RO
    ; MultiUser sets the shell-var context to match the chosen mode, so
    ; shortcuts land in the all-users or per-user Start Menu accordingly.
    ; SHCTX resolves to HKLM or HKCU the same way.
    SetOutPath "$INSTDIR"
    File /r "${STAGE_DIR}\*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Ferry Island Modular"
    CreateShortcut "$SMPROGRAMS\Ferry Island Modular\Harbor.lnk" "$INSTDIR\Harbor.exe"

    WriteRegStr SHCTX "${APP_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr SHCTX "${APP_REG_KEY}" "InstallMode" "$MultiUser.InstallMode"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Harbor.exe"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SEC_DESKTOP
    CreateShortcut "$DESKTOP\Harbor.lnk" "$INSTDIR\Harbor.exe"
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\Harbor.lnk"
    Delete "$SMPROGRAMS\Ferry Island Modular\Harbor.lnk"
    RMDir "$SMPROGRAMS\Ferry Island Modular"
    DeleteRegKey SHCTX "${UNINSTALL_KEY}"
    DeleteRegKey SHCTX "${APP_REG_KEY}"
    RMDir /r "$INSTDIR"
SectionEnd
