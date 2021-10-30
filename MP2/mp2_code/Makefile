all: vec ls

vec: main.c monitor_neighbors.h
	gcc -pthread -o vec_router main.c monitor_neighbors.h

ls: main.c monitor_neighbors.h
	gcc -pthread -o ls_router main.c monitor_neighbors.h

manager: manager_send.c 
	gcc -pthread -o manager_send manager_send.c

debug: main.c monitor_neighbors.h
	gcc -pthread -g -o ls_router main.c monitor_neighbors.h

.PHONY: clean
clean:
	rm *.o vec_router ls_router
