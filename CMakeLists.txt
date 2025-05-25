cmake_minimum_required(VERSION 3.10)
project(SimProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_VERBOSE_MAKEFILE ON) # More output during build

add_compile_options(/source-charset:utf-8 /execution-charset:utf-8)

# --- Windows Specific Settings ---
if(MSVC)
    message(STATUS "MSVC compiler detected.")
    # NOMINMAX should be defined in main.cpp, but can also be added here if needed globally
    # add_definitions(-DNOMINMAX)
    # Ensure source files are treated as UTF-8 by the compiler (helps with C4819)
    add_compile_options(/source-charset:utf-8 /execution-charset:utf-8)
else()
    message(WARNING "Non-MSVC compiler detected. Ensure library compatibility.")
endif()

# --- ViGEmClient SDK Paths (assuming current structure) ---
set(VIGEM_SDK_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/ViGEm")
set(VIGEM_INCLUDE_DIR "${VIGEM_SDK_BASE_DIR}/include")

# --- Determine ViGEm Library and DLL paths based on build type ---
# We'll default to Debug for now, you can expand this for Release
# Ensure you have the corresponding Release .lib and .dll if you build in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE) # Default to Debug if not set
    set(VIGEM_LIBRARY_DIR "${VIGEM_SDK_BASE_DIR}/bin/release/x64")
    set(VIGEM_DLL_DIR "${VIGEM_SDK_BASE_DIR}/bin/release/x64")
    message(STATUS "ViGEm SDK Paths (Debug):")
else() # Assuming Release or other
    set(VIGEM_LIBRARY_DIR "${VIGEM_SDK_BASE_DIR}/bin/release/x64") # Assuming you have these
    set(VIGEM_DLL_DIR "${VIGEM_SDK_BASE_DIR}/bin/release/x64")   # Assuming you have these
    message(STATUS "ViGEm SDK Paths (Release or other):")
endif()
message(STATUS "  Include Dir: ${VIGEM_INCLUDE_DIR}")
message(STATUS "  Library Dir: ${VIGEM_LIBRARY_DIR}")
message(STATUS "  DLL Dir: ${VIGEM_DLL_DIR}")

include_directories(${VIGEM_INCLUDE_DIR})
link_directories(${VIGEM_LIBRARY_DIR}) # For ViGEmClient.lib

# --- OpenCV ---
# Assuming CMAKE_TOOLCHAIN_FILE is set in VSCode settings to point to vcpkg.cmake
find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    message(STATUS "OpenCV found via vcpkg.")
    message(STATUS "OpenCV Version: ${OpenCV_VERSION}")
    message(STATUS "OpenCV Include Dirs: ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV Libraries: ${OpenCV_LIBS}")
    include_directories(${OpenCV_INCLUDE_DIRS}) # Add OpenCV include dirs
else()
    message(FATAL_ERROR "OpenCV not found. Ensure CMAKE_TOOLCHAIN_FILE is set correctly for vcpkg and OpenCV is installed via vcpkg.")
endif()


# --- Main Program ---
set(EXECUTABLE_NAME "SimApp")
set(SOURCE_FILE "main.cpp")

add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

# --- Link Libraries ---
set(DX_LIBS dxgi d3d11) # DirectX libraries for Desktop Duplication API

target_link_libraries(${EXECUTABLE_NAME} PRIVATE
    dinput8      # DirectInput
    dxguid       # GUIDs for DirectX components
    user32       # Windows User API (CreateWindow, GetAsyncKeyState, etc.)
    ViGEmClient  # ViGEm SDK library
    ${OpenCV_LIBS} # OpenCV libraries (resolved by find_package)
    ${DX_LIBS}     # DirectX libraries (dxgi.lib, d3d11.lib)
)

# --- Post-build step to copy ViGEmClient.dll ---
set(VIGEM_DLL_FILE_TO_COPY "${VIGEM_DLL_DIR}/ViGEmClient.dll")
if(EXISTS "${VIGEM_DLL_FILE_TO_COPY}")
    message(STATUS "ViGEmClient.dll found at ${VIGEM_DLL_FILE_TO_COPY}, will attempt to copy.")
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VIGEM_DLL_FILE_TO_COPY}"
        $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
        COMMENT "Copying ViGEmClient.dll to output directory"
    )
else()
    message(WARNING "ViGEmClient.dll NOT found at ${VIGEM_DLL_FILE_TO_COPY}. Runtime may fail. Check VIGEM_DLL_DIR and build type.")
endif()

# --- OpenCV DLLs ---
# When using vcpkg with dynamic linking (default x64-windows), the OpenCV DLLs
# are in vcpkg's installed bin directory (e.g., vcpkg_root/installed/x64-windows/bin).
# For the application to run, this directory needs to be in the system PATH,
# or the necessary DLLs must be copied to the executable's directory.
# VSCode's CMake Tools extension often handles this for debugging sessions if vcpkg is integrated.
# For standalone execution, ensure PATH is set or copy DLLs.
# Example of copying one common OpenCV DLL (you might need more or opencv_worldXXXd.dll):
# You'd need to know the exact DLL name and find it within vcpkg's installed tree.
# This is more robustly handled by ensuring vcpkg's bin path is in the system PATH.
#
# if(OpenCV_FOUND AND WIN32)
#     # This is a simplified example and might need adjustment for specific OpenCV DLLs and paths
#     # It's often better to add vcpkg's bin directory to the PATH for development.
#     file(GLOB OpenCV_RUNTIME_DLLS "${OpenCV_DIR}/../../../bin/opencv_world${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}d.dll") # Example for debug world dll
#     if(OpenCV_RUNTIME_DLLS)
#         add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
#             COMMAND ${CMAKE_COMMAND} -E copy_if_different
#             ${OpenCV_RUNTIME_DLLS}
#             $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
#             COMMENT "Copying OpenCV DLLs to output directory"
#         )
#     else()
#         message(WARNING "Could not automatically find OpenCV runtime DLLs to copy. Ensure they are in PATH or copied manually.")
#     endif()
# endif()

message(STATUS "CMakeLists.txt processing finished.")