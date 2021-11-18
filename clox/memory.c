#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "stdio.h"
#include "debug.h"
#endif 

/* The threshold is a multiple of the heap size. 
 * This way, as the amount of memory the program uses grows, 
 * the threshold moves farther out to limit the total time 
 * spent re-traversing the larger live set. 
 * Like other numbers in this chapter, 
 * the scaling factor is basically arbitrary. */
#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) 
{
    /* Every time allocate or free some memory, 
     * adjust the counter by that delta. */
    vm.bytesAllocated += newSize - oldSize;

    /* Whenever call reallocate() to acquire more memory, 
     * force a collection to run. 
     * The if check is because reallocate() is also called 
     * to free or shrink an allocation. 
     * Don’t want to trigger a GC for that 
     * — in particular because the GC itself 
     * will call reallocate() to free memory. 
     *
     * Collecting right before allocation is the classic way 
     * to wire a GC into a VM. 
     * Also, allocation is the only time 
     * when really need some freed up memory, so that can reuse it. 
     * If you don’t use allocation to trigger a GC, 
     * have to make sure every possible place in code 
     * where can loop and allocate memory 
     * also has a way to trigger the collector. 
     * Otherwise, the VM can get into a starved state 
     * where it needs more memory but never collects any. */
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
    
        /* When the total crosses the limit, run the collector. */
        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
    }
    
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

void markObject(Obj* object)
{
    if (object == NULL) return;

    /* References between objects are directed, 
     * but that doesn’t mean they’re acyclic! 
     * It’s entirely possible to have cycles of objects. When that happens, 
     * need to ensure the collector doesn’t get stuck in an infinite loop as 
     * it continually re-adds the same series of objects to the gray stack. 
     *
     * If the object is already marked, 
     * don’t mark it again and thus don’t add it to the gray stack. 
     * This ensures that an already-gray object is not redundantly added 
     * and that a black object is not inadvertently turned back to gray. 
     * In other words, 
     * it keeps the wavefront moving forward through the white objects. */
    if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif 

    object->isMarked = true;

    /* Create a separate worklist to keep track of all of the gray objects. 
     * When an object turns gray 
     * — in addition to setting the mark field 
     * — also add it to the worklist. */
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

        /* Take full responsibility for this array. 
         * That includes allocation failure. 
         * If can’t create or grow the gray stack, 
         * then can’t finish the garbage collection. */
        if (vm.grayStack == NULL) exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value)
{
    /* Some Lox values — numbers, Booleans, and nil — 
     * are stored directly inline in Value and require no heap allocation. 
     * The garbage collector doesn’t need to worry about them at all, 
     * so the first thing need to do is ensure that 
     * the value is an actual heap object. 
     * If so, the real work happens in the "markObject()" function. */
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array)
{
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif 
    /* Each object kind has different fields 
     * that might reference other objects, 
     * so need a specific blob of code for each type. 
     * Start with the easy ones 
     * — strings and native function objects contain no outgoing references 
     * so there is nothing to traverse. */
    switch (object->type) {
        case OBJ_BOUND_METHOD: {
            /* The bound method has a couple of references, 
             * but it doesn’t own them, so it frees nothing but itself. 
             * However, those references 
             * do get traced by the garbage collector. 
             * This ensures that 
             * a handle to a method keeps the receiver around in memory 
             * so that this can still find the object 
             * when invoke the handle later. */
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(bound->receiver);
            
            /* Also trace the method closure. */
            markObject((Obj*)bound->method);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject((Obj*)klass->name);

            /* The GC needs to trace through classes into the method table. 
             * If a class is still reachable (likely through some instance), 
             * then all of its methods certainly need to stick around too. */
            markTable(&klass->methods);

            break;
        }
        case OBJ_CLOSURE: {
            /* Each closure has a reference to the bare function it wraps, 
             * as well as an array of pointers to the upvalues it captures. */
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            /* Each function has a reference to an ObjString 
             * containing the function’s name. 
             * More importantly, the function has a constant table 
             * packed full of references to other objects. */ 
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_INSTANCE: {
            /* If the instance is alive, need to keep its class around. 
             * Also, need to keep every object 
             * referenced by the instance’s fields. 
             * Most live objects that are not roots are reachable 
             * because some instance references the object in a field. */ 
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;            
        }
        case OBJ_UPVALUE:
            /* When an upvalue is closed, 
             * it contains a reference to the closed-over value. 
             * Since the value is no longer on the stack, need to make sure 
             * trace the reference to it from the upvalue. */
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void freeObject(Obj* object) 
{
    /* Logging at the end of an object's lifespan. */
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif 

    /* Free the character array 
     * and then free the ObjString. */
    switch (object->type) {
        case OBJ_BOUND_METHOD:
            /* The bound method has a couple of references, 
             * but it doesn’t own them, so it frees nothing but itself. */
            FREE(ObjBoundMethod, object);
            break;
        case OBJ_CLASS: {
            /* The ObjClass struct owns the memory for the method table, 
             * so when the memory manager deallocates a class, 
             * the table should be freed too. */
            ObjClass* klass = (ObjClass*)object;
            freeTable(&klass->methods);
            
            FREE(ObjClass, object);
            break;            
        }
        case OBJ_CLOSURE: {
            /* When an ObjClosure is freed, we also free the upvalue array. */
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            /* ObjClosure does not own the ObjUpvalue objects itself, 
             * but it does own its dynamic array 
             * that contains pointers to those upvalues. */

            /* Only free the ObjClosure itself, not the ObjFunction. 
             * That’s because the closure doesn’t own the function. 
             * There may be multiple closures 
             * that all reference the same function, 
             * and none of them claims any special privilege over it. 
             * Can’t free the ObjFunction 
             * until all objects referencing it are gone 
             * — including even the surrounding function 
             * whose constant table contains it. */
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(OBJ_FUNCTION, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
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

        case OBJ_UPVALUE: {
            /* Multiple closures can close over the same variable, 
             * so ObjUpvalue does not own the variable it references. 
             * Thus, the only thing to free is the ObjUpvalue itself. */
            FREE(ObjUpvalue, object);
            break;
        }
    }
}

static void markRoots()
{
    /* Most roots are local variables or temporaries 
     * sitting right in the VM’s stack, 
     * so start by walking that. */
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    /* Most function call state lives in the value stack, 
     * but the VM maintains a separate stack of CallFrames. 
     * Each CallFrame contains a pointer to the closure being called. 
     * The VM uses those pointers to access constants and upvalues, 
     * so those closures need to be kept around too. */
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    /* The open upvalue list is another set of values 
     * that the VM can directly reach. */
    for (ObjUpvalue* upvalue = vm.openUpvales; 
         upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    /* Marking the stack covers local variables and temporaries. 
     *
     * The other main source of roots are the global variables. 
     * Those live in a hash table owned by the VM, 
     * so call another helper function 
     * for marking all of the objects in a table. */
    markTable(&vm.globals);

    /* A collection can begin during any allocation. 
     * Those allocations don’t just happen 
     * while the user’s program is running. 
     * The compiler itself periodically grabs memory 
     * from the heap for literals and the constant table. 
     * If the GC runs while in the middle of compiling, 
     * then any values the compiler directly accesses 
     * need to be treated as roots too.  
     * To keep the compiler module cleanly separated 
     * from the rest of the VM, 
     * do that in a separate function. */
    markCompilerRoots();

    /* Make the `initString` to stick around, 
     * so the GC considers it a root. */
    markObject((Obj*)vm.initString);

}

static void traceReferences() 
{
    /* Until the stack empties, 
     * keep pulling out gray objects, traversing their references, 
     * and then marking them black. 
     * Traversing an object’s references may turn up new white objects 
     * that get marked gray and added to the stack. 
     * So this function swings back and forth 
     * between turning white objects gray and gray objects black, 
     * gradually advancing the entire wavefront forward. */
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep()
{
    Obj* previous = NULL;
    Obj* object   = vm.objects;
    
    /* The outer while loop walks the linked list of every object in the heap, 
     * checking their mark bits. 
     * If an object is unmarked (white), 
     * unlink it from the list and free it 
     * using the freeObject() function we already wrote. */
    while (object != NULL) {
        if (object->isMarked) {
            /* After one round of sweep() completes, 
             * the only remaining objects are the live black ones 
             * with their mark bits set. 
             * That’s correct, but when the next collection cycle starts, 
             * need every object to be white. 
             * So whenever reach a black object, 
             * go ahead and clear the bit 
             * now in anticipation of the next run. */
            object->isMarked = false;
            
            previous = object;
            object   = object->next;
        } else {
            Obj* unreached = object;

            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

void collectGarbage()
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");

    /* Capture the heap size before the collection. */
    size_t before = vm.bytesAllocated;
#endif


    /* Objects are scattered across the heap like stars in the inky night sky. 
     * A reference from one object to another forms a connection 
     * and these constellations are the graph that the mark phase traverses. 
     * Marking begins at the roots. */
    markRoots();
    
    /* Processing gray objects. */
    traceReferences();

    /* To remove references to unreachable strings, 
     * need to know which strings are unreachable. 
     * Don’t know that until after the mark phase has completed. 
     * But can’t wait until after the sweep phase is done because 
     * by then the objects — and their mark bits — 
     * are no longer around to check. 
     * So the right time is exactly 
     * between the marking and sweeping phases. */
    tableRemoveWhite(&vm.strings);


    /* Anything still white never got touched by the trace 
     * and is thus garbage. All that’s left is to reclaim them. */
    sweep();

    /* The sweep phase frees objects by calling reallocate(), 
     * which lowers the value of bytesAllocated, 
     * so after the collection completes know how many live bytes remain. 
     * Adjust the threshold of the next GC based on that. */
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");

    printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated,
           vm.nextGC);
#endif
}

void freeObjects()
{
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }

    /* Free the worklist for gray objects. */
    free(vm.grayStack);
}
