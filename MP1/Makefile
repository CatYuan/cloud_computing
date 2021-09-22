# CS435 - http_client

CC=g++
CC_OPTS=
CC_LIBS=
CC_DEFINES=
CC_INCLUDES=
CC_ARGS=${CC_OPTS} ${CC_LIBS} ${CC_DEFINES} ${CC_INCLUDES}

# clean is not a file
.PHONY=clean

#target "all" depends on all others
# all: httpClient httpClient-debug

httpClient: http_client.c
	@${CC} ${CC_ARGS} -o http_client http_client.c

httpClient-debug: http_client.c
	@${CC} -g -o http_client http_client.c
	
clean:
	@rm -f server http_client *.o
