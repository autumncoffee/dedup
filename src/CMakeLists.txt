project(dedup)

cmake_minimum_required(VERSION 3.10)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(DEDUP_EXTRA_CXX_FLAGS "-stdlib=libc++")
    set(DEDUP_EXTRA_LIBS "-lstdc++" "-lc++experimental")

else()
    set(DEDUP_EXTRA_CXX_FLAGS "")
    set(DEDUP_EXTRA_LIBS "-lstdc++fs")
endif()

set(AC_COMMON_CXX_FLAGS "-std=c++17 ${DEDUP_EXTRA_CXX_FLAGS}")
set(AC_TCMALLOC_CXX_FLAGS "-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free")
set(AC_DEBUG_CXX_FLAGS "")
set(AC_TCMALLOC_LIBS "-ltcmalloc")

if("$ENV{IS_DEV}" STREQUAL "1")
    set(AC_TCMALLOC_CXX_FLAGS "")
    set(AC_DEBUG_CXX_FLAGS "-g")
    set(AC_TCMALLOC_LIBS "")

    set(CMAKE_EXE_LINKER_FLAGS "-g -Wl,-no_pie -fsanitize=address ${CMAKE_EXE_LINKER_FLAGS}")
    add_compile_options("-fsanitize=address")

else()
    add_compile_options("-O3")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${AC_COMMON_CXX_FLAGS} ${AC_TCMALLOC_CXX_FLAGS} ${AC_DEBUG_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

file(GLOB AC_DEDUP_SOURCES *.cpp)

include_directories("../ac")
include_directories(".")

add_subdirectory("../ac/ac-common" ac_common_bindir)
add_subdirectory("../ac/ac-library/containers/rbtree" ac_library_rbtree_bindir)

add_executable(dedup ${AC_DEDUP_SOURCES})

target_link_libraries(
    dedup
    ac_common
    ac_library_rbtree
    "-lcrypto"
    ${AC_TCMALLOC_LIBS}
    ${DEDUP_EXTRA_LIBS}
)

install(TARGETS dedup RUNTIME DESTINATION bin)
