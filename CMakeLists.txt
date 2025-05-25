cmake_minimum_required(VERSION 3.10) # 指定CMake的最低版本

# 项目名称和语言
project(JoystickReader LANGUAGES CXX)

# 设置C++标准 (例如 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加可执行文件目标，源文件是 joystick_reader.cpp
add_executable(main main.cpp)

# 链接DirectInput和DXGuid库
# 对于MSVC编译器，通常这样写就可以，CMake会自动找到Windows SDK中的库
# 对于MinGW，库名可能是 libdinput8.a 和 libdxguid.a，并且可能需要指定库路径
if(MSVC)
    target_link_libraries(main PRIVATE dinput8 dxguid user32)
else()
    # 对于MinGW或其他编译器，您可能需要更明确地指定
    # 或者确保这些库在编译器的搜索路径中
    # user32.lib (或 libuser32.a) 是为了 GetAsyncKeyState, CreateWindow 等
    target_link_libraries(main PRIVATE dinput8 dxguid user32)
    # 如果MinGW找不到，您可能需要使用 find_library 并指定路径，
    # 或者直接在 target_link_libraries 中使用 -ldinput8 -ldxguid 等，
    # 并通过 link_directories() 添加库的搜索路径。
endif()

# 确保Windows.h等可用 (通常不需要显式添加，因为编译器会查找系统头文件)
# if(WIN32)
#   include_directories(${CMAKE_CURRENT_SOURCE_DIR}) # 确保本地头文件可以被找到
# endif()