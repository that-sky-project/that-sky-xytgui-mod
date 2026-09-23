MAKEFLAGS += -s -j

DIST_DIR = ./dist
SRC_DIR = ./src

SRC_DIRS = $(SRC_DIR) $(wildcard $(SRC_DIR)/*/)

C_SRC = $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/*/*.c)
CPP_SRC = $(wildcard $(SRC_DIR)/*.cpp $(SRC_DIR)/*/*.cpp)

C_OBJ = $(addprefix $(DIST_DIR)/, $(notdir $(C_SRC:.c=.o)))
CPP_OBJ = $(addprefix $(DIST_DIR)/, $(notdir $(CPP_SRC:.cpp=.o)))

VERSION_SCRIPT = $(SRC_DIR)/exports.txt

TARGET = sky-gui.dll
BIN_TARGET = $(DIST_DIR)/$(TARGET)

CC = gcc
CXX = g++

CFLAGS = -Wall -Wformat -O2 -ffunction-sections -fdata-sections -static -flto=auto -s -Wno-unused-function -Wno-unused-variable
CFLAGS += -I./src -I./include
# HTML SDK + ImGui
CFLAGS += -I../libraries/htmodloader/includes/htmodloader
CFLAGS += -I../libraries/htmodloader/includes/imgui-1.92.2b

LFLAGS = -Wl,--gc-sections,-O3,--as-needed,--version-script=$(VERSION_SCRIPT)
LFLAGS += -L../libraries/htmodloader/lib -lhtmodloader

vpath %.c $(SRC_DIRS)
vpath %.cpp $(SRC_DIRS)

.PHONY: all clean

all: $(DIST_DIR)
	-@$(MAKE) $(BIN_TARGET)

$(BIN_TARGET): $(C_OBJ) $(CPP_OBJ)
	@echo Linking ...
	@$(CXX) --std=c++17 $(CFLAGS) $^ -shared -o $@ $(LFLAGS)
	@echo Done.

$(DIST_DIR)/%.o: %.c
	@echo CC  $< ...
	@$(CC) --std=c11 $(CFLAGS) -c $< -o $@

$(DIST_DIR)/%.o: %.cpp
	@echo CXX $< ...
	@$(CXX) --std=c++17 $(CFLAGS) -c $< -o $@

$(DIST_DIR):
	mkdir dist

clean:
	-@del .\dist\*.o
	-@del .\dist\*.dll
