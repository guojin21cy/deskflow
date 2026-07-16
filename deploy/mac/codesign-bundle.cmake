# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# CPack pre-build hook: runs after every install rule has populated the
# staging area and right before the DMG is created, so this signature seals
# the final bundle contents (signing any earlier gets invalidated by files
# installed afterwards, e.g. the license files in Contents/Resources).

file(GLOB _bundles "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*/*.app" "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*.app")
foreach(_bundle ${_bundles})
  message(STATUS "codesigning ${_bundle}")
  execute_process(
    COMMAND codesign --force --deep -s - "${_bundle}"
    RESULT_VARIABLE _signResult
  )
  if(NOT _signResult EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${_bundle}")
  endif()
endforeach()
