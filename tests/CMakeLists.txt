# Test executables for libigrf.  Each suite is its own binary and its
# own CTest test; igrf_tests is an aggregate target building them all.

set(IGRF_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/data")
set(IGRF_TEST_REFERENCE_DIR "${CMAKE_SOURCE_DIR}/tests/reference")

add_executable(test_coordinates test_coordinates.c)
add_executable(test_shc test_shc.c)
add_executable(test_igrf test_igrf.c)
add_executable(test_field_elements test_field_elements.c)
add_executable(test_batch test_batch.c)
add_executable(test_time test_time.c)
add_executable(test_reference test_reference.c)

set(IGRF_TEST_TARGETS
    test_coordinates
    test_shc
    test_igrf
    test_field_elements
    test_batch
    test_time
    test_reference)

foreach(target ${IGRF_TEST_TARGETS})
    target_link_libraries(${target} PRIVATE igrf::igrf)
    target_compile_definitions(${target} PRIVATE
        IGRF_TEST_DATA_DIR="${IGRF_TEST_DATA_DIR}"
        IGRF_TEST_REFERENCE_DIR="${IGRF_TEST_REFERENCE_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endif()
endforeach()

foreach(target ${IGRF_TEST_TARGETS})
    add_test(NAME ${target} COMMAND ${target}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# Aggregate target so `cmake --build . --target igrf_tests` builds the
# whole suite.
add_custom_target(igrf_tests DEPENDS ${IGRF_TEST_TARGETS})
