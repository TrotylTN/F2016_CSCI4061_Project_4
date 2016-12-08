CC = gcc
CFLAGS = -D_REENTRANT
LDFLAGS = -lpthread

web_server: server.c
	${CC} -o web_server server.c util.o ${LDFLAGS} ${CFLAGS}

clean:
	rm web_server

test_server: web_server
	./web_server 9001 ./testing 100 100 100

test_request: web_server
	cat ./testing/urls | xargs -n 1 -P 8 wget --directory-prefix=./junk

