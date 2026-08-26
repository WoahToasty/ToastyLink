cmake_minimum_required(VERSION 3.15)
project(ToastyLink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(toastylink
    src/main.cpp
    src/Socket.cpp
    src/XbdmClient.cpp
    src/HexUtils.cpp
    src/MemoryScanner.cpp
    src/Shell.cpp
    src/TypedValue.cpp
    src/AddressResolver.cpp
    src/ValueScanner.cpp
    src/Json.cpp
    src/FreezeEngine.cpp
    src/ConsoleBook.cpp
)

target_include_directories(toastylink PRIVATE include)

target_link_libraries(toastylink PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(toastylink PRIVATE ws2_32)
endif()

# GCC before 9 ships std::filesystem in a separate archive.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(toastylink PRIVATE stdc++fs)
endif()

if(MSVC)
    target_compile_options(toastylink PRIVATE /W4)
else()
    target_compile_options(toastylink PRIVATE -Wall -Wextra)
endif()

install(TARGETS toastylink RUNTIME DESTINATION bin)
