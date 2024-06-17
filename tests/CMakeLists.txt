# === Basic Raft server functionality test ===

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost 1.74.0 COMPONENTS program_options thread filesystem)

# FIND_PATH(
#         LIBCRYPTOPP_INCLUDE_DIR
#         NAME cryptopp
#         HINTS $ENV{HOME}/local/include
#             /opt/local/include
#             /usr/local/include/cryptopp
#             /usr/include
#         PATH_SUFFIXES cryptopp
#         )
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")
# message(STATUS "Found cryptopp: ${LIBCRYPTOPP_INCLUDE_DIR}/cryptopp")
# set(LIBCRYPTOPP_INCLUDE_DIRS ${LIBCRYPTOPP_INCLUDE_DIR} )
# set(LIBCRYPTOPP_LIBRARIES ${LIBCRYPTOPP_LIBRARY} )
# message(STATUS "Test libcryptolib: ${LIBCRYPTOPP_LIBRARIES}")

# set(LIBCRYPTOPP_PATH_HINT
#     ~/Library/Frameworks
#     /Library/Frameworks
#     $ENV{HOME}/local/lib64
#     $ENV{HOME}/local/lib
#     /usr/lib
#     /usr/lib/x86_64-linux-gnu
#     /usr/lib64
#     /usr/local/lib
#     /usr/local/lib64
#     /opt/local/lib
#     /opt/local/lib64
#     /opt/csw
#     /usr/local/include/cryptopp
#     /opt)
# find_path(CRYPTOPP_LIBRARY_PATH
#       NAMES libcryptopp.a
#       PATHS ${PROJECT_SOURCE_DIR}
#             ${DEPS_PREFIX}/lib
#             ${DEPS_PREFIX}/lib64
#             ${LIBCRYPTOPP_PATH_HINT}
#     PATH_SUFFIXES cryptopp)

# if (NOT CRYPTOPP_LIBRARY_PATH)
#     message(STATUS "Cryptopp not found!")
#     set(LIBCRYPTOPP crypto)
# else ()
#     message(STATUS "Found cryptopp: ${CRYPTOPP_LIBRARY_PATH}/libcryptopp.a")
#     set(LIBCRYPTOPP ${CRYPTOPP_LIBRARY_PATH}/libcryptopp.a)
#     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lcryptopp")
# endif ()

add_executable(raft_server_test
                 unit/raft_server_test.cxx
                 unit/fake_network.cxx
                 ${EXAMPLES_SRC}/logger.cc
                 ${EXAMPLES_SRC}/in_memory_log_store.cxx)
  add_dependencies(raft_server_test
                   static_lib)
  target_link_libraries(raft_server_test
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        #   ${LIBCRYPTOPP_LIBRARIES}
                          ${LIBRARIES})

# === Failure recovery & conflict resolution test ===
add_executable(failure_test
               unit/failure_test.cxx
               unit/fake_network.cxx
               ${EXAMPLES_SRC}/logger.cc
               ${EXAMPLES_SRC}/in_memory_log_store.cxx)
add_dependencies(failure_test
                 static_lib)
target_link_libraries(failure_test
                        ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        #   ${LIBCRYPTOPP_LIBRARIES}
                          ${LIBRARIES})

# === ASIO network stuff test ===
add_executable(asio_service_test
               unit/asio_service_test.cxx
               ${EXAMPLES_SRC}/logger.cc
               ${EXAMPLES_SRC}/in_memory_log_store.cxx)
add_dependencies(asio_service_test
                 static_lib
                 build_ssl_key)
target_link_libraries(asio_service_test
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        #   ${LIBCRYPTOPP_LIBRARIES}
                          ${LIBRARIES})

                        

# === Benchmark ===

if(Boost_FOUND)
    message(STATUS "tests: Boost found")
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(raft_bench
                bench/raft_bench.cxx
                ${EXAMPLES_SRC}/logger.cc
                ${EXAMPLES_SRC}/in_memory_log_store.cxx)
    add_dependencies(raft_bench
                    static_lib)
    target_link_libraries(raft_bench
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        #   ${LIBCRYPTOPP_LIBRARIES}
                          ${LIBRARIES})
endif()

# === Forensics Test ===
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(raft_forensics_bench
                forensics_test/raft_forensics_bench.cxx
                ${EXAMPLES_SRC}/logger.cc
                ${EXAMPLES_SRC}/in_memory_log_store.cxx)
    target_include_directories(raft_forensics_bench PRIVATE bench)
    add_dependencies(raft_forensics_bench
                    static_lib)
    target_link_libraries(raft_forensics_bench
                          ${Boost_LIBRARIES}
                          ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        #   ${LIBCRYPTOPP_LIBRARIES}
                          ${LIBRARIES})
endif()

# === Other modules ===
add_executable(buffer_test
               unit/buffer_test.cxx)
add_dependencies(buffer_test
                 static_lib)
target_link_libraries(buffer_test
                        ${Boost_LIBRARIES}
                        ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        # ${LIBCRYPTOPP_LIBRARIES}
                        ${LIBRARIES})

add_executable(serialization_test
               unit/serialization_test.cxx)
add_dependencies(serialization_test
                 static_lib)
target_link_libraries(serialization_test
                        ${Boost_LIBRARIES}
                        ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        # ${LIBCRYPTOPP_LIBRARIES}
                        ${LIBRARIES})
 
add_executable(timer_test
               unit/timer_test.cxx)
add_dependencies(timer_test
                 static_lib)
target_link_libraries(timer_test
                        ${Boost_LIBRARIES}
                        ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        # ${LIBCRYPTOPP_LIBRARIES}
                        ${LIBRARIES})

add_executable(strfmt_test
               unit/strfmt_test.cxx)
add_dependencies(strfmt_test
                 static_lib)
target_link_libraries(strfmt_test
                        ${Boost_LIBRARIES}
                        ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        # ${LIBCRYPTOPP_LIBRARIES}
                        ${LIBRARIES})


add_executable(stat_mgr_test
               unit/stat_mgr_test.cxx)
add_dependencies(stat_mgr_test
                 static_lib)
target_link_libraries(stat_mgr_test
                        ${Boost_LIBRARIES}
                        ${BUILD_DIR}/${LIBRARY_OUTPUT_NAME}
                        # ${LIBCRYPTOPP_LIBRARIES}
                        ${LIBRARIES})