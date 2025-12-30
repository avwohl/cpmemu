cmake_minimum_required(VERSION 3.10)
project(cpmemu VERSION 1.0.0 LANGUAGES CXX)

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

include(CPack)
