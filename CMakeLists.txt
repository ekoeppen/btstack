cmake_minimum_required(VERSION 3.10)
project(blunt3k)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
enable_language(ASM)
set(BTSTACK "${PROJECT_SOURCE_DIR}/src")
set(PORT "${PROJECT_SOURCE_DIR}/port/newton")
set(PLAT_NEWTON "${PROJECT_SOURCE_DIR}/platform/newton")
set(NCT_PROJECTS $ENV{NCT_PROJECTS})
add_subdirectory(${PLAT_NEWTON})
add_executable(blunt3k
    ${PORT}/Main.cpp
    ${PORT}/BluntClient.cpp
    ${PORT}/BluntServer.cpp
    ${PORT}/EventsCommands.cpp
    ${PORT}/glue.s
    ${PORT}/log.c
    ${PORT}/hal_newton.c
    ${PORT}/hal_timer_newton.cpp
    ${PORT}/hal_uart_newton.cpp
    ${PORT}/NewtonRuntime/NewtonExceptions.cpp
    ${PORT}/NewtonRuntime/NewtonPrivateAPIs.s
    ${PORT}/NewtonRuntime/NewtonPublicAPIs.s)
target_include_directories(blunt3k PUBLIC ${BTSTACK} ${PORT} ${PLAT_NEWTON}
    ${NCT_PROJECTS}/DDKIncludes
    ${NCT_PROJECTS}/DDKIncludes/Bootstrap
    ${NCT_PROJECTS}/DDKIncludes/CLibrary
    ${NCT_PROJECTS}/DDKIncludes/CommAPI
    ${NCT_PROJECTS}/DDKIncludes/Communications
    ${NCT_PROJECTS}/DDKIncludes/Frames
    ${NCT_PROJECTS}/DDKIncludes/HAL
    ${NCT_PROJECTS}/DDKIncludes/Lantern
    ${NCT_PROJECTS}/DDKIncludes/OS600
    ${NCT_PROJECTS}/DDKIncludes/Packages
    ${NCT_PROJECTS}/DDKIncludes/Power
    ${NCT_PROJECTS}/DDKIncludes/QD
    ${NCT_PROJECTS}/DDKIncludes/Toolbox
    ${NCT_PROJECTS}/DDKIncludes/UtilityClasses
    ${NCT_PROJECTS}/DDKIncludes/PCMCIA
    ${NCT_PROJECTS}/Includes
    ${NCT_PROJECTS}/Includes/CLibrary
    ${NCT_PROJECTS}/Includes/Frames
    ${NCT_PROJECTS}/Includes/Utilities
    ${NCT_PROJECTS}/LanternDriver_DDK/Includes
    ${NCT_PROJECTS}/NSandDDK)
target_compile_options(blunt3k PRIVATE
    -march=armv4 -mbig-endian
    $<$<COMPILE_LANGUAGE:C,CXX>:
        -O2
        -D__arm -DhasPCMCIA -DforARM -DNO_ERRNO_H -DSTDC -DforARM
        -fPIC -fdata-sections -ffunction-sections
        -Wno-multichar -Wno-endif-labels -Wno-builtin-declaration-mismatch -Wno-write-strings>
    $<$<COMPILE_LANGUAGE:C>: -Werror=incompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:CXX>: -fno-exceptions -fno-rtti>)
target_link_options(blunt3k PRIVATE
    -L${PORT}
    -EB -shared --gc-sections
    --no-allow-shlib-undefined --no-export-dynamic --no-undefined -x
    -Tblunt3k.ld --version-script=blunt3k.ver)
target_link_libraries(blunt3k btstack_newton)
add_custom_command(TARGET blunt3k POST_BUILD
    COMMAND rm -f blunt3k.ntkc
    COMMAND ELFtoNTK -via ${PORT}/blunt3k.exp -o blunt3k.ntkc $<TARGET_FILE:blunt3k>)
set_directory_properties(PROPERTIES ADDITIONAL_CLEAN_FILES "blunt3k.ntkc")
