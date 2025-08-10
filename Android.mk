LOCAL_PATH := $(call my-dir)

# Common flags and libraries
common_shared_libraries := \
    libutils \
    libcutils \
    liblog \
    libmedia \
    libhardware \
    libbinder \
    libaudioclient

common_header_libraries := \
    libmedia_headers \
    libmediametrics_headers

common_cflags := \
    -Wextra \
    -Wno-unused-parameter \
    -Wall \
    -Werror

include $(CLEAR_VARS)

# audio_test_client module
LOCAL_MODULE := audio_test_client
LOCAL_SRC_FILES := audio_test_client.cpp
LOCAL_SHARED_LIBRARIES := $(common_shared_libraries)
LOCAL_HEADER_LIBRARIES := $(common_header_libraries)
LOCAL_CFLAGS := $(common_cflags)

include $(BUILD_EXECUTABLE)
