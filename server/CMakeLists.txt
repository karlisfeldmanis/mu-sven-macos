cmake_minimum_required(VERSION 3.20)
project(MuServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0")
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
endif()

find_package(SQLite3 REQUIRED)

include_directories(include)

add_executable(MuServer
    src/main.cpp
    src/Server.cpp
    src/Session.cpp
    src/PacketHandler.cpp
    src/GameWorld.cpp
    src/Database.cpp
)

target_link_libraries(MuServer PRIVATE SQLite::SQLite3)

message(STATUS "Configured MuServer (Lorencia-only)")
