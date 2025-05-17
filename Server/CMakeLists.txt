cmake_minimum_required(VERSION 3.14)
project(mmo_servers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common library
add_library(common STATIC
    common/src/BaseServer.cpp
    common/src/Config.cpp
    common/src/Database.cpp
    common/src/MySQLClient.cpp
    common/src/RedisClient.cpp
    common/src/Security.cpp
    common/src/SocketServer.cpp
)
target_include_directories(common PUBLIC common/include)

# LZ4 auto-installation if not found
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZ4 liblz4)
if(NOT LZ4_FOUND)
    message(STATUS "liblz4 not found with pkg-config, trying manual search...")
    find_path(LZ4_INCLUDE_DIRS NAMES lz4.h)
    find_library(LZ4_LIBRARIES NAMES lz4)
    if(LZ4_INCLUDE_DIRS AND LZ4_LIBRARIES)
        set(LZ4_FOUND TRUE)
    else()
        message(FATAL_ERROR "LZ4 not found. Please install liblz4-dev.")
    endif()
endif()
target_link_libraries(common PUBLIC ${LZ4_LIBRARIES})
target_include_directories(common PUBLIC ${LZ4_INCLUDE_DIRS})

# OpenSSL auto-installation if not found
find_package(OpenSSL)
if(NOT OpenSSL_FOUND)
    message(STATUS "OpenSSL not found, attempting to install via apt-get (Linux only)...")
    if(UNIX AND NOT APPLE)
        execute_process(COMMAND sudo apt-get install -y libssl-dev)
        find_package(OpenSSL REQUIRED)
    else()
        message(FATAL_ERROR "Automatic OpenSSL installation is only supported on Linux. Please install OpenSSL manually.")
    endif()
endif()
target_link_libraries(common PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# MySQL auto-installation if not found
# Only use mysqlclient
pkg_check_modules(MYSQL mysqlclient)
if(NOT MYSQL_FOUND)
    message(STATUS "MySQL client not found, attempting to install via apt-get (Linux only)...")
    if(UNIX AND NOT APPLE)
        execute_process(COMMAND sudo apt-get install -y libmysqlclient-dev)
        pkg_check_modules(MYSQL REQUIRED mysqlclient)
    else()
        message(FATAL_ERROR "Automatic MySQL installation is only supported on Linux. Please install it manually.")
    endif()
endif()
target_link_libraries(common PUBLIC ${MYSQL_LIBRARIES})
target_include_directories(common PUBLIC ${MYSQL_INCLUDE_DIRS})

# Hiredis auto-installation if not found
pkg_check_modules(HIREDIS hiredis)
if(NOT HIREDIS_FOUND)
    message(STATUS "hiredis not found, attempting to install via apt-get (Linux only)...")
    if(UNIX AND NOT APPLE)
        execute_process(COMMAND sudo apt-get install -y libhiredis-dev)
        pkg_check_modules(HIREDIS REQUIRED hiredis)
    else()
        message(FATAL_ERROR "Automatic hiredis installation is only supported on Linux. Please install hiredis manually.")
    endif()
endif()
target_link_libraries(common PUBLIC ${HIREDIS_LIBRARIES})
target_include_directories(common PUBLIC ${HIREDIS_INCLUDE_DIRS})

# Auth server
add_executable(auth_server auth/src/main.cpp)
target_link_libraries(auth_server PRIVATE common)
target_include_directories(auth_server PRIVATE common/include auth/include)

# Chat server
add_executable(chat_server chat/src/main.cpp)
target_link_libraries(chat_server PRIVATE common)
target_include_directories(chat_server PRIVATE common/include chat/include)

# Game server
add_executable(game_server game/src/main.cpp)
target_link_libraries(game_server PRIVATE common)
target_include_directories(game_server PRIVATE common/include game/include)

# Optionally, add system libraries here if needed (e.g., pthread, ssl, lzo2, mysqlclient, hiredis)
# find_package(Threads REQUIRED)
# target_link_libraries(common PUBLIC Threads::Threads)
# ...
