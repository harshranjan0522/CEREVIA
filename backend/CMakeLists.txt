cmake_minimum_required(VERSION 3.10)
project(cerevia_backend CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/json)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/json/sqlite)

add_executable(backend
    main.cpp
    ApiServer.cpp
    Http.cpp
    Database.cpp
    MoodTracker.cpp
    JournalManager.cpp
    Encryption.cpp
    Paths.cpp
    libs/json/sqlite/sqlite3.c
)

target_compile_definitions(backend PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

# The server used to be Winsock-only. Each platform now links just what it needs.
find_package(Threads REQUIRED)
target_link_libraries(backend PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(backend PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(backend PRIVATE ${CMAKE_DL_LIBS})
endif()
