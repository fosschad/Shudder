cmake_minimum_required(VERSION 3.24)

project(Shudder
  VERSION 0.1.0
  DESCRIPTION "Native Linux Twitch desktop client"
  HOMEPAGE_URL "https://github.com/fosschad/Shudder"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SHUDDER_GITHUB_OWNER "" CACHE STRING "GitHub owner for Shudder package metadata")
set(SHUDDER_GITHUB_REPOSITORY "Shudder" CACHE STRING "GitHub repository name for Shudder package metadata")
set(SHUDDER_TWITCH_CLIENT_ID "6kt5h1zfzmdv5vre9ios9qfr3lobmq" CACHE STRING "Public Twitch Device Code client ID for Shudder builds")
if(SHUDDER_TWITCH_CLIENT_ID STREQUAL "")
  set(SHUDDER_TWITCH_CLIENT_ID "6kt5h1zfzmdv5vre9ios9qfr3lobmq" CACHE STRING "Public Twitch Device Code client ID for Shudder builds" FORCE)
endif()

if(NOT SHUDDER_GITHUB_OWNER)
  execute_process(
    COMMAND git config --get remote.origin.url
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _shudder_origin_url
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_shudder_origin_url MATCHES "github.com[:/]([^/]+)/([^/.]+)(\\.git)?$")
    set(SHUDDER_GITHUB_OWNER "${CMAKE_MATCH_1}" CACHE STRING "GitHub owner for Shudder package metadata" FORCE)
    if(NOT DEFINED CACHE{SHUDDER_GITHUB_REPOSITORY})
      set(SHUDDER_GITHUB_REPOSITORY "${CMAKE_MATCH_2}" CACHE STRING "GitHub repository name for Shudder package metadata" FORCE)
    endif()
  endif()
endif()

if(NOT SHUDDER_GITHUB_OWNER)
  message(FATAL_ERROR "Set -DSHUDDER_GITHUB_OWNER=<actual fork owner>. Production package metadata must not contain placeholders.")
endif()

string(REGEX REPLACE "[^A-Za-z0-9_]" "_" SHUDDER_GITHUB_OWNER_APP_ID_SEGMENT "${SHUDDER_GITHUB_OWNER}")
set(SHUDDER_APP_ID "io.github.${SHUDDER_GITHUB_OWNER_APP_ID_SEGMENT}.Shudder" CACHE STRING "Canonical reverse-DNS Shudder application ID")

if(NOT SHUDDER_APP_ID MATCHES "^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+$")
  message(FATAL_ERROR "SHUDDER_APP_ID is not a valid D-Bus/application ID: ${SHUDDER_APP_ID}")
endif()

include(GNUInstallDirs)
include(CTest)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network WebSockets WebEngineCore WebEngineQuick Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/${SHUDDER_APP_ID}.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/${SHUDDER_APP_ID}.metainfo.xml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
install(FILES "assets/icons/hicolor/scalable/apps/shudder.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
  RENAME "${SHUDDER_APP_ID}.svg")
foreach(_size 16 24 32 48 64 128 256 512)
  install(FILES "assets/icons/hicolor/${_size}x${_size}/apps/shudder.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps"
    RENAME "${SHUDDER_APP_ID}.svg")
endforeach()

configure_file(packaging/linux/shudder.desktop.in "${CMAKE_CURRENT_BINARY_DIR}/packaging/${SHUDDER_APP_ID}.desktop" @ONLY)
configure_file(packaging/linux/shudder.metainfo.xml.in "${CMAKE_CURRENT_BINARY_DIR}/packaging/${SHUDDER_APP_ID}.metainfo.xml" @ONLY)
configure_file(packaging/flatpak/io.github.Shudder.yml.in "${CMAKE_CURRENT_BINARY_DIR}/packaging/${SHUDDER_APP_ID}.yml" @ONLY)

set(CPACK_PACKAGE_NAME "shudder")
set(CPACK_PACKAGE_VENDOR "${SHUDDER_GITHUB_OWNER}")
set(CPACK_PACKAGE_CONTACT "https://github.com/${SHUDDER_GITHUB_OWNER}/${SHUDDER_GITHUB_REPOSITORY}/issues")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Linux Twitch desktop client")
set(CPACK_PACKAGE_DESCRIPTION "Shudder is an independent GPL-3.0-or-later native Linux Twitch desktop client. It uses a Qt Quick monochrome interface, Twitch Device Code sign-in, native Twitch chat, Qt WebEngine fallback playback, Streamlink URL resolution, libmpv Native playback, and Secret Service credential storage.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/${SHUDDER_GITHUB_OWNER}/${SHUDDER_GITHUB_REPOSITORY}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "shudder_${PROJECT_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6webenginequick6, libqt6websockets6, libmpv2, streamlink, libsecret-1-0")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase, qt6-qtdeclarative, qt6-qtwebengine, qt6-qtwebsockets, mpv-libs, streamlink, libsecret")
set(CPACK_RPM_PACKAGE_DESCRIPTION "Shudder is an independent GPL-3.0-or-later native Linux Twitch desktop client. It uses a Qt Quick monochrome interface, Twitch Device Code sign-in, native Twitch chat, Qt WebEngine fallback playback, Streamlink URL resolution, libmpv Native playback, and Secret Service credential storage.")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
include(CPack)
