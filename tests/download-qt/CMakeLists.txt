cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(localvocal_download_tests LANGUAGES CXX)
  enable_testing()
  find_package(CURL REQUIRED)
  add_library(libcurl INTERFACE)
  target_link_libraries(libcurl INTERFACE CURL::libcurl)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
add_executable(localvocal_download_qt_tests worker.cpp
  ../../src/model-utils/model-downloader-ui.cpp
  ../../src/model-utils/model-downloader-ui.h
  ../../src/model-utils/sha256.cpp
  ../../src/translation/cloud-translation/curl-helper.cpp)
set_target_properties(localvocal_download_qt_tests PROPERTIES AUTOMOC ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/download-tests")
target_compile_features(localvocal_download_qt_tests PRIVATE cxx_std_17)
target_include_directories(localvocal_download_qt_tests BEFORE PRIVATE stubs ../../src)
target_link_libraries(localvocal_download_qt_tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
if(TARGET libcurl)
  target_link_libraries(localvocal_download_qt_tests PRIVATE libcurl)
else()
  target_link_libraries(localvocal_download_qt_tests PRIVATE CURL::libcurl)
endif()
add_test(NAME model_download_worker COMMAND localvocal_download_qt_tests)
set_tests_properties(model_download_worker PROPERTIES TIMEOUT 30)
if(MSVC)
  target_compile_options(localvocal_download_qt_tests PRIVATE /utf-8 /W4)
  add_custom_command(TARGET localvocal_download_qt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:localvocal_download_qt_tests>
      $<TARGET_FILE_DIR:localvocal_download_qt_tests> COMMAND_EXPAND_LISTS)
endif()
