cmake_minimum_required(VERSION 3.20)
project(localvocal_upstream_reliability LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)

add_executable(localvocal_upstream_regression regression.cpp)
target_compile_features(localvocal_upstream_regression PRIVATE cxx_std_17)
add_test(NAME upstream_entity_and_translation_prefix COMMAND localvocal_upstream_regression)

add_executable(localvocal_upstream_safety safety.cpp
  ../../src/transcription-utils.cpp
  ../../src/model-utils/sha256.cpp)
target_compile_features(localvocal_upstream_safety PRIVATE cxx_std_17)
target_link_libraries(localvocal_upstream_safety PRIVATE Threads::Threads)
add_test(NAME upstream_text_utf8_download_lifetime COMMAND localvocal_upstream_safety)

foreach(test localvocal_upstream_regression localvocal_upstream_safety)
  if(MSVC)
    target_compile_options(${test} PRIVATE /W4 /WX /utf-8)
  else()
    target_compile_options(${test} PRIVATE -Wall -Wextra -Werror -pedantic)
  endif()
endforeach()

option(LOCALVOCAL_TEST_QT_WORKER "Build the real Qt/cURL model-download integration fixture" OFF)
if(LOCALVOCAL_TEST_QT_WORKER)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
  find_package(CURL REQUIRED)
  add_executable(localvocal_upstream_download_worker download-worker.cpp
    ../../src/model-utils/model-downloader-ui.cpp
    ../../src/model-utils/model-downloader-ui.h
    ../../src/model-utils/sha256.cpp
    ../../src/translation/cloud-translation/curl-helper.cpp)
  set_target_properties(localvocal_upstream_download_worker PROPERTIES AUTOMOC ON)
  target_compile_features(localvocal_upstream_download_worker PRIVATE cxx_std_17)
  target_include_directories(localvocal_upstream_download_worker BEFORE PRIVATE stubs ../../src)
  target_link_libraries(localvocal_upstream_download_worker
    PRIVATE Qt6::Core Qt6::Widgets Qt6::Network CURL::libcurl)
  add_test(NAME upstream_actual_qt_curl_model_worker COMMAND localvocal_upstream_download_worker)
  set_tests_properties(upstream_actual_qt_curl_model_worker PROPERTIES TIMEOUT 30)
endif()
