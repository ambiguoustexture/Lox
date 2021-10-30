#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 
 * First create a flag to hide the diagnostic logging behind.
 *
 * When this flag is defined, 
 * the VM disassembles and prints each instruction right 
 * before executing it.
 */
#define DEBUG_TRACE_EXECUTION

#endif
