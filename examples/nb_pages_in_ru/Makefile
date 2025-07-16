CC = gcc
CFLAGS = -Wall -Wextra

SRC = main.c

TARGET = pages_per_ru

all : $(TARGET)

$(TARGET) : $(SRC)
	$(CC) -o $@ $(CFLAGS) $< -lfdp -luring

clean : 
	rm $(TARGET)
