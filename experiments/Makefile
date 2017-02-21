CC      = gcc
CFLAGS  = -O3 -I./src -Wall
CFLAGS += -DNDEBUG
CFLAGS += -DONLY_MSPACES=1
SRC_DIR = ./src
OBJ_DIR = ./obj
SRC_COMMON = $(SRC_DIR)/memlog.c $(SRC_DIR)/malloc.c
SRC_ALLOCATOR = $(SRC_DIR)/allocator.c

######################################
# TLSF ###############################
ifeq ($(shell test -f $(SRC_DIR)/tlsf.c && echo true),true)
  CFLAGS += -DENABLE_TLSF=1
	SRC_COMMON += $(SRC_DIR)/tlsf.c
	CFLAGS+= -DTLSF_USE_LOCKS=0
	CFLAGS+= -DTLSF_STATISTIC=0
	CFLAGS+= -DUSE_MMAP=1
	CFLAGS+= -DUSE_SBRK=0
endif
######################################

######################################
# Compact-fit ########################
ifeq ($(shell test -f $(SRC_DIR)/cf.c && echo true),true)
  CFLAGS += -DENABLE_CF=1
	CFLAGS += -DTARGET_X86
	SRC_COMMON += $(SRC_DIR)/cf.c
endif
######################################

OBJ_COMMON = $(subst $(SRC_DIR), $(OBJ_DIR), $(SRC_COMMON:.c=.o))

DIR_MF   = ../multiheap_fit
LIB_MF   = $(DIR_MF)/multiheap_fit.a
CFLAGS  += -I$(DIR_MF)/include

DIR_VMF  = ../virtual_multiheap_fit/allocator
LIB_VMF  = $(DIR_VMF)/virtual_multiheap_fit.a
CFLAGS  += -I$(DIR_VMF)/include

TIME_SRC = $(SRC_DIR)/time_test.c $(SRC_ALLOCATOR)
TIME_EXE = ./time_test.out
MEMORY_SRC = $(SRC_DIR)/memory_test.c $(SRC_ALLOCATOR)
MEMORY_EXE = ./memory_test.out
INST_SRC = $(SRC_DIR)/inst_test.c $(SRC_ALLOCATOR)
INST_EXE = ./inst_test.out
DIR_INST = ../instruction_counter
LIB_INST = $(DIR_INST)/inst_counter.a

DEPENDS = $(OBJ_COMMON:.o=.d)

all: $(TIME_EXE) $(MEMORY_EXE) $(INST_EXE)

$(TIME_EXE): $(TIME_SRC) $(OBJ_COMMON) $(LIB_MF) $(LIB_VMF)
	$(CC) -o $@ $(CFLAGS) $^ -lm

$(MEMORY_EXE): $(MEMORY_SRC) $(OBJ_COMMON) $(LIB_MF) $(LIB_VMF)
	$(CC) -o $@ $(CFLAGS) -DMEMORY_TEST=1 $^ -lm

$(INST_EXE): $(INST_SRC) $(OBJ_COMMON) $(LIB_MF) $(LIB_VMF) $(LIB_INST)
	$(CC) -o $@ $(CFLAGS) -DINSTRUCTION_COUNTER_ENABLE \
  -I$(DIR_INST)/include $^ -lm

$(LIB_MF):
	make -C $(DIR_MF)

$(LIB_VMF):
	make -C $(DIR_VMF)

$(LIB_INST):
	make -C $(DIR_INST)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@if [ ! -d $(OBJ_DIR) ]; \
	  then echo "mkdir -p $(OBJ_DIR)"; mkdir -p $(OBJ_DIR); \
	fi
	$(CC) $(CFLAGS) -MMD -MP -o $@ -c $<

clean:
	$(RM) $(TIME_EXE) $(MEMORY_EXE) $(INST_EXE) $(OBJ_COMMON) $(DEPENDS)

-include $(DEPENDS)

.PHONY: all clean
