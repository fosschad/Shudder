set(TEST_COMMON_SOURCES
  ../src/chat/IrcParser.cpp
  ../src/core/UrlUtils.cpp
  ../src/core/XdgPaths.cpp
  ../src/storage/PreferencesService.cpp
  ../src/playback/StreamlinkResolver.cpp
)

add_executable(shudder_core_tests
  tst_core.cpp
  ${TEST_COMMON_SOURCES}
)

target_include_directories(shudder_core_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/src/generated"
)

target_link_libraries(shudder_core_tests PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Test
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
)

add_test(NAME shudder_chat_jump_tests COMMAND shudder_chat_jump_tests)

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
)

add_test(NAME shudder_tab_transition_tests COMMAND shudder_tab_transition_tests)
