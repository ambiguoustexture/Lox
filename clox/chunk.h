#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

/* 
 * chunk: sequences of bytecode.
 */

/* 
 * In this bytecode format, 
 * each instruction has a one-byte operation code 
 * (universally shortened to opcode).
 * That number controls what kind of instruction we’re dealing with —- 
 * add, subtract, look up variable, etc.
 */
typedef enum {
    /*
     * The compiled chunk needs to 
     * not only contain the values, 
     * but know when to produce them 
     * so that they are printed in the right order. 
     * Thus, need an instruction that produces a particular constant.
     *
     * When the VM executes a constant instruction, 
     * it “loads” the constant for use.
     */
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
} OpCode;

/*
 * A dynamical array of instructions
 *
 * Bytecode is a series of instructions. 
 * Eventually, we’ll store some other data along with the instructions.
 *
 * Since don’t know how big the array needs to be 
 * before start compiling a chunk, it must be dynamic.
 *
 * - Cache-friendly, dense storage.
 * - Constant-time indexed element lookup.
 * - Constant-time appending to the end of the array.
 *
 *  In addition to the array itself, keep two numbers: 
 *  the number of elements in the array allocated (“capacity”) 
 *  and how many of those allocated entries are actually in use (“count”).
 */
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    ValueArray constants;
} Chunk;

/* 
 * C doesn’t have constructors, 
 * so declare a function to initialize a new chunk.
 */
void initChunk(Chunk* chunk);

/*  We’re in C now, 
 *  have to manage memory ourselves, 
 *  like Ye Olden Times, and that means freeing it too.
 */
void freeChunk(Chunk* chunk);

/* 
 * The dynamic array starts off completely empty. 
 * Don’t even allocate a raw array yet. 
 * To append a byte to the end of the chunk use a new function.
 */
void writeChunk(Chunk* chunk, uint8_t byte, int line);

/*
 * A convenience method to add a new constant to the chunk.
 */
int addConstant(Chunk* chunk, Value value);

#endif
