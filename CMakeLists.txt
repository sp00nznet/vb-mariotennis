cmake_minimum_required(VERSION 3.16)
project(mariotennis C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# vbrecomp library (as subdirectory)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vbrecomp vbrecomp_build)

# Mario's Tennis recompiled executable
add_executable(mariotennis
    src/main.c
    generated/recomp_funcs.c
)

target_include_directories(mariotennis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mariotennis PRIVATE vbrecomp)

# Suppress warnings in generated code
if(MSVC)
    target_compile_options(mariotennis PRIVATE /W1 /wd4244 /wd4146)
else()
    target_compile_options(mariotennis PRIVATE -w)
endif()
