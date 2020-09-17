Q := @

TOP_DIR := $(shell pwd)
DEMOS_DIR := demos
OUT_DIR := output

CC := arm-linux-gnueabi-gcc

BLD_CFLAGS := $(CFLAGS)
BLD_CFLAGS += -Os

BLD_LDFLAGS := $(LDFLAGS) -lpthread

SRC_DIR := $(shell find . -type d \( ! -name project \))

HDR_DIR := $(SRC_DIR)
BLD_CFLAGS += $(addprefix -I,$(HDR_DIR))

SRC_FILES := $(shell find $(SRC_DIR) -not -path "*/project/*.c" -name "*.c")
OBJ_FILES := $(SRC_FILES:.c=.o)
OBJ_FILES := $(addprefix $(OUT_DIR)/,$(OBJ_FILES))

PROG_TARGET := $(subst _,-,$(patsubst %.c,%,$(wildcard project/*.c)))

all: prepare $(PROG_TARGET)

prepare:
	$(Q)mkdir -p output

$(PROG_TARGET): $(OBJ_FILES)
	$(Q)echo "+ Linking $(OUT_DIR)/$(notdir $@) ..."
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -o $@ \
	    $(patsubst $(OUT_DIR)/%,%,$(addsuffix .c,$(subst $(notdir $@),$(subst -,_,$(notdir $@)),$@))) \
	    $(BLD_CFLAGS) $^ $(BLD_LDFLAGS)

	$(Q)mv $@ $(OUT_DIR)

$(OUT_DIR)/%.o: %.c
	$(Q)echo ": Compiling $< ..."
	$(Q)mkdir -p $(OUT_DIR)/$(dir $<)
	$(Q)$(CC) -o $@ -c $< $(BLD_CFLAGS)

clean:
	$(Q)rm -rf $(OUT_DIR)
