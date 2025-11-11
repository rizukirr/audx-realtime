cmake_minimum_required(VERSION 4.0)

enable_testing()

if(EXISTS "${CMAKE_SOURCE_DIR}/external/Unity/src/unity.c")
    add_library(unity STATIC
        ${CMAKE_SOURCE_DIR}/external/Unity/src/unity.c
    )
    target_include_directories(unity PUBLIC
        ${CMAKE_SOURCE_DIR}/external/Unity/src
    )
    set(HAVE_UNITY TRUE)
else()
    message(FATAL_ERROR
        "Unity test framework not found, please run git submodel update --init")
endif()

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/external/Unity/src)

set(TEST_MOCKS_DIRS
    ${CMAKE_SOURCE_DIR}/tests/mocks)

function(add_audx_test test_name)
    add_executable(${test_name} ${ARGN} ${MOCK_SRC})
    target_include_directories(${test_name} PRIVATE
        ${TEST_INCLUDE_DIRS}
        ${TEST_MOCKS_DIRS}
    )

    target_link_libraries(${test_name}
        unity
        audx_src
    )

    # Enable LTO for tests if enabled globally (Android Release builds)
    # This ensures test executables can link with LTO-compiled libraries
    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set_target_properties(${test_name} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION ON
        )
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})

    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

add_audx_test(test_denoiser unit/test_denoiser.c)
