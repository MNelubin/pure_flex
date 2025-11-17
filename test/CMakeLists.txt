file(GLOB_RECURSE C_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)

add_mocked_test(main
        SOURCES ${C_SOURCES}
        MOCKS getc putc fprintf exit
        COMPILE_OPTIONS -fno-builtin
        )
target_include_directories(test_main PUBLIC "${CMAKE_SOURCE_DIR}/include")
