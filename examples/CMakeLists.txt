
# === Example replicated calculator ===
add_executable(calc_server
               calculator/calc_server.cxx
               logger.cc
               in_memory_log_store.cxx)
add_dependencies(calc_server
                 static_lib)
target_link_libraries(calc_server
                      ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                      ${LIBRARIES})

add_executable(echo_server
               echo/echo_server.cxx
               logger.cc
               in_memory_log_store.cxx)
add_dependencies(echo_server
                 static_lib)
target_link_libraries(echo_server
                      ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                      ${LIBRARIES})

add_executable(quick_start
               quick_start.cxx
               logger.cc
               in_memory_log_store.cxx)
add_dependencies(quick_start
                 static_lib)
target_link_libraries(quick_start
                      ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                      ${LIBRARIES})

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.74.0 COMPONENTS program_options)
if(Boost_FOUND)
    message(STATUS "Boost found")
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(mock_cli
                   mock_cli.cxx)
    add_dependencies(mock_cli
                     static_lib)
    target_link_libraries(mock_cli
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                          ${LIBRARIES})
endif()
