LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

APP_ABI := $(ABI)
LOCAL_MODULE := novelvm
LOCAL_SRC_FILES := ../libnovelvm.so

include $(PREBUILT_SHARED_LIBRARY)
