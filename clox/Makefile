cc = gcc-11
prog = clox
# source = chunk.c debug.c main.c memory.c value.c vm.c scanner.c compiler.c object.c table.c
source = chunk.c debug.c main.c memory.c value.c 

$(prog): $(source)
	$(cc) $(source) -g -o $(prog)

clean:
	rm -rf clox
