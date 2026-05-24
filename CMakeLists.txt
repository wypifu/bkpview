cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(bkpview CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- locate bkp ----------
# Resolution order:
#   1. -DBKP_ROOT=<path> on the cmake command line
#   2. BKP_ROOT environment variable
#   3. sibling directory ../bkp  (works when bkp and bkpview share the same parent)
if(NOT BKP_ROOT)
    if(DEFINED ENV{BKP_ROOT})
        set(BKP_ROOT "$ENV{BKP_ROOT}")
    else()
        get_filename_component(BKP_ROOT "${CMAKE_SOURCE_DIR}/../bkp" ABSOLUTE)
    endif()
endif()
message(STATUS "BKP_ROOT : ${BKP_ROOT}")

# -DLINK_MODE=static  → libblukpastStatic.a
# -DLINK_MODE=dynamic → libblukpast.so  (default)
if(NOT DEFINED LINK_MODE)
    set(LINK_MODE dynamic)
endif()
string(TOLOWER "${LINK_MODE}" LINK_MODE_LOWER)
message(STATUS "LINK_MODE: ${LINK_MODE_LOWER}")

unset(BKP_LIB CACHE)
if(WIN32 OR LINK_MODE_LOWER MATCHES "static")
    find_library(BKP_LIB
        NAMES blukpastStatic
        HINTS "${BKP_ROOT}/build" "${BKP_ROOT}/build/Release" "${BKP_ROOT}/build/Debug"
        REQUIRED
    )
else()
    find_library(BKP_LIB
        NAMES blukpast
        HINTS "${BKP_ROOT}/build" "${BKP_ROOT}/build/Release" "${BKP_ROOT}/build/Debug"
        REQUIRED
    )
    get_filename_component(BKP_LIB_DIR "${BKP_LIB}" DIRECTORY)
    set(CMAKE_BUILD_RPATH "${BKP_LIB_DIR}")
endif()
message(STATUS "BKP_LIB  : ${BKP_LIB}")

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)

# ---------- compile flags ----------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    # cl.exe or clang-cl.exe (MSVC-compatible driver)
    set(CMAKE_CXX_FLAGS_DEBUG   "/W3 /Od /Zi /D_CRT_SECURE_NO_WARNINGS /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/W3 /O2 /D_CRT_SECURE_NO_WARNINGS")
else()
    # GCC or clang GCC driver (Linux and Windows)
    set(CMAKE_CXX_FLAGS_DEBUG   "-Wall -O0 -g -msse4.1")
    set(CMAKE_CXX_FLAGS_RELEASE "-Wall -O3 -msse4.1")
    if(WIN32)
        string(APPEND CMAKE_CXX_FLAGS_DEBUG   " -D_CRT_SECURE_NO_WARNINGS -DDEBUG")
        string(APPEND CMAKE_CXX_FLAGS_RELEASE " -D_CRT_SECURE_NO_WARNINGS")
    endif()
endif()

# ---------- includes ----------
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${BKP_ROOT}
    ${BKP_ROOT}/thirdparty
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
    ${Vulkan_INCLUDE_DIRS}
)

if(WIN32)
    if(DEFINED ENV{GLFW})
        include_directories($ENV{GLFW}/include)
        link_directories($ENV{GLFW}/lib-vc2019)
    else()
        message(FATAL_ERROR "GLFW environment variable not set. Set it to your GLFW SDK root (e.g. C:/glfw-3.4.x.WIN64).")
    endif()
endif()

# ---------- sources ----------
set(IMGUI_SRC
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_vulkan.cpp
)

set(APP_SRC
    src/main.cpp
    src/app.cpp
    src/camera.cpp
    src/renderer.cpp
    src/animator.cpp
    src/dialog.cpp
)

add_executable(bkpview ${APP_SRC} ${IMGUI_SRC})


if(WIN32)
    target_link_libraries(bkpview ${BKP_LIB} ${Vulkan_LIBRARIES} glfw3 winmm comdlg32)
else()
    target_link_libraries(bkpview ${BKP_LIB} ${Vulkan_LIBRARIES} glfw pthread m)
endif()

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(bkpview PRIVATE -Wl,/SUBSYSTEM:WINDOWS,/ENTRY:mainCRTStartup)
endif()

# ---------- shaders ----------
set(SHADER_DIR    ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTDIR ${CMAKE_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SHADER_OUTDIR})

set(SHADER_FILES pbr.vert pbr.frag pbr_skin.vert grid.vert grid.frag plan.frag shadow.vert shadow_skin.vert shadow.frag aabb.vert aabb.frag)
set(SHADER_SPVS "")

foreach(S ${SHADER_FILES})
    set(IN  ${SHADER_DIR}/${S})
    set(OUT ${SHADER_OUTDIR}/${S}.spv)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC} ${IN} -o ${OUT}
        DEPENDS ${IN}
        COMMENT "glslc ${S}"
    )
    list(APPEND SHADER_SPVS ${OUT})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPVS})
add_dependencies(bkpview shaders)

include(GNUInstallDirs)

if(DEFINED BKPVIEW_SHADER_DIR)
    target_compile_definitions(bkpview PRIVATE BKPVIEW_SHADER_DIR="${BKPVIEW_SHADER_DIR}")
endif()

install(TARGETS bkpview
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bkpview/shaders
    FILES_MATCHING PATTERN "*.spv"
)
