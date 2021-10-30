#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main ()
{
    initVM();
    Chunk chunk;
    initChunk(&chunk);
        
    /*
     * Evaluating (0 - (1.2 + 3.4) / 5.6)
     */
    int constant = addConstant(&chunk, 1.2);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);

    constant = addConstant(&chunk, 3.4);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);
    writeChunk(&chunk, OP_ADD, 123);

    constant = addConstant(&chunk, 5.6);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);
    writeChunk(&chunk, OP_DIVIDE, 123);

    writeChunk(&chunk, OP_NEGATE, 123);
    writeChunk(&chunk, OP_RETURN, 123);

    freeVM();
    disassembleChunk(&chunk, "test chunk");
    /*
     * == test chunk
       0000  123 OP_CONSTANT         0 '1.2'
       0002    | OP_CONSTANT         1 '3.4'
       0004    | OP_ADD
       0005    | OP_CONSTANT         2 '5.6'
       0007    | OP_DIVIDE
       0008    | OP_NEGATE
       0009    | OP_RETURN
     */
    freeChunk(&chunk);

    return 0;
}
