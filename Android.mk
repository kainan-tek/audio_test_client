LOCAL_PATH := $(call my-dir)

#######################################
# Audio Test Client Configuration
#######################################

# Common compiler flags
COMMON_CFLAGS := \
    -Wextra \
    -Wno-unused-parameter \
    -Wall \
    -Werror

# C++ specific flags
COMMON_CPPFLAGS := \
    -std=c++17 \
    -fexceptions

# Shared libraries required by the application
COMMON_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    liblog \
    libmedia \
    libhardware \
    libbinder \
    libaudioclient \
    libmedia_helper

# Header libraries required by the application
COMMON_HEADER_LIBRARIES := \
    libmedia_headers \
    libmediametrics_headers \
    libmedia_helper_headers

#######################################
# Module Definition
#######################################

include $(CLEAR_VARS)

# Module name
LOCAL_MODULE := audio_test_client

# Source files
LOCAL_SRC_FILES := audio_test_client.cpp

# Library dependencies
LOCAL_SHARED_LIBRARIES := $(COMMON_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES := $(COMMON_HEADER_LIBRARIES)

# Compiler flags
LOCAL_CFLAGS := $(COMMON_CFLAGS)

# C++ standard and specific flags
LOCAL_CPPFLAGS := $(COMMON_CPPFLAGS)

# Platform-specific definitions
# Android 14+ removed callback parameters from AudioRecord/AudioTrack constructors
ifneq ($(filter 14 15 16,$(PLATFORM_VERSION)),)
    $(info PLATFORM_VERSION is $(PLATFORM_VERSION))
    LOCAL_CFLAGS += -DANDROID_API_14_PLUS
endif

# Build as executable
include $(BUILD_EXECUTABLE)