compile: 
	mkdir -p ./build
	gcc ./src/p1.c -g -o ./build/p1.o -Wall -Wextra -pedantic -Wno-unused-parameter
	gcc ./src/p3.c -g -o ./build/p3.o -Wall -Wextra -pedantic -Wno-unused-parameter

.PHONY: compile
