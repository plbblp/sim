cmake_minimum_required(VERSION 3.10)
project(JoystickReaderProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_VERBOSE_MAKEFILE ON)

message(STATUS "CMakeLists.txt processing started.")
message(STATUS "Project Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")

# --- ViGEmClient SDK Paths ---
set(VIGEM_SDK_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/ViGEm") # Base directory for ViGEm SDK

set(VIGEM_INCLUDE_DIR "${VIGEM_SDK_BASE_DIR}/include")
message(STATUS "ViGEm Include Dir: ${VIGEM_INCLUDE_DIR}")

set(VIGEM_LIBRARY_DIR_DEBUG_X64 "${VIGEM_SDK_BASE_DIR}/bin/release/x64")
message(STATUS "ViGEm Library Dir (Debug x64): ${VIGEM_LIBRARY_DIR_DEBUG_X64}")

set(VIGEM_DLL_DIR_DEBUG_X64 "${VIGEM_SDK_BASE_DIR}/bin/release/x64")
message(STATUS "ViGEm DLL Dir (Debug x64): ${VIGEM_DLL_DIR_DEBUG_X64}")


# 将ViGEm的头文件目录添加到项目的包含路径中
include_directories(${VIGEM_INCLUDE_DIR})

# 将ViGEm的库文件目录添加到链接器的搜索路径中
link_directories(${VIGEM_LIBRARY_DIR_DEBUG_X64})


if(MSVC)
    message(STATUS "MSVC compiler detected.")
else()
    message(WARNING "Non-MSVC compiler detected. Ensure ViGEmClient library compatibility.")
endif()

# --- 主程序 ---
set(EXECUTABLE_NAME "JoystickReaderApp")
set(SOURCE_FILE "main.cpp") # 您的主程序文件名

message(STATUS "Attempting to add executable '${EXECUTABLE_NAME}' with source '${SOURCE_FILE}'")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
    message(FATAL_ERROR "Source file '${SOURCE_FILE}' not found in '${CMAKE_CURRENT_SOURCE_DIR}'. Please check the file name and location.")
endif()

add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})
message(STATUS "add_executable for '${EXECUTABLE_NAME}' called.")

if(TARGET ${EXECUTABLE_NAME})
    message(STATUS "Target '${EXECUTABLE_NAME}' successfully created by CMake.")
else()
    message(FATAL_ERROR "Target '${EXECUTABLE_NAME}' was NOT created by CMake after add_executable. This is the primary issue.")
endif()

# --- 链接库 ---
message(STATUS "Attempting to link libraries to '${EXECUTABLE_NAME}'")
target_link_libraries(${EXECUTABLE_NAME} PRIVATE dinput8 dxguid user32 ViGEmClient)
message(STATUS "target_link_libraries for '${EXECUTABLE_NAME}' called successfully.")

# --- Post-build step to copy ViGEmClient.dll ---
set(VIGEM_DLL_FILE_DEBUG_X64 "${VIGEM_DLL_DIR_DEBUG_X64}/ViGEmClient.dll")
if(EXISTS "${VIGEM_DLL_FILE_DEBUG_X64}")
    message(STATUS "ViGEmClient.dll found at ${VIGEM_DLL_FILE_DEBUG_X64}, will attempt to copy.")
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VIGEM_DLL_FILE_DEBUG_X64}"
        $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
        COMMENT "Copying ViGEmClient.dll to output directory"
    )
else()
    message(WARNING "ViGEmClient.dll NOT found at ${VIGEM_DLL_FILE_DEBUG_X64}. Runtime will likely fail. Please ensure the DLL is present and the path is correct.")
endif()

message(STATUS "CMakeLists.txt processing finished.")