add_executable(localvocal_openai_response_tests openai-responses.cpp)
target_compile_features(localvocal_openai_response_tests PRIVATE cxx_std_17)
target_include_directories(localvocal_openai_response_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  $<TARGET_PROPERTY:${CMAKE_PROJECT_NAME},INCLUDE_DIRECTORIES>)
if(MSVC)
  target_compile_options(localvocal_openai_response_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(localvocal_openai_response_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME openai_responses_contract COMMAND localvocal_openai_response_tests)
