TARGET = sish 
CC = cc
CFLAGS  = -ansi -g -Wall -Werror -Wextra -Wformat=2 -Wno-format-y2k -Wjump-misses-init -Wlogical-op -Wpedantic -Wshadow -lmagic
RM = rm -f

default: $(TARGET)
all: default

$(TARGET): $(TARGET).o
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).o

$(TARGET).o: $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS) -c $(TARGET).c

debug:
	$(CC) -o $(TARGET) $(TARGET).c

clean:
	$(RM) $(TARGET) *.o 