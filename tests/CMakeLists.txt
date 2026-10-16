# Engine regression tests (no Qt needed). Enable with -DQSAPECNG_BUILD_TESTS=ON,
# run with: ctest --test-dir build --output-on-failure

set(ENGINE_SRCS
  ${PROJECT_SOURCE_DIR}/src/model/circuit.cpp
  ${PROJECT_SOURCE_DIR}/src/model/metacircuit.cpp
  ${PROJECT_SOURCE_DIR}/src/parser/crc_circuit.cpp
  ${PROJECT_SOURCE_DIR}/src/parser/ir_circuit.cpp
  ${PROJECT_SOURCE_DIR}/src/logger/logger.cpp
)

function(qsapecng_test name)
  add_executable(${name} ${ARGN} ${ENGINE_SRCS})
  target_include_directories(${name} PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src)
  target_link_libraries(${name} PRIVATE Boost::headers)
  target_compile_definitions(${name} PRIVATE
    BOOST_BIND_GLOBAL_PLACEHOLDERS BOOST_ALLOW_DEPRECATED_HEADERS)
  # checked iterators turn container misuse into a deterministic failure
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(${name} PRIVATE _GLIBCXX_DEBUG)
  elseif(MSVC)
    # MSVC checks iterators in the Debug configuration
    target_compile_options(${name} PRIVATE /bigobj)
  endif()
endfunction()

qsapecng_test(metacircuit_compress_test metacircuit_compress_test.cpp)
add_test(NAME issue6_compress COMMAND metacircuit_compress_test)

qsapecng_test(solve_test solve_test.cpp)
add_test(NAME issue6_active_bandpass
  COMMAND solve_test ${CMAKE_CURRENT_SOURCE_DIR}/data/issue6-active-bandpass.info
    "( - C1 * R2 * V1 ) * s / C1 * C2 * R1 * R2 * s^2 + ( C1 * R1 + C2 * R2 ) * s + 1"
    "( - 1 ) * s / 1 * s^2 + 2 * s + 1")
add_test(NAME rc_lowpass_netlist
  COMMAND solve_test ${CMAKE_CURRENT_SOURCE_DIR}/data/rc-lowpass-2nd-order.crc
    "V1 / ( - C1 * C2 * R1 * R2 ) * s^2 + ( ( - C2 * R2 ) + ( - C1 * R1 ) + ( - C2 * R1 ) ) * s + ( - 1 )"
    "1 / ( - 1e-06 ) * s^2 + ( - 0.003 ) * s + ( - 1 )")
