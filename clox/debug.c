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

static int byteInstruction(const char* name, Chunk* chunk, int offset)
{
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
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

    if (offset > 0 && 
        chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL",      offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE",     offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE",    offset);
        case OP_POP:
            return simpleInstruction("OP_POP",      offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL: 
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL",    offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER",  offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS",     offset);

        /* The arithmetic instruction formats are simple, like OP_RETURN. 
         * Even though the arithmetic operators take operands —— 
         * which are found on the stack —— 
         * the arithmetic bytecode instructions do not.
         */
        case OP_ADD:
            return simpleInstruction("OP_ADD",      offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE",   offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT",      offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE",   offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT",    offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN",   offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
