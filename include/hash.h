#ifndef HASH_H
#define HASH_H

#define LOAD_FACTOR_THRESHOLD 0.7

typedef enum {
    REJECT       = 0,
    UPDATE_VALUE = 1,
}ConflictFlags;

typedef enum {
    HASH_OK          = 0,
    ENTRY_NOT_FOUND  = 1,
    ENTRY_REJECTED   = 2,
    ENTRY_UPDATED    = 3,
    ENTRY_DUPLICATED = 4,
    UNKNOWN_FLAG     = 5,
    INVALID_PARAMS   = 6
    
}hash_error;

typedef struct Entry_s {
    const char *key;
    const char *value;
    struct Entry_s *next;
} Entry_t;

typedef struct HashTable_s {
    Entry_t **buckets;
    unsigned int capacity;
    unsigned int stored;
} HashTable_t;


/**
 * @brief Create an hash table
 * 
 * @param capacity The initial capacity for the new hash table
 * 
 * @return The pointer to the new hash table
 */
HashTable_t *hash_create_table(int capacity);

/**
 * @brief Deletes the hash table
 * 
 * @param The hash table to be deleted
 */
void hash_free_table(HashTable_t *table);

/**
 * @brief Inserts the new entry into te hash table and handles conflict based on the flag
 * 
 * @param table Hash table to store the new entry
 * @param key   The entry's key
 * @param value The entry's value
 * @param flag  What to do in the case of a conflict
 * 
 * @return - OK
 * @return - ENTRY_REJECTED
 * @return - ENTRY_UPDATED
 * @return - ENTRY_DUPLICATED
 */
hash_error hash_insert_entry(HashTable_t *table, const char *key, const char *value, ConflictFlags flag);

/**
 * @brief Deletes entry from hash table
 * 
 * @param table Hash table from where to remove the entry
 * @param key   Key from the entry to be removed
 * 
 * @return      - ENTRY_NOT_FOUND
 * @return      - OK
 */
hash_error hash_delete_entry(HashTable_t *table, const char *key);

/**
 * @brief Searches the provided key in the hash table
 * 
 * @param table Hash table to search in
 * @param key   The key to be searched for
 *
 * @return      - The pointer to the entry if it already exists
 * @return      - The pointer to the next available entry in that index
 * @return      - NULL if entry doesn't exist
 */
Entry_t *hash_search_table(HashTable_t *table, const char *key);

/**
 * @brief Determine the key's index in the hash table
 * 
 * @param key       The key to be indexed
 * @param capacity  The current capacity of the hash table
 * 
 * @return The index for the provided key
 */
unsigned int hash_index(const char *key, int capacity);

#endif
