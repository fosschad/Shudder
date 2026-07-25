set(TEST_COMMON_SOURCES
  ../src/chat/IrcParser.cpp
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
