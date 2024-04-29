CUR_PATH		:= $(shell pwd)
HOME_PATH		:= $(CUR_PATH)/../..
SRC_PATH		:= $(CUR_PATH)/source
OUT_PATH		:= $(HOME_PATH)/msp/out
OUT_BIN			:= $(OUT_PATH)/bin/IPCDemo
LIB_PATH		:= $(OUT_PATH)/lib
BIN_PATH		:= $(CUR_PATH)/bin
COMPONENTS_PATH	:= $(CUR_PATH)/../components
SSL_PATH        := $(HOME_PATH)/third-party/openssl
TINYXML_PATH    := $(HOME_PATH)/third-party/tinyxml2
JSONCPP_PATH    := $(HOME_PATH)/third-party/jsoncpp
# added by Yang
CV_PATH			:= $(HOME_PATH)/third-party/opencv-arm-linux

ROOTFS_TARGET_PATH := $(OUT_PATH)

include $(HOME_PATH)/build/config.mak

MOD_NAME 		= ipc_demo
OUTPUT 			:= .obj

# source
SRCCPPS			+=	$(wildcard $(SRC_PATH)/*.cpp) \
					$(wildcard $(SRC_PATH)/utils/*.cpp) \
					$(wildcard $(SRC_PATH)/utils/OsdHandler/*.cpp) \
					$(wildcard $(SRC_PATH)/camera/*.cpp) \
					$(wildcard $(SRC_PATH)/components/*.cpp) \
					$(wildcard $(SRC_PATH)/sensor/*.cpp) \
					$(wildcard $(SRC_PATH)/stages/*.cpp) \
					$(wildcard $(SRC_PATH)/venc/*.cpp) \
					$(wildcard $(SRC_PATH)/jenc/*.cpp) \
					$(wildcard $(SRC_PATH)/rtsp/*.cpp) \
					$(wildcard $(SRC_PATH)/webserver/*.cpp) \
					$(wildcard $(SRC_PATH)/mp4enc/*.cpp) \
					$(wildcard $(SRC_PATH)/detector/*.cpp) \
					$(wildcard $(SRC_PATH)/capture/*.cpp) \
					$(wildcard $(SRC_PATH)/hotbalance/*.cpp) \
					$(wildcard $(SRC_PATH)/osd/*.cpp)

SRCS			+= $(wildcard $(SRC_PATH)/utils/*.c)

OBJS			:= $(SRCCPPS:%.cpp=$(OUTPUT)/%.o) \
				   $(SRCS:%.c=$(OUTPUT)/%.o)

DEPS			:= $(OBJS:%.o=%.d)

# include
CINCLUDE 		:= 	-I$(SRC_PATH) \
					-I$(SRC_PATH)/include \
					-I$(SRC_PATH)/utils \
					-I$(SRC_PATH)/camera \
					-I$(SSL_PATH)/include \
					-I$(TINYXML_PATH)/include \
					-I$(OUT_PATH)/include \
					-I$(SRC_PATH)/components \
					-I$(SRC_PATH)/sensor \
					-I$(SRC_PATH)/stages \
					-I$(SRC_PATH)/venc \
					-I$(SRC_PATH)/jenc \
					-I$(SRC_PATH)/rtsp \
					-I$(SRC_PATH)/webserver \
					-I$(SRC_PATH)/include/BasicUsageEnvironment \
					-I$(SRC_PATH)/include/groupsock \
					-I$(SRC_PATH)/include/liveMedia \
					-I$(SRC_PATH)/include/UsageEnvironment \
					-I$(SRC_PATH)/include/appweb \
					-I$(SRC_PATH)/include/ffmpeg \
					-I$(SRC_PATH)/mp4enc \
					-I$(SRC_PATH)/detector \
					-I$(SRC_PATH)/capture \
					-I$(SRC_PATH)/hotbalance \
					-I$(SRC_PATH)/osd \
					-I$(SRC_PATH)/utils/OsdHandler \
					-I$(SRC_PATH)/utils/OsdHandler/freetype \
					-I$(OUT_PATH)/include/ai_kit \
					-I$(CV_PATH)/include/opencv4
# added by Yang	:opencv库

# exec
TARGET			:= $(BIN_PATH)/$(MOD_NAME)


# build flags
CFLAGS			:= --std=c++11 -fstack-protector -Wall -Wl,-rpath-link=$(LIB_PATH):$(BIN_PATH)/lib:$(SRC_PATH)/lib:$(JSONCPP_PATH)/lib:$(TINYXML_PATH)/lib:$(CV_PATH)/lib:$(CV_PATH)/lib/opencv4/3rdparty
# 													   -Wl,-rpath-link=…………：这是一个链接选项（通过-Wl,前缀传递给链接器），指定在运行时解析动态库的路径。
# CFLAGS变量通常用于指定编译器在编译源代码文件时应该使用的选项。这包括优化级别、警告级别、语言标准等。在这里的CFLAGS还包含用于链接器的选项

ifeq ($(debug),yes)
CFLAGS			+= -O0 -ggdb3
else
CFLAGS			+= -O2
endif

ifeq ($(mtrace),yes)
CFLAGS			+= -DAX_MEM_CHECK
endif

# AX_SIMPLIFIED_MEM_VER
ifeq ($(findstring AX620U, $(PROJECT)), AX620U)
	CFLAGS			+= -DAX_SIMPLIFIED_MEM_VER
	ifeq ($(PROJECT), AX620U_38board)
		CFLAGS			+= -DAX_620U_38BOARD_VER
	endif
endif


# CFLAGS			+= -DTUNING_CTRL

CFLAGS          += -DAPP_BUILD_VERSION=\"$(SDK_VERSION)\"

# dependency
# CLIB变量专门用于管理和指定链接阶段所需的库。这包括指定要链接的具体库（通过-l选项）和库的搜索路径（通过-L选项）。CLIB提供了一个集中的方式来列出所有项目依赖的库，使得在链接阶段可以很容易地添加或移除库依赖
CLIB			+= -L$(LIB_PATH)

CLIB			+= -lax_sys
CLIB			+= -lax_3a
CLIB			+= -lax_proton
CLIB			+= -lax_mipi
CLIB			+= -lai_kit_release
CLIB			+= -lax_venc
CLIB			+= -lax_ivps
CLIB			+= -lax_ives
CLIB			+= -lax_skel
CLIB			+= -lax_interpreter_external
CLIB			+= -lrt
CLIB			+= -lm
CLIB			+= -ldl
CLIB			+= -lpthread
CLIB			+= -L$(BIN_PATH)/lib -L$(SRC_PATH)/lib -L$(SSL_PATH)/lib -lssl -lcrypto -lappweb -lmpr -lhttp -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment -lmp4v2
CLIB			+= -lstdc++
CLIB			+= -laxsyslog
CLIB			+= -lax_nt_stream
CLIB			+= -lax_nt_ctrl
CLIB            += -l:libfreetype.a
CLIB			+= -llens_dciris
CLIB			+= -L$(CV_PATH)/lib -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio
CLIB			+= -L$(CV_PATH)/lib/opencv4/3rdparty -littnotify -llibjpeg-turbo -llibopenjp2 -llibpng -llibtiff -llibwebp -lzlib
# opencv added by Yang

# link
LINK = $(CPP)

include $(HOME_PATH)/build/rules.mak


install:
	$(VERB) $(MKDIR) $(OUT_BIN)
	$(VERB) $(CP) $(BIN_PATH)/* $(OUT_BIN) -R
ifeq ($(findstring _nand, $(PROJECT)), _nand)
	@sed -i 's/mmcblk1p1/mmcblk0p1/g' $(OUT_BIN)/run.sh
	@sed -i 's/mmcblk1p1/mmcblk0p1/g' $(OUT_BIN)/gdb.sh
endif
