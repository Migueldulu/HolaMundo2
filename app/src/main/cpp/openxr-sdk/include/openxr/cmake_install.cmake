# Install script for directory: C:/dev/openxr-build/OpenXR-SDK-Source/include/openxr

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/OPENXR")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/Users/miguel/AppData/Local/Android/Sdk/ndk/29.0.13599879/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Headers" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/openxr/openxr_platform_defines.h;/openxr/openxr.h;/openxr/openxr_loader_negotiation.h;/openxr/openxr_platform.h;/openxr/openxr_reflection.h;/openxr/openxr_reflection_structs.h;/openxr/openxr_reflection_parent_structs.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/openxr" TYPE FILE FILES
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_platform_defines.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_loader_negotiation.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_platform.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_reflection.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_reflection_structs.h"
    "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/openxr_reflection_parent_structs.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/dev/openxr-build/OpenXR-SDK-Source/build-android/include/openxr/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
