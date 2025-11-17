file(GLOB_RECURSE C_SOURCES ./*.c)

add_library(protocol ${C_SOURCES})

target_include_directories(protocol PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_options(protocol PRIVATE -Wall -Wextra -pedantic -Werror)
