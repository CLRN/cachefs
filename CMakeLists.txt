cmake_minimum_required(VERSION 3.5)
project(cachefs)

set(CMAKE_CXX_STANDARD 14)

set(BOOST_COMPONENTS system	filesystem)
find_package(Boost COMPONENTS ${BOOST_COMPONENTS} REQUIRED)

add_definitions(-DFUSE_USE_VERSION=31
                -DFUSERMOUNT_DIR="~/")

include_directories(${Boost_INCLUDE_DIR}
                    ${CMAKE_CURRENT_LIST_DIR}
                    ${CMAKE_CURRENT_LIST_DIR}/libfuse/include)

file(GLOB SOURCE_FILES *.cpp *.h )
file(GLOB_RECURSE FUSE_FILES libfuse/lib/*.h libfuse/lib/*.c)
list(REMOVE_ITEM FUSE_FILES ${CMAKE_CURRENT_LIST_DIR}/libfuse/lib/mount_bsd.c)

add_executable(cachefs ${SOURCE_FILES} ${FUSE_FILES})
target_link_libraries(cachefs ${Boost_LIBRARIES} pthread dl)