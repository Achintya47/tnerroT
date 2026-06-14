/**
 * @authors : @Achintya47, @loki533
 * @date 	: 12/06/2026
 * @brief 	: Standard BitTorrent protocol has four major data types, integers, strings, lists and dictionaries.
 * 			All are standard datatypes, Lists are heterogenous, thus BValue datatype was created.
 * 			Further, for consistency and simple function headers, integers (int num) and strings (char* str) were also
 * 			wrapped inside BValue objects, thus this here was a tradeoff, a slight complex design was choose, for developer ease.
 */

#include "include/btypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief : Wraps integers in BValue object
 * @param : long long value integer
 * @return : BValue* pointer to the object
 */
BValue* create_int(long long value) {
	BValue* v = malloc(sizeof(BValue));
	if (v == NULL) 
		return NULL;
	
	v->type = BINT;
	v->value.integer.value = value;

	return v;
}

/**
 * @brief : Wraps strings in BValue object
 * @param : char* array and int length
 * @return : BValue* pointer to the object
 */
BValue* create_string(const char* data, int length) {
	BValue* v = malloc(sizeof(BValue));
	if (v == NULL)
		return NULL;

	v->type = BSTRING;
	v->value.string.data = malloc(length);
	memcpy(v->value.string.data, data, length);

	v->value.string.length = length;

	return v;
}

/**
 * @brief : Allocates space for BValue list object
 * @param : void
 * @return : BValue* pointer to the object
 */
BValue* create_list(void) {
	BValue* v = malloc(sizeof(BValue));
	if (v == NULL)
		return NULL;

	v->type = BLIST;

	v->value.list.count = 0;
	v->value.list.capacity = 4;

	v->value.list.items = malloc(sizeof(BValue*) * v->value.list.capacity);

	return v;
}

/**
 * @brief : Allocates space for BValue dict object
 * @param : void
 * @return : BValue* pointer to the object
 */
BValue* create_dict(void) {
	BValue* v = malloc(sizeof(BValue));
	if (v == NULL)
		return NULL;

	v->type = BDICT;

	v->value.dict.count = 0;
	v->value.dict.capacity = 4;

	v->value.dict.entries = malloc(sizeof(BDictEntry) * v->value.dict.capacity);

	return v;
}

/**
 * @brief : Append a BValue item to a BValue list
 * @param : BValue* list and BValue* item to be appended
 * @return : void
 */
void list_append(BValue* list, BValue* item) {
	if (list == NULL || list->type != BLIST)
		return;
	
	BList* l = &list->value.list;

	if (l->count == l->capacity) {
		l->capacity *= 2;

		/*
		BUG FIX : reallocating BValue instead of BValue*, as items
		is a BValue** pointer
		*/
		l->items = realloc(
			l->items, 
			sizeof(BValue*) * l->capacity
		);
	}

	l->items[l->count++] = item;
}

/**
 * @brief : Get a BValue item from a BValue list based on index
 * @param : BValue* list and int index of the item
 * @return : BValue* item returned
 */
BValue* list_get(BValue* list, int index) {
	if (list == NULL || list->type != BLIST)
		return NULL;
	
	BList* l = &list->value.list;

	/*
	BUG FIX : index > l->count , thus case of index == l->count will be missed 
	*/
	if (index < 0 || index >= l->count)
		return NULL;
	
	return l->items[index];
}

/**
 * @brief : Invert a string key : BValue value pair in a BValue dict
 * @param : BValue* dict, char* key, int key_length, BValue* value -> key's pair
 * @return : void
 */
void dict_insert(BValue* dict, const char* key, int key_length, BValue* value) {
	
	if (dict == NULL || dict->type != BDICT)
		return;
	
	BDict* d = &dict->value.dict;

	if (d->count == d->capacity) {
		d->capacity *= 2;

		d->entries = realloc(
			d->entries,
			sizeof(BDictEntry) * d->capacity
		);
	}

	BDictEntry* entry = &d->entries[d->count++];

	entry->key.data = malloc(key_length);
	memcpy(entry->key.data, key, key_length);

	entry->key.length = key_length;
	entry->value = value;

}

/**
 * @brief : Get a BValue item from a BValue dict based on a string key
 * @param : BValue* dict, char* key and int key_length
 * @return : BValue* object
 */
BValue* dict_get(BValue* dict, const char* key, int key_length) {
	if (dict == NULL || dict->type != BDICT)
		return NULL;
	
	BDict* d = &dict->value.dict;

	for (int i = 0; i < d->count; i++) {
		BDictEntry* entry = &d->entries[i];

		if (entry->key.length != key_length)
            continue;

		if (memcmp(entry->key.data, key, key_length) == 0)
            return entry->value;
	}

	return NULL;
}

/**
 * @brief : Deallocates space allocated for each BValue object, generalized de-allocator, works for all
 * @param : BValuez* Object
 * @return : void
 */
void destroy_value(BValue* value) {
    if (value == NULL)
        return;

    switch (value->type)
    {
        case BINT:
            break;

        case BSTRING:
            free(value->value.string.data);
            break;

        case BLIST:
        {
            BList* list = &value->value.list;

            for (int i = 0; i < list->count; i++)
                destroy_value(list->items[i]);

            free(list->items);
            break;
        }

        case BDICT:
        {
            BDict* dict = &value->value.dict;

            for (int i = 0; i < dict->count; i++)
            {
                free(dict->entries[i].key.data);
                destroy_value(dict->entries[i].value);
            }

            free(dict->entries);
            break;
        }
    }

    free(value);
}