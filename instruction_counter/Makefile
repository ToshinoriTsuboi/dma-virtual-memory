CC      = gcc
CFLAGS  = -Wall -MMD -MP
CFLAGS += -I./include
SRC_DIR = ./src
OBJ_DIR = ./obj
LIB_SOURCES = $(shell ls $(SRC_DIR)/*.c | grep -v 'main.c') 
MAIN_SOURCE = $(SRC_DIR)/main.c
LIB_OBJS    = $(subst $(SRC_DIR),$(OBJ_DIR), $(LIB_SOURCES:.c=.o))
MAIN_OBJ    = $(subst $(SRC_DIR),$(OBJ_DIR), $(MAIN_SOURCE:.c=.o))
LIB_TARGET  = inst_counter.a
MAIN_TARGET = inst_counter.out
DEPENDS = $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d)

all: $(LIB_TARGET) $(MAIN_TARGET)

$(MAIN_TARGET) : $(MAIN_OBJ)
	$(CC) -o $@ $(MAIN_OBJ)

$(LIB_TARGET): $(LIB_OBJS)
	ar rcs $@ $(LIB_OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c 
	@if [ ! -d $(OBJ_DIR) ]; \
		then echo "mkdir -p $(OBJ_DIR)"; mkdir -p $(OBJ_DIR); \
	fi
	$(CC) $(CFLAGS) -o $@ -c $< 

clean:
	$(RM) $(LIB_OBJS) $(MAIN_OBJ) $(LIB_TARGET) $(MAIN_TARGET) $(DEPENDS)

-include $(DEPENDS)

.PHONY: all clean
