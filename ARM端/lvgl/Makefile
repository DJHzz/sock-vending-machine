# ================= Makefile 基础配置 =================
# 使用 WSL 本地的 gcc (模拟器运行环境)
CC = arm-linux-gcc

# 目标可执行文件
BIN = demo

# 编译输出目录
BUILD_DIR = ./build

# LVGL 路径
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= $(shell pwd)

# ================= 编译与链接参数 =================
# 头文件包含路径
CFLAGS ?= -O3 -g0 -I$(LVGL_DIR)/ -I$(LVGL_DIR)/sq_ui -I$(LVGL_DIR)/src -Wall -Wextra
# 链接库 (-lm 数学库, -lpthread 线程库)
LDFLAGS ?= -lm -lpthread

# ================= 源码收集 =================
# 1. 主入口文件
MAINSRC = ./main.c ./simfang.c

# 2. 引入 LVGL 官方编译规则 (会自动收集 lvgl/src 的几千个源文件)
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

# 3. 增加图标和鼠标驱动
CSRCS += $(LVGL_DIR)/mouse_cursor_icon.c

# 4. 扫描你的业务代码 (sq_ui 和 src)
CSRCS += $(shell find $(LVGL_DIR)/sq_ui -name "*.c")
CSRCS += $(shell find $(LVGL_DIR)/src -name "*.c")

# ================= 核心构建规则 =================
# 【核心修改】：保留原有的路径结构，直接在 build 目录下镜像源代码目录
OBJS = $(addprefix $(BUILD_DIR)/, $(CSRCS:.c=.o))
MAINOBJ = $(addprefix $(BUILD_DIR)/, $(MAINSRC:.c=.o))

# 默认编译目标
all: $(BUILD_DIR) $(BIN)

# 创建基础构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 【关键】：如何编译一个 .c 文件为 build 目录里的 .o
# 这里使用目录自动创建策略，确保 build/lvgl/src/xxx 这样的路径被自动建立
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "⚙️ 编译 $< -> $@"
	$(CC) $(CFLAGS) -c $< -o $@

# 链接最终的可执行文件
$(BIN): $(OBJS) $(MAINOBJ)
	@echo "🔗 正在链接生成 $(BIN)..."
	$(CC) -o $@ $^ $(LDFLAGS)

# ================= 清理工作区 =================
.PHONY: clean
clean:
	
	rm -rf $(BUILD_DIR) $(BIN)