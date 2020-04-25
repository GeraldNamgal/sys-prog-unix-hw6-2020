#
#
# makefile for webserver
#

CC = gcc -Wall

ws: ws.o socklib.o
	$(CC) -o ws ws.o socklib.o

clean:
	rm -f ws.o socklib.o core
