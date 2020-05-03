# Gerald Arocena
# CSCI E-28, Spring 2020
# 5-2-2020
# makefile for webserver/hw 6

CC = gcc -Wall

wsng: wsng.o socklib.o
	$(CC) -o wsng wsng.o socklib.o

clean:
	rm -f wsng.o socklib.o core wsng
