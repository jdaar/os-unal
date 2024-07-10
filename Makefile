compile: 
	mkdir -p ./build
	gcc ./src/p1.c -g -o ./build/p1.o --pedantic -Wall
	gcc ./src/p3.c -g -o ./build/p3.o --pedantic -Wall

.PHONY: compile
