# BESM-6 simulator, built on the SIMH framework.
#
# One target, no options worth a GUI: cmake -B build && cmake --build build

cmake_minimum_required(VERSION 3.20)

project(simbesm LANGUAGES C)


option(SIMBESM_WERROR "Treat warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- sources ----------------------------------------------------------------

# The SIMH framework, cut to what BESM-6 reaches. sim_tmxr is besm6_tty's
# telnet console and sim_sock is sim_tmxr's own. The rest of upstream's
# framework -- disk, tape, ether, card, imd, video, scsi, serial,
# frontpanel -- is gone from this tree.
set(SIM_SOURCES
    simh/scp.c
    simh/sim_console.c
    simh/sim_sock.c
    simh/sim_timer.c
    simh/sim_tmxr.c)

set(BESM6_SOURCES
    besm6/besm6_arith.c
    besm6/besm6_cpu.c
    besm6/besm6_disk.c
    besm6/besm6_drum.c
    besm6/besm6_gost.c
    besm6/besm6_main.c
    besm6/besm6_mmu.c
    besm6/besm6_sys.c
    besm6/besm6_trace.c
    besm6/besm6_tty.c)

add_executable(besm6 ${SIM_SOURCES} ${BESM6_SOURCES})

target_include_directories(besm6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/simh
                                         ${CMAKE_CURRENT_SOURCE_DIR}/besm6)

# C99 with GNU extensions: upstream compiles -std=gnu99 -U__STRICT_ANSI__.
set_target_properties(besm6 PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

# --- host features ----------------------------------------------------------

target_compile_options(besm6 PRIVATE -Wall)

if(SIMBESM_WERROR)
    target_compile_options(besm6 PRIVATE -Werror)
endif()

# --- libraries --------------------------------------------------------------

find_library(M_LIB m)
if(M_LIB)
    target_link_libraries(besm6 PRIVATE ${M_LIB})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(besm6 PRIVATE rt)
endif()

# --- tests ------------------------------------------------------------------

# The simulator drives the console, so the test needs a pty: expect spawns it,
# types at the Unix shell and checks what comes back.
enable_testing()
find_program(EXPECT_PROG expect)
if(EXPECT_PROG)
    add_test(NAME unix
             COMMAND ${EXPECT_PROG} -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/unix.exp)
    set_tests_properties(unix PROPERTIES
        FAIL_REGULAR_EXPRESSION "FAIL"
        TIMEOUT 300)
else()
    message(STATUS "expect not found -- the unix boot test will not be run")
endif()
