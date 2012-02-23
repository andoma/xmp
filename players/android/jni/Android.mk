LOCAL_PATH	:= $(call my-dir)
#LOCAL_ARM_MODE	:= arm

include $(CLEAR_VARS)

include $(LOCAL_PATH)/src/Makefile
include $(LOCAL_PATH)/src/loaders/Makefile
include $(LOCAL_PATH)/src/loaders/prowizard/Makefile

SRC_SOURCES	:= $(addprefix src/,$(SRC_OBJS))
LOADERS_SOURCES := $(addprefix src/loaders/,$(LOADERS_OBJS))
PROWIZ_SOURCES	:= $(addprefix src/loaders/prowizard/,$(PROWIZ_OBJS))

XMP_VERSION	:= `grep ^VERSION $(LOCAL_PATH)/../../../Makefile|sed 's/.* //'`
LOCAL_MODULE    := xmp
LOCAL_CFLAGS	:= -I$(LOCAL_PATH)/src -DVERSION=\"$(XMP_VERSION)\" -O3 -DHAVE_CONFIG_H -I$(LOCAL_PATH) -I$(LOCAL_PATH)/include
LOCAL_LDLIBS	:= -Lbuild/platforms/android-3/arch-arm/usr/lib -llog
LOCAL_SRC_FILES := xmp-jni.c \
	$(SRC_SOURCES:.o=.c) \
	$(LOADERS_SOURCES:.o=.c) \
	$(PROWIZ_SOURCES:.o=.c)

include $(BUILD_SHARED_LIBRARY)
