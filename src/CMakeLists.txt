cmake_minimum_required(VERSION 3.10)
# The version is written once, in version.h; src/makefile reads the same line.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.h" CPMEMU_VERSION_LINE
     REGEX "CPMEMU_VERSION \"")
string(REGEX REPLACE ".*CPMEMU_VERSION \"([^\"]*)\".*" "\\1"
       CPMEMU_VERSION "${CPMEMU_VERSION_LINE}")
if(NOT CPMEMU_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "could not read CPMEMU_VERSION from src/version.h")
endif()
project(cpmemu VERSION ${CPMEMU_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library sources
set(LIB_SOURCES
    qkz80.cc
    qkz80_errors.cc
    qkz80_mem.cc
    qkz80_reg_set.cc
)

# Application sources
set(APP_SOURCES
    cpmemu.cc
    gdbstub.cc
)

# Platform-specific source
if(WIN32)
    set(PLATFORM_SOURCE os/windows/platform.cc)
else()
    set(PLATFORM_SOURCE os/linux/platform.cc)
endif()

# Create static library
add_library(qkz80 STATIC ${LIB_SOURCES})
target_include_directories(qkz80 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Create executable
add_executable(cpmemu ${APP_SOURCES} ${PLATFORM_SOURCE})
target_link_libraries(cpmemu PRIVATE qkz80)
# Winsock, for the GDB stub in gdbstub.cc and os/windows/platform.cc
if(WIN32)
    target_link_libraries(cpmemu PRIVATE ws2_32)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(cpmemu PRIVATE /W4)
    target_compile_options(qkz80 PRIVATE /W4)
else()
    target_compile_options(cpmemu PRIVATE -Wall -Wextra)
    target_compile_options(qkz80 PRIVATE -Wall -Wextra)
endif()

# Installation
install(TARGETS cpmemu RUNTIME DESTINATION bin)
install(TARGETS qkz80 ARCHIVE DESTINATION lib)
# LICENSE, and NOTICE with the BSD license of the tnylpo code in the CPU core,
# which a binary distribution has to reproduce.  The macOS release archive is
# built from these rules.  The archives of 4.15.0 and earlier had neither file.
install(FILES ../LICENSE ../NOTICE DESTINATION share/doc/cpmemu)
install(FILES
    qkz80.h
    qkz80_cpu_flags.h
    qkz80_mem.h
    qkz80_reg_pair.h
    qkz80_reg_set.h
    qkz80_trace.h
    qkz80_types.h
    DESTINATION include/qkz80
)

# cpm_disk, the CP/M disk-image tool.  src/makefile's `install:` target has
# always installed ../util/cpm_disk.py as $(BINDIR)/cpm_disk; this is the same
# file under the same name, so the macOS archive - which cpack builds by
# re-running these rules rather than by staging files by hand - carries the
# same two programs a build from source installs.
#
# PROGRAMS and not FILES, because FILES installs 0644 and this file has a
# shebang.  Not on Windows, for the same reason packaging/windows does not
# carry it: there is no interpreter in the package and nothing to declare a
# dependency on one.
if(NOT WIN32)
    install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../util/cpm_disk.py
            DESTINATION bin
            RENAME cpm_disk)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../man/cpmemu.1
                  ${CMAKE_CURRENT_SOURCE_DIR}/../man/cpm_disk.1
            DESTINATION share/man/man1)
endif()

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "cpmemu")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CP/M 2.2 Emulator")
set(CPACK_PACKAGE_VENDOR "cpmemu")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# The archive name has to carry the architecture.  Left unset,
# CPACK_PACKAGE_FILE_NAME defaults to name-version-system, which is
# cpmemu-4.6.0-Linux.tar.gz on amd64 and cpmemu-4.6.0-Linux.tar.gz on arm64
# as well: two different binaries under one filename, so whichever is attached
# to a release second replaces the first and a user downloads a binary that
# cannot run.  Verified with cpack before and after on linux x86_64.
#
# CMAKE_SYSTEM_PROCESSOR is x86_64 or aarch64 on linux and arm64 or x86_64 on
# macOS.  A macOS build asked for more than one architecture is one universal
# binary, so it is named for all of them rather than for the host.
#
# Measured on macOS 27 arm64: a plain build leaves CMAKE_OSX_ARCHITECTURES
# empty and cpack names the archive cpmemu-4.6.0-Darwin-arm64.tar.gz, and
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" names it
# cpmemu-4.6.0-Darwin-arm64-x86_64.tar.gz.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "-" CPACK_TARGET_ARCH "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(CPACK_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT CPACK_TARGET_ARCH)
    set(CPACK_TARGET_ARCH "unknown")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CPACK_TARGET_ARCH}")

include(CPack)
