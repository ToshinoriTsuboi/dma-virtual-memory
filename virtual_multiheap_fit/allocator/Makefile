CC      = gcc
CFLAGS  = -O3 -I./src -Wall -MMD -MP
CFLAGS += -DNDEBUG
#CFLAGS += -DCOPYLESS
CFLAGS += -I./include
SRC_DIR = ./src
OBJ_DIR = ./obj
LIB_SOURCES = $(shell ls $(SRC_DIR)/*.c) 
LIB_OBJS    = $(subst $(SRC_DIR), $(OBJ_DIR), $(LIB_SOURCES:.c=.o))
LIB_TARGET = virtual_multiheap_fit.a
DEPENDS = $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d)

all: $(LIB_TARGET)

$(LIB_TARGET): $(LIB_OBJS)
	ar rcs $@ $(LIB_OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c 
	@if [ ! -d $(OBJ_DIR) ]; \
		then echo "mkdir -p $(OBJ_DIR)"; mkdir -p $(OBJ_DIR); \
	fi
	$(CC) $(CFLAGS) -o $@ -c $< 

clean:
	$(RM) $(LIB_OBJS) $(LIB_TARGET) $(DEPENDS)

-include $(DEPENDS)

.PHONY: all clean
