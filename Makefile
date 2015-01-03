all: tcp-proxy

CFLAGS = -Wall -g -O2 
LFLAGS = -pthread
CC = gcc 
PS2PDF = ps2pdf14
DIST_FILE = tcp-proxy.tar.gz

tcp-proxy: tcp-proxy.c
	$(CC) $(LFLAGS) -o $@ $^ 

dist: clean
	tar -czf $(DIST_FILE) ../tcp-proxy --exclude=$(DIST_FILE) --exclude=".svn" 

clean:
	rm -fr .DS_Store *.tar.gz *.ps *.pdf *.o *.dSYM *~ tcp-proxy test-tcpproxy
