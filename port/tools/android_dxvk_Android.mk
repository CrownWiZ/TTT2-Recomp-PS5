LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dxvk_dxgi
LOCAL_SRC_FILES := prebuilt/libdxvk_dxgi.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := dxvk_d3d11
LOCAL_SRC_FILES := prebuilt/libdxvk_d3d11.so
LOCAL_SHARED_LIBRARIES := dxvk_dxgi
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_SRC_FILES := android_dxvk_present.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../SDL/include \
    $(LOCAL_PATH)/prebuilt/include \
    $(LOCAL_PATH)/prebuilt/include/native \
    $(LOCAL_PATH)/prebuilt/include/native/windows \
    $(LOCAL_PATH)/prebuilt/include/native/directx
LOCAL_CPPFLAGS := -std=c++17 -DDXVK_WSI_SDL3=1
LOCAL_SHARED_LIBRARIES := SDL3 dxvk_d3d11 dxvk_dxgi
LOCAL_LDLIBS := -lvulkan -llog -landroid
include $(BUILD_SHARED_LIBRARY)
