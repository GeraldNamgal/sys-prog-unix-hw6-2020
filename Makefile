#
#
# makefile for webserver
#

CC = gcc -Wall

wsng: wsng.o socklib.o
	$(CC) -o wsng wsng.o socklib.o

clean:
	rm -f wsng.o socklib.o core wsng
