cmake_minimum_required(VERSION 3.20)
project(GLauncher)

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
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    add_subdirectory(../GWauncher/dependencies/detours detours)
    include_directories(../GWauncher/dependencies/detours/lib/src)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_library(dxgi SHARED src/win/dxgi_proxy.cpp src/win/dxgi.def src/win/resources.rc)
        set_target_properties(dxgi PROPERTIES
            OUTPUT_NAME "dxgi"
            PREFIX ""
        )
        target_link_libraries(dxgi lib_detours)
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




