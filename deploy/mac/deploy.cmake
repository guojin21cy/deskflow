# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(OS_STRING "macos-${BUILD_ARCHITECTURE}")

# macdeployqt already strips the bundle; CPack stripping would run after our
# final codesign and invalidate the bundle signature (SIGKILL at launch).
set(CPACK_STRIP_FILES FALSE)

if (OSX_BUNDLE)
  # Qt installs (e.g. Homebrew) may split modules across directories; the
  # directory next to macdeployqt aggregates them all for -libpath.
  get_filename_component(DEPLOYQT_BIN_DIR ${DEPLOYQT} DIRECTORY)
  get_filename_component(DEPLOYQT_LIB_DIR ${DEPLOYQT_BIN_DIR}/../lib ABSOLUTE)

  # Deploy Qt into the bundle. deskflow-core must be listed explicitly or its
  # Qt references keep pointing at the build machine's Qt install and the core
  # process aborts at startup with two sets of Qt binaries loaded.
  install(CODE "execute_process(COMMAND
    ${DEPLOYQT}
    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
    \"-executable=\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app/Contents/MacOS/deskflow-core\"
    \"-libpath=${DEPLOYQT_LIB_DIR}\"
    -timestamp -no-codesign
  )")
  # The virtual keyboard input-context plugin drags in QtVirtualKeyboard/QtQml/
  # QtQuick, which macdeployqt fails to resolve (and nothing in the app uses).
  # Remove the plugin and its orphaned frameworks, then (re)sign the bundle.
  install(CODE "
    set(bundleContents \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app/Contents\")
    file(REMOVE \"\${bundleContents}/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib\")
    file(REMOVE \"\${bundleContents}/PlugIns/imageformats/libqpdf.dylib\")
    foreach(orphan QtVirtualKeyboard QtVirtualKeyboardQml QtVirtualKeyboardSettings QtPdf QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQuick)
      file(REMOVE_RECURSE \"\${bundleContents}/Frameworks/\${orphan}.framework\")
    endforeach()
    execute_process(COMMAND sh \"${MY_DIR}/bundle-qtsvg.sh\"
      \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
      \"${DEPLOYQT_LIB_DIR}\"
    )
  ")
  # Final ad-hoc signature: must run after ALL install rules (CPack pre-build)
  # or later-installed files (e.g. bundled licenses) break the resource seal.
  set(CPACK_PRE_BUILD_SCRIPTS ${MY_DIR}/codesign-bundle.cmake)
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${MY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")
endif()
