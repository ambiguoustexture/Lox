#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk* chunk) 
{
    chunk->count    = 0;
    chunk->capacity = 0;
    chunk->code     = NULL;
    chunk->lines    = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) 
{
    /* Deallocate all of the memory and 
     * then call initChunk() to zero out the fields 
     * leaving the chunk in a well-defined empty state. 
     */
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line)
{
    /* 
     * The first thing need to do 
     * is to see if the current array already has capacity for the new byte. 
     * If it doesn’t, then first need to grow the array to make room.
     */
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code     = GROW_ARRAY(uint8_t, chunk->code, 
                                     oldCapacity, chunk->capacity);
        chunk->lines    = GROW_ARRAY(int, chunk->lines, 
                                     oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count]  = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value)
{
    push(value);
    writeValueArray(&chunk->constants, value);
    pop();

    /*
     * After add the constant, 
     * return the index where the constant was appended 
     * so that can locate that same constant later.
     */
    return chunk->constants.count - 1;
}
