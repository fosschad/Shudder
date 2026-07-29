set(TEST_COMMON_SOURCES
  ../src/chat/IrcParser.cpp
  ../src/core/ExternalProcessEnvironment.cpp
  ../src/core/UrlUtils.cpp
  ../src/core/XdgPaths.cpp
  ../src/storage/PreferencesService.cpp
  ../src/playback/StreamlinkResolver.cpp
  ../src/web/PlayerHostServer.cpp
)

add_executable(shudder_core_tests
  tst_core.cpp
  ${TEST_COMMON_SOURCES}
)

target_include_directories(shudder_core_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_compile_definitions(shudder_core_tests PRIVATE
  SHUDDER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  SHUDDER_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

target_link_libraries(shudder_core_tests PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_core_tests COMMAND shudder_core_tests)

add_executable(shudder_chat_jump_tests
  tst_chat_jump.cpp
)

target_compile_definitions(shudder_chat_jump_tests PRIVATE
  SHUDDER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(shudder_chat_jump_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_chat_jump_tests COMMAND shudder_chat_jump_tests)

add_executable(shudder_chat_model_tests
  tst_chat_model.cpp
  ../src/chat/ChatModel.cpp
  ../src/chat/IrcParser.cpp
)

target_include_directories(shudder_chat_model_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_chat_model_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Test
  Qt6::WebSockets
  shudder_sanitizers
)

add_test(NAME shudder_chat_model_tests COMMAND shudder_chat_model_tests)

add_executable(shudder_playback_process_tests
  tst_playback_process.cpp
  ../src/core/ExternalProcessEnvironment.cpp
  ../src/core/UrlUtils.cpp
  ../src/playback/PlayerController.cpp
  ../src/playback/StreamlinkResolver.cpp
  ../src/web/PlayerHostServer.cpp
)

target_include_directories(shudder_playback_process_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_playback_process_tests PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_playback_process_tests COMMAND shudder_playback_process_tests)

add_executable(shudder_mpv_video_item_tests
  tst_mpv_video_item.cpp
  ../src/playback/MpvVideoItem.cpp
)

target_include_directories(shudder_mpv_video_item_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(shudder_mpv_video_item_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Test
  PkgConfig::MPV
  shudder_sanitizers
)

if(SHUDDER_ENABLE_SANITIZERS)
  add_test(NAME shudder_mpv_video_item_tests
    COMMAND xvfb-run -a "${CMAKE_COMMAND}" -E env
      DBUS_SESSION_BUS_ADDRESS=
      LIBGL_ALWAYS_SOFTWARE=1
      MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
      NO_AT_BRIDGE=1
      QT_QPA_PLATFORM=xcb
      QSG_RHI_BACKEND=opengl
      SHUDDER_MPV_HWDEC=no
      $<TARGET_FILE:shudder_mpv_video_item_tests>
  )
else()
  add_test(NAME shudder_mpv_video_item_tests
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run-in-headless-weston.sh" $<TARGET_FILE:shudder_mpv_video_item_tests>
  )
endif()

add_executable(shudder_standard_player_tests
  tst_standard_player.cpp
)

target_link_libraries(shudder_standard_player_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::Test
  Qt6::WebEngineQuick
)

if(SHUDDER_ENABLE_SANITIZERS)
  add_test(NAME shudder_standard_player_tests
    COMMAND xvfb-run -a "${CMAKE_COMMAND}" -E env
      DBUS_SESSION_BUS_ADDRESS=
      LIBGL_ALWAYS_SOFTWARE=1
      MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
      NO_AT_BRIDGE=1
      QT_QPA_PLATFORM=xcb
      QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu
      QSG_RHI_BACKEND=opengl
      $<TARGET_FILE:shudder_standard_player_tests>
  )
else()
  add_test(NAME shudder_standard_player_tests
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run-in-headless-weston.sh" "${CMAKE_COMMAND}" -E env
      QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu
      $<TARGET_FILE:shudder_standard_player_tests>
  )
endif()

add_executable(shudder_directory_model_tests
  tst_directory_model.cpp
  ../src/twitch/TwitchDirectoryModel.cpp
)

target_include_directories(shudder_directory_model_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_directory_model_tests PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_directory_model_tests COMMAND shudder_directory_model_tests)

add_executable(shudder_auth_service_tests
  tst_auth_service.cpp
  ../src/core/UrlUtils.cpp
  ../src/storage/SecretService.cpp
  ../src/twitch/TwitchAuthService.cpp
)

target_include_directories(shudder_auth_service_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_auth_service_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_auth_service_tests COMMAND shudder_auth_service_tests)

set_tests_properties(
  shudder_chat_model_tests
  shudder_auth_service_tests
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(shudder_secret_store_tests
  tst_secret_store.cpp
  ../src/storage/SecretService.cpp
)

target_include_directories(shudder_secret_store_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_secret_store_tests PRIVATE
  Qt6::Core
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_secret_store_tests COMMAND shudder_secret_store_tests)

add_executable(shudder_tab_transition_tests
  tst_tab_transitions.cpp
)

target_compile_definitions(shudder_tab_transition_tests PRIVATE
  SHUDDER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(shudder_tab_transition_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Test
  shudder_sanitizers
)

add_test(NAME shudder_tab_transition_tests COMMAND shudder_tab_transition_tests)
