cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME USB-Audio-Player)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# LVGL is kept as a pinned submodule.  Its configuration belongs to the
# firmware, not the library checkout, so CubeMX regeneration cannot disturb it.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/lv_conf.h" CACHE PATH
    "LVGL configuration file" FORCE)
# A prior local configure may have selected the directory-based LVGL option.
# v9 accepts one configuration mechanism only; use the explicit pinned file.
set(LV_BUILD_CONF_DIR "" CACHE PATH "LVGL configuration directory" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Build LVGL's unused ThorVG renderer" FORCE)
add_subdirectory(Middlewares/Third_Party/lvgl)
# LVGL v9.3's generic CMake glob includes optional architecture-specific
# assembly sources.  Cortex-M4 has neither Helium nor Neon; remove those
# sources rather than attempting to feed their C preprocessor headers to gas.
get_target_property(LVGL_SOURCES lvgl SOURCES)
list(FILTER LVGL_SOURCES EXCLUDE REGEX "\\.S$")
set_property(TARGET lvgl PROPERTY SOURCES "${LVGL_SOURCES}")

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/app.c
    Core/Src/audio_player.c
    Core/Src/codec_cs43l22.c
    Core/Src/encoder.c
    Core/Src/lvgl_port.c
    Core/Src/ui.c
    Core/Src/wav.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    lvgl

    # Add user defined libraries
)
