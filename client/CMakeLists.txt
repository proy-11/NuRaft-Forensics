# add_executable(raft_client
#                raft_client.cxx
#                in_memory_log_store.cxx)
# add_dependencies(raft_client
#                  static_lib)
# target_link_libraries(raft_client
#                       ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
#                       ${LIBRARIES})


set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.74.0 COMPONENTS program_options thread)
if(Boost_FOUND)
    message(STATUS "Boost found")
    include_directories(${Boost_INCLUDE_DIRS})
    # add_executable(workload
    #                 workload.cxx)
    # add_dependencies(workload
    #                  static_lib)
    # target_link_libraries(workload
    #                       ${Boost_LIBRARIES}
    #                       ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
    #                       ${LIBRARIES})
    
    add_executable(d_raft
                    d_raft_server.cxx
                    logger.cc
                    workload.cxx
                    job_queue.cxx
                    in_memory_log_store.cxx)
    include_directories(${Boost_INCLUDE_DIRS})
    add_dependencies(d_raft
                     static_lib)
    target_link_libraries(d_raft
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                          ${LIBRARIES})

    # add_executable(d_raft_client
    #                 d_raft_client.cxx
    #                 d_raft_scheduler.cxx
    #                 workload.cxx
    #                 in_memory_log_store.cxx)
    # include_directories(${Boost_INCLUDE_DIRS})
    # add_dependencies(d_raft_client
    #                  static_lib)
    # target_link_libraries(d_raft_client
    #                       ${Boost_LIBRARIES}
    #                       ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
    #                       ${LIBRARIES})

    add_executable(d_raft_launcher
                    workload.cxx
                    d_raft_scheduler.cxx
                    d_raft_launcher.cxx
                    req_socket_mgr.cxx
                    server_data_mgr.cxx
                    utils.cxx
                    sync_file_obj.hxx
                    commander.cxx)
    include_directories(${Boost_INCLUDE_DIRS})
    add_dependencies(d_raft_launcher
                     static_lib)
    target_link_libraries(d_raft_launcher
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                          ${LIBRARIES})
endif()
