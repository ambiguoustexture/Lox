#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) 
{
    table->count    = 0;
    table->capacity = 0;
    table->entries  = NULL;
}

void freeTable(Table* table)
{
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) 
{
    /* The real core of the hash table. 
     * It’s responsible for taking a key and an array of buckets 
     * and figuring out which bucket the entry belongs in. 
     * This function is also where linear probing and collision handling 
     * come into play. 
     * Will use it both to find existing entries in the hash table, 
     * and to decide where to insert new ones. */

    /* First, take the key’s hash code and map it to the array 
     * by taking it modulo the array size. 
     * That gives an index into the array where, ideally, 
     * one will be able to find or place the entry. */
    uint32_t index = key->hash % capacity;
    /* The first time pass a tombstone, store it in a local variable. */
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        /* If the key for that entry in the array is NULL, 
         * then the bucket is empty.  
         * 
         * If the key in the bucket is equal to the key we’re looking for, 
         * then that key is already present in the table. (look-up or insert)
         *
         * If reach a truly empty entry, 
         * then the key isn’t present. 
         * In that case, if have passed a tombstone, 
         * return its bucket instead of the later empty one. 
         * If we’re calling findEntry() in order to insert a node, 
         * that lets us treat the tombstone bucket 
         * as empty and reuse it for the new entry. */
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // Found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key->hash == key->hash) {
                // Found the key
                return entry;
        }
        
        /* Otherwise, the bucket has an entry in it, 
         * but with a different key. This is a collision 
         * In that case, start probing. That’s what that for loop does. */
        index = (index + 1) % capacity;
    }
}

static void adjustCapacity(Table* table, int capacity)
{
    /* Creates a bucket array with capacity entries. 
     * After it allocates the array, 
     * it initializes every element to be an empty bucket 
     * and then stores the array (and its capacity) 
     * in the hash table’s main struct. */
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key   = NULL;
        entries[i].value = NIL_VAL;
    }

    /* To choose the bucket for each entry, 
     * take its hash key modulo the array size. 
     * That means that when the array size changes, 
     * entries may end up in different buckets.
     *
     * Need to recalculate the buckets for each 
     * of the existing entries in the hash table. 
     * That in turn could cause new collisions which need to be resolved. 
     * So the simplest way to handle that is to rebuild the table 
     * from scratch by re-inserting every entry into the new empty array. 
     *
     * Walk through the old array front to back. 
     * Any time find a non-empty bucket, 
     * insert that entry into the new array. 
     * Use findEntry(), passing in the new array 
     * instead of the one currently stored in the Table. 
     * (This is why findEntry() takes a pointer directly to an entry array 
     * and not the whole Table struct. 
     * That way, can pass the new array and capacity 
     * before we’ve stored those in the struct.) */

    /* During the adjust process, don’t copy the tombstones over. 
     * They don’t add any value 
     * since we’re rebuilding the probe sequences anyway, 
     * and would just slow down lookups. */
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[1];
        if (entry->key == NULL) continue;

        Entry* dst = findEntry(entries, capacity, entry->key);
        dst->key   = entry->key;
        dst->value = entry->value;
        
        /* Then each time find a non-tombstone entry, increment it. */
        table->count++;
    }

    /* Release the memory for the old array. */
    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries  = entries;
    table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value)
{
    /* Adds the given key/value pair to the given hash table. 
     * If an entry for that key is already present, 
     * the new value overwrites the old one. 
     * It returns true if a new entry was added. 
     *
     * Once have found the bucket in the array the key should go, 
     * inserting is straightforward. 
     * Update the hash table’s size, 
     * taking care to not increase the count 
     * if overwrote an already-present key. 
     * Then copy the key and value 
     * into the corresponding fields in the entry. */

    /* Before inserting anything, 
     * need to make sure have an array. 
     * If don’t have enough capacity to insert an item, 
     * reallocate and grow the array. */
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    } 

    Entry* entry = findEntry(table->entries, table->capacity, key);

    bool isNewKey = entry->key == NULL;
    
    /* Counting Tombstone 
     *
     * The count is no longer the number of entries in the hash table, 
     * it’s the number of entries plus tombstones. 
     * That implies that only increment the count during insertion 
     * if the new entry goes into an entirely empty bucket. 
     *
     * If we are replacing a tombstone with a new entry, 
     * the bucket has already been accounted for 
     * and the count doesn’t change. */
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    {
        entry->key   = key;
        entry->value = value;
    }
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) 
{
    if (table->count == 0) return false;

    /* First, find the bucket containing the entry we want to delete. 
     * (If don’t find it, there’s nothing to delete, so bail out.) 
     * Replace the entry with a tombstone. 
     * In clox, use a NULL key and a true value to represent that, 
     * but any representation that can’t be confused with an empty bucket 
     * or a valid entry works. */

    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // Place a tombstone in the entry.
    entry->key   = NULL;
    entry->value = BOOL_VAL(true);

    return true;
}

bool tableGet(Table* table, ObjString* key, Value* value)
{
    if (table->count == 0) return false;
    /* Pass in a table and a key. 
     * If it finds an entry with that key, it returns true, 
     * otherwise it returns false. 
     * If the entry exists, 
     * it stores the resulting value in the value output parameter. */

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

void tableAddAll(Table* from, Table* to)
{
    /* Walks the bucket array of the source hash table. 
     * Whenever it finds a non-empty bucket, 
     * it adds the entry to the destination hash table 
     * using the tableSet() function we recently defined. */
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars, int length, 
        uint32_t hash)
{
    /* First, pass in the raw character array of the key looking for 
     * instead of an ObjString. 
     * At the point that call this, haven’t created an ObjString yet.
     *
     * Second, when checking to see if found the key, 
     * look at the actual strings. 
     * First, see if they have matching lengths and hashes. 
     * Those are quick to check and if they aren’t equal, 
     * the strings definitely aren’t the same. 
     *
     * Finally, in case there is a hash collision, 
     * do an actual character-by-character string comparison. 
     * This is the one place in the VM 
     * where actually test strings for textual equality. 
     * Do it here to deduplicate strings and then 
     * the rest of the VM can take for granted 
     * that any two strings at different addresses in memory 
     * must have different contents. */
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;

    for (;;) {
        Entry* entry = &table->entries[index];

        if (entry->key == NULL) {
            // Stop if find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length && 
                entry->key->hash == hash && 
                memcpy(entry->key->chars, chars, length) == 0) {
            // Found the target string.
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}

void tableRemoveWhite(Table* table) 
{
    /* Walk every entry in the table. 
     * The string intern table only uses the key of each entry 
     * — it’s basically a hash set not a hash map. 
     * If the key string object’s mark bit is not set, 
     * then it is a white object that is moments from being swept away. 
     * Delete it from the hash table first and 
     * thus ensure won’t see any dangling pointers. */
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked) {
            tableDelete(table, entry->key);
        }
    }
}

void markTable(Table* table)
{
    /* Walk the entry array. For each one, mark its value. 
     * Also mark the key strings 
     * for each entry since the GC manages those strings too. */
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markObject((Obj*)entry->key);
        markValue(entry->value);
    }
}
