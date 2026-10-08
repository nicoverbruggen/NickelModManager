cmake_minimum_required(VERSION 3.13)
project(nickelmodmanager LANGUAGES CXX)
# The version shown on the About page. A release passes its tag's version;
# tools/build.sh passes `git describe` for other builds.
set(NICKELMODMANAGER_VERSION "dev" CACHE STRING "Version shown on the About page")
configure_file(cmake/project_info.h.in project_info.h @ONLY)
# Firmware 4.x ships Qt 5.2.1; firmware 5.x and 6.x ship Qt 6.5.
set(NICKELMODMANAGER_QT 6 CACHE STRING "Qt major version to build against: 5 or 6")
if(NOT NICKELMODMANAGER_QT MATCHES "^[56]$")
    message(FATAL_ERROR "NICKELMODMANAGER_QT must be 5 or 6")
endif()
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_AUTOMOC ON)
# Several mods share one process. Prevent classes from binding to another mod.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(QT Qt${NICKELMODMANAGER_QT})
find_package(${QT} REQUIRED COMPONENTS Core)
set(WARNINGS -Wall -Wextra -Werror)
include(CTest)
option(NICKELMODMANAGER_PLUGINS "Build the NickelModManager plugin" ON)
option(NICKELMODMANAGER_PROBES "Build the test mod plugins" OFF)

function(embed_text input output symbol)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input}"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${output}" "-DSYMBOL=${symbol}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_text.cmake"
        DEPENDS "${input}" cmake/embed_text.cmake
        VERBATIM)
endfunction()
embed_text(src/updates/restore-hook.sh restore_hook.h restoreHook)
add_library(nmmstore STATIC
    src/mods/mod.cpp src/mods/files.cpp src/mods/persistence.cpp src/mods/store.cpp
    src/entrypoint/lifecycle.cpp
    src/updates/hook.cpp src/updates/package.cpp "${CMAKE_CURRENT_BINARY_DIR}/restore_hook.h")
target_link_libraries(nmmstore PUBLIC ${QT}::Core ${CMAKE_DL_LIBS})
target_include_directories(nmmstore PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(nmmstore PRIVATE ${WARNINGS})

if(BUILD_TESTING)
    foreach(check mods persistence hook hook_script lifecycle)
        add_executable(nmm_${check}_test tests/${check}_test.cpp)
        target_link_libraries(nmm_${check}_test PRIVATE nmmstore)
        target_compile_options(nmm_${check}_test PRIVATE ${WARNINGS})
        add_test(NAME ${check} COMMAND nmm_${check}_test)
    endforeach()
    set_tests_properties(hook_script PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(NICKELMODMANAGER_PLUGINS OR NICKELMODMANAGER_PROBES)
    if(NICKELMODMANAGER_QT EQUAL 5 AND NOT TARGET Qt5::Gui_GL)
        # The Qt5 SDK asks for libGL, which its sysroot does not have.
        add_library(Qt5::Gui_GL INTERFACE IMPORTED)
    endif()
    find_package(${QT} REQUIRED COMPONENTS Gui Widgets)
endif()
if(NICKELMODMANAGER_PLUGINS)
    embed_text(src/ui/icons/LICENSE icon_notice.h lucideNotice)
    add_library(nmmui STATIC src/ui/icons.cpp src/ui/fonts.cpp src/ui/controls.cpp src/ui/pagination.cpp
        src/ui/dialogs.cpp src/ui/menu.cpp src/ui/manager.cpp "${CMAKE_CURRENT_BINARY_DIR}/icon_notice.h")
    target_include_directories(nmmui PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(nmmui PUBLIC ${QT}::Widgets ${QT}::Gui)
    target_compile_options(nmmui PRIVATE ${WARNINGS})
    add_library(nickelmodmanager MODULE src/entrypoint/plugin.cpp src/entrypoint/plugin.json
        src/entrypoint/startup.cpp src/controller.cpp src/nickel.cpp)
    set_target_properties(nickelmodmanager PROPERTIES OUTPUT_NAME nickelmm)
    target_link_libraries(nickelmodmanager PRIVATE nmmstore nmmui)
    target_compile_options(nickelmodmanager PRIVATE ${WARNINGS})
    if(BUILD_TESTING)
        add_executable(nmm_ui_test tests/ui_test.cpp)
        target_link_libraries(nmm_ui_test PRIVATE nmmui)
        target_compile_options(nmm_ui_test PRIVATE ${WARNINGS})
        add_test(NAME ui COMMAND nmm_ui_test)
        set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
if(NICKELMODMANAGER_PROBES)
    foreach(probe a b)
        # Qt loads one plugin per key, so each probe needs its own.
        set(PROBE_NAME ${probe})
        configure_file(probe/probe.json.in probe-${probe}/probe.json @ONLY)
        add_library(nmm-probe-${probe} MODULE probe/probe.cpp "${CMAKE_CURRENT_BINARY_DIR}/probe-${probe}/probe.json")
        target_include_directories(nmm-probe-${probe} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/probe-${probe}")
        target_compile_definitions(nmm-probe-${probe} PRIVATE PROBE_NAME="${probe}")
        target_link_libraries(nmm-probe-${probe} PRIVATE ${QT}::Gui ${CMAKE_DL_LIBS})
        target_compile_options(nmm-probe-${probe} PRIVATE ${WARNINGS})
    endforeach()
endif()
