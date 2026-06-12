#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btypes.h"


/* We don't really need the encoded strings, ofcourse we need it for
error checking and testing the bencoders, but we can do that while implementing the actual logic.
Actual Logic : Would be to write the encoded strings in a file/stream, further while decoding as well, they 
take input from a file stream, this was it is more standardised and we can moove forward with torrent files.*/

// ALSO REDUCES ALLOCATION OVERHEAD, FILE* HANDLES BUFFER
void encode_value(FILE* out, const BValue* value);
void encode_int(FILE* out, const BValue* value);
void encode_string(FILE* out, const BValue* value);
void encode_list(FILE* out, const BValue* value);
void encode_dict(FILE* out, const BValue* value);


void encode_int(FILE* out, const BValue* value)
{
    fprintf(out, "i%llde", value->value.integer.value);
}

void encode_string(FILE* out, const BValue* value)
{
    const BString* str = &value->value.string;

    fprintf(out, "%d:", str->length);
    fwrite(str->data, 1, str->length, out);
}

void encode_list(FILE* out, const BValue* value)
{
    const BList* list = &value->value.list;

    fputc('l', out);

    for (int i = 0; i < list->count; i++)
    {
        encode_value(out, list->items[i]);
    }

    fputc('e', out);
}

void encode_dict(FILE* out, const BValue* value)
{
    const BDict* dict = &value->value.dict;

    fputc('d', out);

    for (int i = 0; i < dict->count; i++)
    {
        BDictEntry* entry = &dict->entries[i];

        fprintf(out, "%d:", entry->key.length);
        fwrite(entry->key.data, 1, entry->key.length, out);

        encode_value(out, entry->value);
    }

    fputc('e', out);
}

void encode_value(FILE* out, const BValue* value)
{
    switch (value->type)
    {
        case BINT:
            encode_int(out, value);
            break;

        case BSTRING:
            encode_string(out, value);
            break;

        case BLIST:
            encode_list(out, value);
            break;

        case BDICT:
            encode_dict(out, value);
            break;
    }
}


/* IMPLEMENTING DECODERS */