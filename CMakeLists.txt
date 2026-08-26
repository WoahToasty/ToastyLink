cmake_minimum_required(VERSION 3.15)
project(ToastyLink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(toastylink
    src/main.cpp
    src/Socket.cpp
    src/XbdmClient.cpp
    src/HexUtils.cpp
    src/MemoryScanner.cpp
    src/Shell.cpp
)

target_include_directories(toastylink PRIVATE include)

if(WIN32)
    target_link_libraries(toastylink PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(toastylink PRIVATE /W4)
else()
    target_compile_options(toastylink PRIVATE -Wall -Wextra)
endif()

install(TARGETS toastylink RUNTIME DESTINATION bin)
