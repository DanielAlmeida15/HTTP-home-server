#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *set_value(const char *value)
{
    char *new_value = malloc(sizeof(char)*strlen(value));
    memcpy(new_value, value, strlen(value)); 

    return (const char *) new_value;
}

/**
 * @brief The DJB2 hash function
 * 
 * @param str The string to be hashed
 * 
 * @return    The hashed result
 */
static unsigned long hash(const char *str)
{
    unsigned long hash = 5381;
    unsigned int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    
    return hash;
}

/**
 * @brief Determine the key's index in the hash table
 * 
 * @param key       The key to be indexed
 * @param capacity  The current capacity of the hash table the key is going to be stored in
 * 
 * @return The index for the provided key
 */
unsigned int hash_index(const char *key, int capacity)
{
    return hash(key) % capacity;
}

HashTable_t *hash_create_table(int capacity)
{
    HashTable_t *table = malloc(sizeof(HashTable_t));
    if(table == NULL) return NULL;

    table->capacity = capacity;
    table->stored = 0;
    table->buckets = malloc(sizeof(Entry_t *) * capacity);

    if(table->buckets == NULL)
    {
        free(table);
        return NULL;
    }

    for (int i = 0; i < capacity; i++)
    {
        table->buckets[i] = NULL;
    }
    
    return table;
}

void hash_free_table(HashTable_t *table)
{
    for (int i = 0; i < table->capacity; i++)
    {
        Entry_t *entry = table->buckets[i];
        while (entry != NULL)
        {
            Entry_t *next = entry->next;
            free((void *)entry->key);
            free((void *)entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table);
}

/**
 * @brief Resize the hash table capacity
 * 
 * @param table The hash table to be resized
 */
static void resize_table(HashTable_t *table)
{
    int new_capacity = table->capacity * 2;
    Entry_t **new_buckets = malloc(sizeof(Entry_t *) * new_capacity);
    if(!new_buckets) return;

    for (int i = 0; i < new_capacity; i++)
    {
        new_buckets[i] = NULL;
    }

    for (int i = 0; i < table->capacity; i++)
    {
        Entry_t *entry = table->buckets[i];
        while (entry != NULL)
        {
            unsigned int new_index = hash_index(entry->key, new_capacity);
            Entry_t *next_entry = entry->next;

            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next_entry;
        }
    }
    
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;
}

Entry_t *hash_search_table(HashTable_t *table, const char *key)
{
    unsigned int index = hash_index(key, table->capacity);
    Entry_t *entry = table->buckets[index], *prev_entry = NULL;

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            return entry;
        }
        prev_entry = entry;
        entry = entry->next;
    }

    return prev_entry;
}

hash_error hash_insert_entry(HashTable_t *table, const char *key, const char *value, ConflictFlags flag)
{
    if (key == NULL || table == NULL)
        return INVALID_PARAMS;
    
    Entry_t *entry = hash_search_table(table, key);

    if (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            switch (flag)
            {
            case REJECT:
                return ENTRY_REJECTED;
            case UPDATE_VALUE:
                free((void *)entry->value);
                entry->value = set_value(value);
                return ENTRY_UPDATED;
            default:
                return UNKNOWN_FLAG;
            }

        }else
        {
            Entry_t *new_entry = malloc(sizeof(Entry_t));
            new_entry->key = set_value(key);
            new_entry->value = (value == NULL) ? NULL : set_value(value);
            new_entry->next = NULL;

            entry->next = new_entry;
        }
    }else
    {
        entry = malloc(sizeof(Entry_t));
        entry->key = set_value(key);
        entry->value = (value == NULL) ? NULL : set_value(value);
        entry->next = NULL;

        if (((float)(table->stored + 1) / table->capacity) > LOAD_FACTOR_THRESHOLD)
        {
            resize_table(table);
        }

        unsigned int index = hash_index(key, table->capacity);
        table->buckets[index] = entry;
        table->stored++;
    }

    return HASH_OK;
}

hash_error hash_delete_entry(HashTable_t *table, const char *key)
{
    unsigned int index = hash_index(key, table->capacity);

    if (hash_search_table(table, key) == NULL)
        return ENTRY_NOT_FOUND;
    
    Entry_t *entry = table->buckets[index];
    Entry_t *previous_entry = entry;

    while (entry != previous_entry->next)
        previous_entry = previous_entry->next;
    
    previous_entry->next = entry->next;

    free((void *)entry->key);
    free((void *)entry->value);
    free(entry);
    table->stored--;

    return HASH_OK;
}