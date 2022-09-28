add_executable(raft_client
               main.cxx
               raft_client.cxx
               in_memory_log_store.cxx)
add_dependencies(raft_client
                 static_lib)
target_link_libraries(raft_client
                      ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                      ${LIBRARIES})


set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.74.0 COMPONENTS program_options)
if(Boost_FOUND)
    message(STATUS "Boost found")
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(workload
    workload.cxx)
    add_dependencies(workload
                     static_lib)
    target_link_libraries(workload
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                          ${LIBRARIES})
    
    add_executable(d_raft
                    d_raft_server.cxx
                    logger.cc
                    in_memory_log_store.cxx)
    include_directories(${Boost_INCLUDE_DIRS})
    add_dependencies(d_raft
                     static_lib)
    target_link_libraries(d_raft
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                          ${LIBRARIES})
endif()
