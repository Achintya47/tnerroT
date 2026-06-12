#ifndef BTYPES_H
#define BTYPES_H


/* CHOICE OF MAKING STRUCTS FOR INTEGERS, STRINGS WAS DONE TO CREATE CONSISTENCY,
ALLL DEALINGS WILL NOW BE DONE USING BVALUE, FUNCTION DEFINITIONS WILL NOW LOOK CONSISTENT,
ACCEPTING BVALUE AND ERROR/TYPE CHECKING WILL BE DONE INSIDE.
THIS WAS ALOS DONE BECAUSE OF ADDING HETEROGENOUS LISTS AND DICTIONARIES*/

typedef struct BValue BValue;

typedef struct {
	long long value;
} BInt;

typedef struct {
	char* data;
	int length;
} BString;

typedef struct {
	BValue **items;
	int count;
	int capacity;
} BList;

typedef struct {
	BString key;
	BValue* value;
} BDictEntry;

typedef struct {
	BDictEntry* entries;
	int count;
	int capacity;
} BDict;

typedef enum {
	BINT,
	BSTRING,
	BDICT,
	BLIST
} BType;

/* Standardised container for all four type of objects, no confusion */
struct BValue {
    BType type;

    union {
        BInt integer;
		BString string;
		BList list;
		BDict dict;
    } value;
};

BValue* create_int(long long value);
BValue* create_string(const char* data, int length);
BValue* create_list(void);
BValue* create_dict(void);
void list_append(BValue* list, BValue* item);
BValue* list_get(BValue* list, int index);
void dict_insert(BValue* dict, const char* key, int key_length, BValue* value);
BValue* dict_get(BValue* dict, const char* key, int key_length);
void destroy_value(BValue* value);




#endif