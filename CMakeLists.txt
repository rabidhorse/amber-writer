# Builds Amber Writer for macOS and Linux with Qt 6. (Windows uses the Makefile.)
#   cmake -B build-qt -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-qt
#   ctest --test-dir build-qt      file format and spelling tests
cmake_minimum_required(VERSION 3.16)

file(READ src/version.h VERSION_H)
string(REGEX MATCH "AMBER_VERSION \"([0-9.]+)\"" _ "${VERSION_H}")
project(AmberWriter VERSION ${CMAKE_MATCH_1} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_AUTORCC ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest macOS to run on")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)
find_package(Threads REQUIRED)

# The documents and spelling code, shared with the Windows version. It works in UTF-16,
# so L"..." strings must be 16 bits wide here too.
add_library(ambercore STATIC src/doc.c src/spell.c src/port.c src/dict_data.c src/beep_unix.c)
target_include_directories(ambercore PUBLIC src)
target_compile_options(ambercore PUBLIC $<$<COMPILE_LANGUAGE:C>:-fshort-wchar>)
target_compile_definitions(ambercore PRIVATE _POSIX_C_SOURCE=200809L _DARWIN_C_SOURCE)
target_link_libraries(ambercore PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT APPLE)
  target_link_libraries(ambercore PUBLIC m)
else()
  target_link_libraries(ambercore PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
endif()

if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE amber.icns)
  set_source_files_properties(res/amber.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(AmberWriter MACOSX_BUNDLE src/amber_qt.cpp unix/amber.qrc res/amber.icns)
  set_target_properties(AmberWriter PROPERTIES
    OUTPUT_NAME "Amber Writer"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/unix/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.amberwriter.AmberWriter
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
else()
  add_executable(AmberWriter src/amber_qt.cpp unix/amber.qrc)
  set_target_properties(AmberWriter PROPERTIES OUTPUT_NAME amberwriter)
  include(GNUInstallDirs)
  install(TARGETS AmberWriter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES unix/amberwriter.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES res/amber.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME amberwriter.png)
endif()
target_link_libraries(AmberWriter PRIVATE ambercore Qt6::Widgets Qt6::PrintSupport)

enable_testing()
add_executable(amber_tests tests/tests.c)
target_link_libraries(amber_tests PRIVATE ambercore)
# The tests read tests/files and write into build/, relative to where they run.
set(TEST_DIR ${CMAKE_BINARY_DIR}/testrun)
file(MAKE_DIRECTORY ${TEST_DIR}/build)
configure_file(tests/files/python-0.2.docx ${TEST_DIR}/tests/files/python-0.2.docx COPYONLY)
add_test(NAME amber_tests COMMAND amber_tests WORKING_DIRECTORY ${TEST_DIR})
set_tests_properties(amber_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
