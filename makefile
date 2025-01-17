CC = gcc
CFLAGS = -Wall -g -I.  # Add -I. to include the current directory for header files

all: myshell mypipeline looper

myshell: myshell.o LineParser.o
	$(CC) $(CFLAGS) -o myshell myshell.o LineParser.o

mypipeline: mypipeline.o
	$(CC) $(CFLAGS) -o mypipeline mypipeline.o

looper: looper.o
	$(CC) $(CFLAGS) -o looper looper.o

myshell.o: myshell.c
	$(CC) $(CFLAGS) -c myshell.c

LineParser.o: LineParser.c LineParser.h  # Ensure LineParser.h is a dependency
	$(CC) $(CFLAGS) -c LineParser.c

mypipeline.o: mypipeline.c
	$(CC) $(CFLAGS) -c mypipeline.c

looper.o: looper.c
	$(CC) $(CFLAGS) -c looper.c

clean:
	rm -f myshell mypipeline *.o

.PHONY: all clean
