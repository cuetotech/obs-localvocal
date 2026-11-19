include(FetchContent)

set(CUSTOM_ONNXRUNTIME_URL
    ""
    CACHE STRING "URL of a downloaded ONNX Runtime tarball")

set(CUSTOM_ONNXRUNTIME_HASH
    ""
    CACHE STRING "Hash of a downloaded ONNX Runtime tarball")

# v1.20.1 is the last version to support MacOS 12 v1.23.0 is the last version to support x86_64 on MacOS, as well as
# MacOS 13
set(Onnxruntime_VERSION "1.20.1")

if(CUSTOM_ONNXRUNTIME_URL STREQUAL "")
  set(USE_PREDEFINED_ONNXRUNTIME ON)
else()
  if(CUSTOM_ONNXRUNTIME_HASH STREQUAL "")
    message(FATAL_ERROR "Both of CUSTOM_ONNXRUNTIME_URL and CUSTOM_ONNXRUNTIME_HASH must be present!")
  else()
    set(USE_PREDEFINED_ONNXRUNTIME OFF)
  endif()
endif()

# Microsoft retains the 1.20.1 native binaries in its official NuGet package.
# The old GitHub release download URLs currently return HTTP 404. This package
# contains all supported desktop RIDs and the C++ header tree.
if(USE_PREDEFINED_ONNXRUNTIME)
  set(Onnxruntime_URL
      "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${Onnxruntime_VERSION}/microsoft.ml.onnxruntime.${Onnxruntime_VERSION}.nupkg")
  set(Onnxruntime_HASH "")
else()
  set(Onnxruntime_URL "${CUSTOM_ONNXRUNTIME_URL}")
  set(Onnxruntime_HASH "${CUSTOM_ONNXRUNTIME_HASH}")
endif()

if(Onnxruntime_HASH)
  FetchContent_Declare(onnxruntime URL "${Onnxruntime_URL}" URL_HASH "${Onnxruntime_HASH}")
else()
  FetchContent_Declare(onnxruntime URL "${Onnxruntime_URL}")
endif()
FetchContent_MakeAvailable(onnxruntime)

# Support both the legacy GitHub release archive and the native NuGet layout.
if(EXISTS "${onnxruntime_SOURCE_DIR}/build/native/include")
  set(Onnxruntime_IS_NUGET TRUE)
  set(Onnxruntime_INCLUDE_DIR "${onnxruntime_SOURCE_DIR}/build/native/include")
else()
  set(Onnxruntime_IS_NUGET FALSE)
  set(Onnxruntime_INCLUDE_DIR "${onnxruntime_SOURCE_DIR}/include")
endif()

if(APPLE)
  if(Onnxruntime_IS_NUGET)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
      set(Onnxruntime_MAC_RID "osx-arm64")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
      set(Onnxruntime_MAC_RID "osx-x64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
      set(Onnxruntime_MAC_RID "osx-arm64")
    else()
      set(Onnxruntime_MAC_RID "osx-x64")
    endif()
    set(Onnxruntime_MAC_LIBDIR "${onnxruntime_SOURCE_DIR}/runtimes/${Onnxruntime_MAC_RID}/native")
    if(NOT EXISTS "${Onnxruntime_MAC_LIBDIR}/libonnxruntime.dylib")
      message(FATAL_ERROR "Official NuGet package lacks ${Onnxruntime_MAC_RID}/libonnxruntime.dylib")
    endif()
    install_library_to_bundle("${Onnxruntime_MAC_LIBDIR}" libonnxruntime.dylib BUILD_DEPENDENCY)
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${Onnxruntime_INCLUDE_DIR}")
  else()
    install_library_to_bundle("${onnxruntime_SOURCE_DIR}" libonnxruntime.${Onnxruntime_VERSION}.dylib BUILD_DEPENDENCY)
  endif()
  add_custom_command(
    TARGET "${CMAKE_PROJECT_NAME}"
    POST_BUILD
    COMMAND
      ${CMAKE_INSTALL_NAME_TOOL} -change "@rpath/libonnxruntime.${Onnxruntime_VERSION}.dylib"
      "@loader_path/../Frameworks/$<IF:$<BOOL:${Onnxruntime_IS_NUGET}>,libonnxruntime.dylib,libonnxruntime.${Onnxruntime_VERSION}.dylib>" $<TARGET_FILE:${CMAKE_PROJECT_NAME}>)
elseif(MSVC)
  if(Onnxruntime_IS_NUGET)
    set(Onnxruntime_WIN_LIBDIR "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native")
  else()
    set(Onnxruntime_WIN_LIBDIR "${onnxruntime_SOURCE_DIR}/lib")
  endif()
  add_library(Ort INTERFACE)
  foreach(lib_name IN ITEMS onnxruntime onnxruntime_providers_shared)
    if(EXISTS "${Onnxruntime_WIN_LIBDIR}/${lib_name}.dll" AND
       EXISTS "${Onnxruntime_WIN_LIBDIR}/${lib_name}.lib")
      add_library(Ort::${lib_name} SHARED IMPORTED)
      set_target_properties(Ort::${lib_name} PROPERTIES
        IMPORTED_IMPLIB "${Onnxruntime_WIN_LIBDIR}/${lib_name}.lib"
        IMPORTED_LOCATION "${Onnxruntime_WIN_LIBDIR}/${lib_name}.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${Onnxruntime_INCLUDE_DIR}")
      target_link_libraries(Ort INTERFACE Ort::${lib_name})
      install(FILES "${Onnxruntime_WIN_LIBDIR}/${lib_name}.dll"
        DESTINATION "obs-plugins/64bit")
    elseif(lib_name STREQUAL "onnxruntime")
      message(FATAL_ERROR "Official ONNX Runtime package lacks Windows x64 import library or DLL")
    endif()
  endforeach()
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Ort)
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${Onnxruntime_INCLUDE_DIR}")

else()
  if(Onnxruntime_IS_NUGET)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      set(Onnxruntime_LINUX_RID "linux-arm64")
    else()
      set(Onnxruntime_LINUX_RID "linux-x64")
    endif()
    set(Onnxruntime_LINK_LIBS
        "${onnxruntime_SOURCE_DIR}/runtimes/${Onnxruntime_LINUX_RID}/native/libonnxruntime.so")
    set(Onnxruntime_INSTALL_LIBS "${Onnxruntime_LINK_LIBS}")
  else()
    set(Onnxruntime_LINK_LIBS "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${Onnxruntime_VERSION}")
    set(Onnxruntime_INSTALL_LIBS "${Onnxruntime_LINK_LIBS}"
        "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime_providers_shared.so"
        "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so"
        "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.1")
  endif()
  if(NOT EXISTS "${Onnxruntime_LINK_LIBS}")
    message(FATAL_ERROR "ONNX Runtime native Linux library missing: ${Onnxruntime_LINK_LIBS}")
  endif()
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${Onnxruntime_LINK_LIBS}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${Onnxruntime_INCLUDE_DIR}")
  install(FILES ${Onnxruntime_INSTALL_LIBS}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins/${CMAKE_PROJECT_NAME}")
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN/${CMAKE_PROJECT_NAME}")

  find_program(PATCHELF_EXE patchelf)
  if(PATCHELF_EXE AND NOT Onnxruntime_IS_NUGET)
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND ${PATCHELF_EXE} --replace-needed libonnxruntime.so.1 libonnxruntime.so.${Onnxruntime_VERSION}
              $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
      COMMENT "Fixing ONNX dependency name for portability")
  else()
    message(WARNING "patchelf not found! The binary will still look for libonnxruntime.so.1")
  endif()
endif()
