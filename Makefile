#CROSS=arm-linux-gnueabihf-

CC=$(CROSS)gcc
CPP=$(CROSS)g++
LD=$(CROSS)ld
AS=$(CROSS)as
AR=$(CROSS)ar
OBJCOPY=$(CROSS)objcopy
OBJDUMP=$(CROSS)objdump


CFLAGS +=  -Wall -I. 
LDFLAGS = -lm 

COPY        := cp
MKDIR       := mkdir -p
MV          := mv
RM          := rm -f
DIRNAME     := dirname

EXEC = fru-generator


SRCS := fru.c cJSON.c main.c

OBJS := $(SRCS:%.c=%.o)

$(EXEC):$(OBJS)
	$(CC)  $^ -o $@ $(LDFLAGS)

$(OBJS):$(SRCS)
	$(CC)  $(CFLAGS) -c $^
clean:
	$(RM) *.o $(EXEC)
