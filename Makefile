all:
	clang -ggdb -c main.c
	clang -lc -lnetgraph -o mcastroute main.o
clean:
	rm -rf mcastroute main.o
