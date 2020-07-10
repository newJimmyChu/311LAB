cmake_minimum_required(VERSION 3.15)
project(redis_4_0_9 C)

set(CMAKE_C_STANDARD 99)


set(
        SOURCE_FILES
        ./lcloud_filesys.c
        ./lcloud_cache.c
        ./lcloud_client.c
)
add_executable(assign3 ${SOURCE_FILES})