all : endpoint.c netio.c userio.c
	make endpoint

endpoint : endpoint.c netio.c userio.c
	g++ -o endpoint endpoint.c netio.c userio.c -lncurses
