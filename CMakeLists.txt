cmake_minimum_required(VERSION 3.10)

# Project name and version
project(audio_test_client VERSION 1.0)

# Set C++ standard (equivalent to LOCAL_CPPFLAGS := -std=c++17 in Android.mk)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common flags from Android.mk (COMMON_CFLAGS)
set(COMMON_CFLAGS 
    -Wextra
    -Wno-unused-parameter
    -Wall
    -Werror
)

# C++ specific flags
set(COMMON_CPPFLAGS 
    -fexceptions
)

# Shared libraries from Android.mk (COMMON_SHARED_LIBRARIES)
set(COMMON_SHARED_LIBRARIES
    utils
    cutils
    log
    media
    hardware
    binder
    audioclient
)

# Header libraries from Android.mk (COMMON_HEADER_LIBRARIES)
set(COMMON_HEADER_LIBRARIES
    media_headers
    mediametrics_headers
)

# Apply compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_CFLAGS} ${COMMON_CPPFLAGS}")

# Source files
set(SOURCES
    audio_test_client.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories for header libraries
target_include_directories(${PROJECT_NAME} PRIVATE
    # Add paths to header libraries here if needed
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${COMMON_SHARED_LIBRARIES})

# Define _INTERFACE_V2_ to match Android.mk behavior
# In Android.mk, this is conditionally defined for platform versions 14, 15, and 16
add_definitions(-D_INTERFACE_V2_)

# Install target
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)