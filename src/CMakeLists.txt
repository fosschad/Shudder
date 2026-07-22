set(SHUDDER_CONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/shudder_config.h")
configure_file(app/shudder_config.h.in "${SHUDDER_CONFIG_HEADER}" @ONLY)

set(SHUDDER_SOURCES
  app/main.cpp
  app/AppController.cpp
  core/XdgPaths.cpp
  core/UrlUtils.cpp
  storage/PreferencesService.cpp
  storage/SecretService.cpp
  twitch/TwitchAuthService.cpp
  twitch/TwitchDirectoryModel.cpp
  twitch/TwitchWebsiteSession.cpp
  twitch/TwitchWebsiteSession.h
  chat/IrcParser.cpp
  chat/ChatModel.cpp
  playback/PlayerController.cpp
  playback/StreamlinkResolver.cpp
  playback/MpvVideoItem.cpp
  web/PlayerHostServer.cpp
)

qt_add_executable(shudder ${SHUDDER_SOURCES})

set(SHUDDER_QML_FILES
  ${CMAKE_SOURCE_DIR}/qml/Main.qml
  ${CMAKE_SOURCE_DIR}/qml/components/AvatarImage.qml
  ${CMAKE_SOURCE_DIR}/qml/components/GlassButton.qml
  ${CMAKE_SOURCE_DIR}/qml/components/IconButton.qml
  ${CMAKE_SOURCE_DIR}/qml/components/MonoSlider.qml
  ${CMAKE_SOURCE_DIR}/qml/components/SideChannelRow.qml
  ${CMAKE_SOURCE_DIR}/qml/components/StableImage.qml
  ${CMAKE_SOURCE_DIR}/qml/components/StreamCard.qml
  ${CMAKE_SOURCE_DIR}/qml/components/Theme.qml
  ${CMAKE_SOURCE_DIR}/qml/dialogs/AuthDialog.qml
  ${CMAKE_SOURCE_DIR}/qml/chat/ChatPanel.qml
  ${CMAKE_SOURCE_DIR}/qml/dialogs/AboutDialog.qml
  ${CMAKE_SOURCE_DIR}/qml/dialogs/SettingsSheet.qml
  ${CMAKE_SOURCE_DIR}/qml/player/NativePlayer.qml
  ${CMAKE_SOURCE_DIR}/qml/player/PlayerControls.qml
  ${CMAKE_SOURCE_DIR}/qml/player/StandardPlayer.qml
)

set(SHUDDER_RESOURCE_FILES
  ${CMAKE_SOURCE_DIR}/assets/icons/hicolor/scalable/apps/shudder.svg
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/AvatarImage.qml PROPERTIES QT_RESOURCE_ALIAS components/AvatarImage.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/GlassButton.qml PROPERTIES QT_RESOURCE_ALIAS components/GlassButton.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/IconButton.qml PROPERTIES QT_RESOURCE_ALIAS components/IconButton.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/MonoSlider.qml PROPERTIES QT_RESOURCE_ALIAS components/MonoSlider.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/SideChannelRow.qml PROPERTIES QT_RESOURCE_ALIAS components/SideChannelRow.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/StableImage.qml PROPERTIES QT_RESOURCE_ALIAS components/StableImage.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/StreamCard.qml PROPERTIES QT_RESOURCE_ALIAS components/StreamCard.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/components/Theme.qml PROPERTIES QT_RESOURCE_ALIAS components/Theme.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/dialogs/AuthDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/AuthDialog.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/chat/ChatPanel.qml PROPERTIES QT_RESOURCE_ALIAS chat/ChatPanel.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/dialogs/AboutDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/AboutDialog.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/dialogs/SettingsSheet.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/SettingsSheet.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/player/NativePlayer.qml PROPERTIES QT_RESOURCE_ALIAS player/NativePlayer.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/player/PlayerControls.qml PROPERTIES QT_RESOURCE_ALIAS player/PlayerControls.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/player/StandardPlayer.qml PROPERTIES QT_RESOURCE_ALIAS player/StandardPlayer.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/icons/hicolor/scalable/apps/shudder.svg PROPERTIES QT_RESOURCE_ALIAS icons/shudder.svg)

qt_add_qml_module(shudder
  URI Shudder
  VERSION 1.0
  RESOURCE_PREFIX /shudder
  QML_FILES ${SHUDDER_QML_FILES}
  RESOURCES ${SHUDDER_RESOURCE_FILES}
)

target_include_directories(shudder PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(shudder PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Network
  Qt6::WebSockets
  Qt6::WebEngineCore
  Qt6::WebEngineQuick
  PkgConfig::MPV
)

if(LIBSECRET_FOUND)
  target_link_libraries(shudder PRIVATE PkgConfig::LIBSECRET)
  target_compile_definitions(shudder PRIVATE SHUDDER_WITH_LIBSECRET=1)
endif()

if(SHUDDER_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(shudder PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(shudder PRIVATE -fsanitize=address,undefined)
endif()

set_target_properties(shudder PROPERTIES
  OUTPUT_NAME shudder
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src"
)

install(TARGETS shudder RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES ${CMAKE_SOURCE_DIR}/packaging/appimage/AppRun DESTINATION "${CMAKE_INSTALL_DATADIR}/shudder/appimage")
install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/CHANGELOG.md ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
