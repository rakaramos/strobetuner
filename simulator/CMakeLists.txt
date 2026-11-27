cmake_minimum_required(VERSION 3.16)
project(strobetuner_sim C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Same LVGL version as the Arduino library. Pass -DLVGL_DIR=/path/to/lvgl to
# use a copy you already have (e.g. ~/Documents/Arduino/libraries/lvgl).
set(LVGL_DIR "" CACHE PATH "Existing LVGL source tree (optional)")
if(NOT LVGL_DIR)
  include(FetchContent)
  FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none_)          # fetch only; we compile the sources below
  FetchContent_MakeAvailable(lvgl)
  set(LVGL_DIR ${lvgl_SOURCE_DIR})
endif()

set(SKETCH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../strobetuner_esp32s2/src)
set(CONF_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)            # shared lv_conf.h

find_package(SDL2 REQUIRED)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${CONF_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_SIM_USE_SDL=1)
target_link_libraries(lvgl PUBLIC SDL2::SDL2)
target_compile_options(lvgl PRIVATE -w)

add_executable(strobetuner_sim
  main.c
  ${SKETCH_SRC}/ui/ui.c
  ${SKETCH_SRC}/core/tuner.c)
target_include_directories(strobetuner_sim PRIVATE ${SKETCH_SRC})
target_compile_options(strobetuner_sim PRIVATE -Wall -Wextra)
target_link_libraries(strobetuner_sim PRIVATE lvgl m)

# Strobe timing test: runs the firmware's LED schedule on the host. No LVGL/SDL.
add_executable(strobe_test strobe_test.c ${SKETCH_SRC}/core/tuner.c)
target_include_directories(strobe_test PRIVATE ${SKETCH_SRC})
target_compile_options(strobe_test PRIVATE -Wall -Wextra)
target_link_libraries(strobe_test PRIVATE m)
enable_testing()
add_test(NAME strobe_timing COMMAND strobe_test)
