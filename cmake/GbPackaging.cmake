# .deb and .rpm packages of the installed executables (ADR-0010 §7), built with CPack:
#
#   cmake --preset release && cmake --build --preset release
#   cpack --config build/release/CPackConfig.cmake -G "DEB;RPM" -B build/release/packages
#
# The package name stays `gb-cplusplus` across versions, so dpkg and rpm treat a new build as an
# upgrade of the old one and delete files that the new package no longer contains.
# Building the RPM needs `rpmbuild` (the `rpm` package on Ubuntu).

set(CPACK_PACKAGE_NAME "gb-cplusplus")
set(CPACK_PACKAGE_VENDOR "Arturo R Montesinos")
set(CPACK_PACKAGE_CONTACT "Arturo R Montesinos <arturormk@gmail.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Inspector, player and video exporter for GoPro MAX 360 recordings")
set(CPACK_PACKAGE_DESCRIPTION
  "gb-player plays GoPro MAX .360 recordings with spherical viewing, synchronized telemetry and
a route map. gb-render exports reframed videos with overlays, and the gb-* laboratory tools
inspect containers, metadata, telemetry and frames.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/arturormk/gb-cplusplus")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Debian: library dependencies come from dpkg-shlibdeps; QML modules are loaded at run time, so
# they are listed by hand (the same set quickstart.sh installs for building).
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
find_program(GB_DPKG_SHLIBDEPS dpkg-shlibdeps)
if(GB_DPKG_SHLIBDEPS)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "qml6-module-qtqml, qml6-module-qtqml-models, qml6-module-qtqml-workerscript, \
qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, \
qml6-module-qtquick-nativestyle, qml6-module-qtquick-templates, qml6-module-qtquick-window")

# RPM: rpmbuild generates library requirements; QML modules come from qt6-qtdeclarative.
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative")

include(CPack)
