CC = g++
CFLAGS = -Wall -Wextra

SRC = main.c

TARGET = $(shell basename $(CURDIR))

all : $(TARGET)

$(TARGET) : $(SRC)
	$(CC) -o $@ $(CFLAGS) $< -lfdp -luring

clean : 
	rm $(TARGET)
