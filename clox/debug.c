#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) 
{
    /*
     * To disassemble a chunk,
     * print a little header
     * (so can tell which chunk looking at)
     * and then crank through the bytecode,
     * disassembling each instruction.
     */
    printf("== %s\n", name);

    for (int offset = 0; offset < chunk->count; ) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) 
{
    /* 
     * As with OP_RETURN, print out the name of the opcode. 
     * Then pull out the constant index 
     * from the subsequent byte in the chunk. 
     * Print that index, but that isn’t super useful to human readers. 
     * So also look up the actual constant value —— 
     * since constants are known at compile-time 
     * after all —— and display the value itself too.
     */
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");

    /* 
     * Where OP_RETURN was only a single byte, 
     * OP_CONSTANT is two ——  
     * one for the opcode and 
     * one for the operand.
     */
    return offset + 2;
}

static int simpleInstruction(const char* name, int offset) 
{
    printf("%s\n", name);
    return offset + 1;
}    

int disassembleInstruction(Chunk* chunk, int offset)
{
    /* 
     * First, it prints the byte offset of the given instruction -— 
     * that tells us where in the chunk this instruction is.
     *
     * Next, it reads a single byte 
     * from the bytecode at the given offset.
     */
    printf("%04d ", offset);

    /*
     * In our disassembler, it’s helpful to show which source line each instruction was compiled from. That gives us a way to map back to the original code when we’re trying to figure out what some blob of bytecode is supposed to do. After printing the offset of the instruction—the number of bytes from the beginning of the chunk—we show its source line.
     */
    if (offset > 0 && 
        chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->code[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
