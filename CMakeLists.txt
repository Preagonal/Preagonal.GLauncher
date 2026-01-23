cmake_minimum_required(VERSION 3.20)
project(GLauncher)

# Enable MASM assembler
enable_language(ASM_MASM)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/bin)
endforeach()

if(WIN32)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /RELEASE")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /RELEASE /INCREMENTAL:NO")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /RELEASE /INCREMENTAL:NO")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    add_subdirectory(../GWauncher/dependencies/detours detours)
    set(STATIC ON CACHE BOOL "Build static library" FORCE)
    add_subdirectory(../gs2-parser gs2-parser)
    include_directories(../GWauncher/dependencies/detours/lib/src ../gs2-parser/src)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_library(glauncher SHARED
            src/win/glauncherw.cpp
            src/win/defs.h
            src/win/Utils.hpp
            src/win/Memory.hpp
            src/win/pch.h
            src/win/pch.cpp
            src/win/framework.h
            src/win/resources.rc
        )
        set_target_properties(glauncher PROPERTIES
            OUTPUT_NAME "GLauncherW"
            PREFIX ""
            LINK_FLAGS "/INCREMENTAL:NO /NOEXP /IMPLIB:NONE"
        )
        target_precompile_headers(glauncher PRIVATE src/win/pch.h)
        target_link_libraries(glauncher lib_detours gs2compiler)
        add_library(dxgi SHARED src/win/dxgi_proxy.cpp src/win/dxgi.def src/win/resources.rc)
        set_target_properties(dxgi PROPERTIES
            OUTPUT_NAME "dxgi"
            PREFIX ""
            LINK_FLAGS "/INCREMENTAL:NO /NOEXP /IMPLIB:NONE"
        )
        target_link_libraries(dxgi lib_detours)
        add_library(version SHARED src/win/version_proxy.cpp src/win/version.def src/win/resources.rc)
        set_target_properties(version PROPERTIES
            OUTPUT_NAME "version"
            PREFIX ""
            LINK_FLAGS "/INCREMENTAL:NO /NOEXP /IMPLIB:NONE"
        )
        target_link_libraries(version lib_detours version.lib)
    else()
        add_library(version SHARED src/win/version_proxy.cpp src/win/version.def src/win/resources.rc)
        set_target_properties(version PROPERTIES
            OUTPUT_NAME "version"
            PREFIX ""
        )
        target_link_libraries(version lib_detours version.lib)
    endif()
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    add_library(glauncher SHARED
        src/linux/glauncher.c
    )
    target_include_directories(glauncher PRIVATE ${GTK3_INCLUDE_DIRS} src/linux)
    target_compile_options(glauncher PRIVATE -std=c99 -D_GNU_SOURCE)
    target_link_libraries(glauncher ${GTK3_LIBRARIES} dl pthread X11)
    set_target_properties(glauncher PROPERTIES
        OUTPUT_NAME "glauncher"
        PREFIX ""
    )

endif()
