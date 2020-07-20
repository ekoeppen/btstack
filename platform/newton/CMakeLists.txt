cmake_minimum_required(VERSION 3.10)
project(btstack_newton)
set(BTSTACK "${PROJECT_SOURCE_DIR}/../../src")
set(PORT "${PROJECT_SOURCE_DIR}/../../port/newton")
set(PLAT_NEWTON "${PROJECT_SOURCE_DIR}/../../platform/newton")
add_library(btstack_newton
    ${BTSTACK}/btstack_linked_list.c
    ${BTSTACK}/btstack_memory.c
    ${BTSTACK}/btstack_memory_pool.c
    ${BTSTACK}/btstack_run_loop.c
    ${BTSTACK}/btstack_tlv_none.c
    ${BTSTACK}/btstack_util.c
    ${BTSTACK}/ad_parser.c
    ${BTSTACK}/classic/btstack_link_key_db_static.c
    ${BTSTACK}/hci.c
    ${BTSTACK}/hci_dump.c
    ${BTSTACK}/hci_cmd.c
    ${BTSTACK}/hci_transport_h4.c
    ${PLAT_NEWTON}/btstack_uart_block_newton.c
    ${PLAT_NEWTON}/btstack_run_loop_newton.c)
target_include_directories(btstack_newton PUBLIC ${BTSTACK} ${PORT} ${PLAT_NEWTON})
target_compile_options(btstack_newton PRIVATE
    -march=armv4 -mbig-endian -Wno-unused-function -Wno-multichar
    -fPIC -fdata-sections -ffunction-sections -O2
    -D__arm -DhasPCMCIA -DforARM -DNO_ERRNO_H -DSTDC)
