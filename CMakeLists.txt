cmake_minimum_required(VERSION 3.15)
project(XenonLink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(xenonlink
    src/main.cpp
    src/Socket.cpp
    src/XbdmClient.cpp
    src/HexUtils.cpp
    src/MemoryScanner.cpp
    src/Shell.cpp
)

target_include_directories(xenonlink PRIVATE include)

if(WIN32)
    target_link_libraries(xenonlink PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(xenonlink PRIVATE /W4)
else()
    target_compile_options(xenonlink PRIVATE -Wall -Wextra)
endif()

install(TARGETS xenonlink RUNTIME DESTINATION bin)
