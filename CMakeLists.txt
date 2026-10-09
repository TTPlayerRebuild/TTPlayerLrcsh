cmake_minimum_required(VERSION 3.24)
project(TtpLrcsh LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_lrcsh requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/version.cmake)
add_library(ttp_lrcsh SHARED src/plugin.cpp src/search.cpp src/protocol.cpp src/transport.cpp
  src/catalog.cpp src/xml.cpp src/strings.rc src/ttp_lrcsh.def
  "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_lrcsh ttp_lrcsh_version)
target_include_directories(ttp_lrcsh PUBLIC include PRIVATE src)
target_compile_definitions(ttp_lrcsh PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_lrcsh PRIVATE /W4 /permissive- /EHsc /GR- /utf-8)
target_link_libraries(ttp_lrcsh PRIVATE wininet ole32 uuid user32)
set_target_properties(ttp_lrcsh PROPERTIES PREFIX "" OUTPUT_NAME "ttp_lrcsh")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_lrcsh POST_BUILD COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py" "$<TARGET_FILE:ttp_lrcsh>"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
  --report "$<TARGET_FILE_DIR:ttp_lrcsh>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_lrcsh RUNTIME DESTINATION AddIn COMPONENT Runtime)
