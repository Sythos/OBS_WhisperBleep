; SPDX-License-Identifier: MIT
; SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

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

Section "Uninstall"
  !include "${UNINSTALL_MANIFEST}"
  Delete "$INSTDIR\uninstall-obs-whisperbleep.exe"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
