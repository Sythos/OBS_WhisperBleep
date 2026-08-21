; SPDX-License-Identifier: MIT
; SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!include LogicLib.nsh

!ifndef STAGE_DIR
  !error "STAGE_DIR must point to an already prepared staging directory."
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE must name the unsigned installer to create."
!endif
!ifndef PRODUCT_VERSION
  !error "PRODUCT_VERSION must contain the package version."
!endif
!ifndef UNINSTALL_MANIFEST
  !error "UNINSTALL_MANIFEST must list the staged files to remove."
!endif

!define PRODUCT_NAME "OBS-WhisperBleep"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS-WhisperBleep"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION} (Unsigned)"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\obs-studio"

Section "Install ${PRODUCT_NAME}"
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"

  WriteUninstaller "$INSTDIR\uninstall-obs-whisperbleep.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME} ${PRODUCT_VERSION} (Unsigned)"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Sythos"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\uninstall-obs-whisperbleep.exe$\""
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Install Python and Whisper runtime (downloads)" SecRuntime
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\runtime\install-runtime.ps1" -NonInteractive'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION|MB_OK \
      "The OBS plugin was installed, but the optional Python/Whisper runtime could not be installed. Run $INSTDIR\runtime\install-runtime.ps1 after fixing network access."
  ${Else}
    MessageBox MB_ICONINFORMATION|MB_OK \
      "The CPU Whisper runtime was installed. Restart OBS Studio before using the plugin."
  ${EndIf}
SectionEnd

Section "Uninstall"
  !include "${UNINSTALL_MANIFEST}"
  Delete "$INSTDIR\uninstall-obs-whisperbleep.exe"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
