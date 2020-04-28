#
#
# makefile for webserver
#

CC = gcc -Wall

wsng: wsng.o socklib.o
	$(CC) -o ws wsng.o socklib.o

clean:
	rm -f wsng.o socklib.o core ws
