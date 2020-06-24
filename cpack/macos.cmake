set(CPACK_BUNDLE_NAME "X-Moto")
set(CPACK_BUNDLE_PLIST ${PROJECT_SOURCE_DIR}/macos/Info.plist)
set(CPACK_BUNDLE_ICON ${PROJECT_SOURCE_DIR}/macos/xmoto.icns)

set(CPACK_PACKAGE_ICON ${PROJECT_SOURCE_DIR}/macos/xmoto.icns)

set(CPACK_DMG_VOLUME_NAME "X-Moto-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

include(InstallRequiredSystemLibraries)

if(BUILD_MACOS_BUNDLE)
  set_target_properties(xmoto PROPERTIES
    # This changes the bundle name to "X-Moto.app", but it's
    # actually a side effect of changing the executable name.
    # As usual, it seems CMake doesn't provide even a half-reasonable way of doing it,
    # so we have to put up with its garbage and have the executable named differently too.
    OUTPUT_NAME "${CPACK_BUNDLE_NAME}"

    MACOSX_BUNDLE_INFO_PLIST
    "${PROJECT_SOURCE_DIR}/macos/Info.plist"
  )
  install(TARGETS xmoto
    RUNTIME DESTINATION . COMPONENT Runtime
    BUNDLE DESTINATION . COMPONENT Runtime
  )

  install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/${CPACK_BUNDLE_NAME}.app\"  \"\"  \"\")
    " COMPONENT Runtime)
endif()
