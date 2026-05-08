
include_directories(${CMAKE_CURRENT_LIST_DIR}/..)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../../urma/common/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include/framework)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../util)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/core)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/core/channel)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/core/queue)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/core/queue/jetty)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/lib)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/lib/contorl)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/lib/contorl/crypto)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/lib/datapath)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/lib/manager)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../framework/protocol)

file(GLOB PERFTEST_COMMON_FILES
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_thread.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_latency.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_qps.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_util.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_common.c"
)

add_executable(urpc_framework_perftest
    ${PERFTEST_COMMON_FILES}
    urpc_lib_perftest_allocator.c
    urpc_lib_perftest_latency.c
    urpc_lib_perftest_param.c
    urpc_lib_perftest_qps.c
    urpc_lib_perftest.c)
set_property(TARGET urpc_framework_perftest PROPERTY C_STANDARD 11)
target_link_libraries(urpc_framework_perftest urpc urma urma_common pthread m)
install(TARGETS urpc_framework_perftest DESTINATION /usr/bin)