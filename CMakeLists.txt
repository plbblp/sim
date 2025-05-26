cmake_minimum_required(VERSION 3.10)
project(JoystickReaderProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_VERBOSE_MAKEFILE ON)

message(STATUS "CMakeLists.txt processing started.")
message(STATUS "Project Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")

# --- ViGEmClient SDK Paths ---
set(VIGEM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/ViGEm/include")
message(STATUS "ViGEm Include Dir: ${VIGEM_INCLUDE_DIR}")
set(VIGEM_LIBRARY_DIR_DEBUG_X64 "${CMAKE_CURRENT_SOURCE_DIR}/libs/ViGEm/bin/release/x64")
message(STATUS "ViGEm Library Dir (Debug x64): ${VIGEM_LIBRARY_DIR_DEBUG_X64}")
set(VIGEM_DLL_DIR_DEBUG_X64 "${CMAKE_CURRENT_SOURCE_DIR}/libs/ViGEm/bin/release/x64")
message(STATUS "ViGEm DLL Dir (Debug x64): ${VIGEM_DLL_DIR_DEBUG_X64}")

include_directories(${VIGEM_INCLUDE_DIR})
link_directories(${VIGEM_LIBRARY_DIR_DEBUG_X64})

# --- OpenCV ---
# 尝试找到OpenCV包。如果CMake找不到，您可能需要设置 OpenCV_DIR 环境变量
# 或者在CMake配置时通过 -DOpenCV_DIR=/path/to/opencv/build 传递
find_package(OpenCV REQUIRED)
if(OpenCV_FOUND)
    message(STATUS "OpenCV found. Version: ${OpenCV_VERSION}")
    include_directories(${OpenCV_INCLUDE_DIRS}) # 添加OpenCV的包含目录
else()
    message(FATAL_ERROR "OpenCV not found. Please ensure OpenCV is installed and CMake can find it.")
endif()


if(MSVC)
    message(STATUS "MSVC compiler detected.")
else()
    message(WARNING "Non-MSVC compiler detected. Ensure ViGEmClient library compatibility.")
endif()

# --- 主程序 ---
set(EXECUTABLE_NAME "JoystickReaderApp")
set(SOURCE_FILE "main.cpp")

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
# 链接 DirectInput, User32, ViGEmClient, 和 OpenCV
# OpenCV_LIBS 变量由 find_package(OpenCV) 设置，包含了所有需要的OpenCV模块
target_link_libraries(${EXECUTABLE_NAME} PRIVATE dinput8 dxguid user32 ViGEmClient ${OpenCV_LIBS} SetupAPI dxgi d3d11)
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
    message(WARNING "ViGEmClient.dll NOT found at ${VIGEM_DLL_FILE_DEBUG_X64}. Runtime will likely fail.")
endif()


message(STATUS "CMakeLists.txt processing finished.")