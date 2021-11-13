#include <stdlib.h>

#include "memory.h"
#include "vm.h"


void* reallocate(void* pointer, size_t oldSize, size_t newSize) 
{
    /* When newSize is zero, 
     * handle the deallocation case ourselves by calling free(). 
     * 
     * Otherwise, rely on the C standard library’s realloc() function. 
     * That function conveniently supports 
     * the other three aspects of our policy. 
     * When oldSize is zero, realloc() is equivalent to calling malloc().
     *
     * The interesting cases are when both oldSize and newSize are not zero. 
     * Those tell realloc() to resize the previously-allocated block. 
     * If the new size is smaller than the existing block of memory, 
     * it simply updates the size of the block and 
     * returns the same pointer you gave it. 
     * If the new size is larger, 
     * it attempts to grow the existing block of memory.
     *
     *  If there isn’t room to grow the block, 
     *  realloc() instead allocates a new block of memory 
     *  of the desired size, copies over the old bytes, frees the old block, 
     *  and then returns a pointer to the new block. 
     */

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    /* Because computers are finite lumps of matter 
     * and not the perfect mathematical abstractions computer 
     * science theory would have us believe, 
     * allocation can fail if there isn’t enough memory 
     * and realloc() will return NULL. We should handle that.
     */
    if (result == NULL) exit(1);
    return result;
}

static void freeObject(Obj* object) 
{
    /* Free the character array 
     * and then free the ObjString. */
    switch (object->type) {
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(OBJ_FUNCTION, object);
            break;
        }

        case OBJ_NATIVE: 
            FREE(ObjNative, object);
            break;

        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
    }
}

void freeObjects()
{
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}
