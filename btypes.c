#include "btypes.h"
#include <stdio.h>

/* Memory Allocation errors are being propogated as of now, will decide on whether to 
throw errors right away, or implement logging + error handling, or a custom handler*/
BValue* create_int(long long value) {
	BValue* v = malloc(sizeof(BValue));
	if (v == NULL) 
		return NULL;
	
	v->type = BINT;
	v->value.integer.value = value;

	return v;
}

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

void list_append(BValue* list, BValue* item) {
	if (list == NULL || list->type != BLIST)
		return;
	
	BList* l = &list->value.list;

	if (l->count == l->capacity) {
		l->capacity *= 2;

		l->items = realloc(
			l->items, 
			sizeof(BValue) * l->capacity
		);
	}

	l->items[l->count++] = item;
}

BValue* list_get(BValue* list, int index) {
	if (list == NULL || list->type != BLIST)
		return NULL;
	
	BList* l = &list->value.list;

	if (index < 0 || index > l->count)
		return NULL;
	
	return l->items[index];
}

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