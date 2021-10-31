#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

void repl()
{
    char line[1024];
    for (;;) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        interpret(line);
    }
}

static char* readFile(const char* path) 
{
    FILE* file = fopen(path, "rb");

    /*
     * If we can’t correctly read the user’s script, 
     * all can really do is tell the user and exit the interpreter gracefully.
     */ if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    /*
     * Open the file, but before reading it, 
     * seek to the very end using fseek(). 
     * Then call ftell() which tells how many bytes 
     * from the start of the file. 
     * Since seeked (“sought”?) to the end, that’s the size. 
     * Rewind back to the beginning, 
     * allocate a string of that size, 
     * and read the whole file in a single batch.
     */
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    /*
     * If can’t even allocate enough memory to read the Lox script, 
     * the user’s probably got bigger problems to worry about, 
     * but one should do best to at least let the users know.
     */ if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    /*
     * Finally, the read itself may fail.
     */ if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char* path) 
{
    /*
     * Read the file and execute the resulting string of Lox source code. 
     * Then, based on the result of that, 
     * set the exit code appropriately.
     */ 
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main (int argc, const char ** argv)
{
    initVM();
    
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        runFile(argv[1]); 
    } else {
        fprintf(stderr, "Usage: clox [path]\n");
        exit(64);
    }
    
    freeVM();
    return 0;
}
