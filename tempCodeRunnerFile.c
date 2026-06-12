#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btypes.h"
#include <ctype.h>


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

BValue* decode_value(FILE* in);
BValue* decode_int(FILE* in);
BValue* decode_string(FILE* in);
BValue* decode_list(FILE* in);
BValue* decode_dict(FILE* in);


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
BValue* decode_int(FILE* in)
{
    long long value;

    if (fscanf(in, "%lld", &value) != 1)
        return NULL;

    if (fgetc(in) != 'e')
        return NULL;

    return create_int(value);
}

BValue* decode_string(FILE* in)
{
    int length = 0;
    int c;

    while ((c = fgetc(in)) != ':')
    {
        if (c == EOF || !isdigit(c))
            return NULL;

        length = length * 10 + (c - '0');
    }

    char* buffer = malloc(length + 1);

    if (!buffer)
        return NULL;

    if (fread(buffer, 1, length, in) != (size_t)length)
    {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';

    BValue* str = create_string(buffer, length);

    free(buffer);

    return str;
}
BValue* decode_list(FILE* in)
{
    BValue* list = create_list();

    while (1)
    {
        int c = fgetc(in);

        if (c == EOF)
        {
            destroy_value(list);
            return NULL;
        }

        if (c == 'e')
            break;

        ungetc(c, in);

        BValue* item = decode_value(in);

        if (!item)
        {
            destroy_value(list);
            return NULL;
        }

        list_append(list, item);
    }

    return list;
}

BValue* decode_dict(FILE* in)
{
    BValue* dict = create_dict();

    while (1)
    {
        int c = fgetc(in);

        if (c == EOF)
        {
            destroy_value(dict);
            return NULL;
        }

        if (c == 'e')
            break;

        ungetc(c, in);

        BValue* key_obj = decode_string(in);

        if (!key_obj)
        {
            destroy_value(dict);
            return NULL;
        }

        BValue* value = decode_value(in);

        if (!value)
        {
            destroy_value(key_obj);
            destroy_value(dict);
            return NULL;
        }

        dict_insert(
            dict,
            key_obj->value.string.data,
            key_obj->value.string.length,
            value
        );

        destroy_value(key_obj);
    }

    return dict;
}

BValue* decode_value(FILE* in)
{
    int c = fgetc(in);

    if (c == EOF)
        return NULL;

    switch (c)
    {
        case 'i':
            return decode_int(in);

        case 'l':
            return decode_list(in);

        case 'd':
            return decode_dict(in);

        default:
            if (isdigit(c))
            {
                ungetc(c, in);
                return decode_string(in);
            }
    }

    return NULL;
}

#include <stdio.h>
#include "btypes.h"

int main(void)
{
    FILE* f = fopen("test.benc", "wb");

    if (!f)
    {
        perror("fopen");
        return 1;
    }

    BValue* root = create_list();

    list_append(root, create_string("spam", 4));
    list_append(root, create_int(42));

    encode_value(f, root);

    fclose(f);

    destroy_value(root);

    f = fopen("test.benc", "rb");

    if (!f)
    {
        perror("fopen");
        return 1;
    }

    BValue* decoded = decode_value(f);

    fclose(f);

    if (!decoded)
    {
        printf("Decode failed\n");
        return 1;
    }

    if (decoded->type == BLIST)
    {
        printf("List contains %d items\n",
               decoded->value.list.count);

        BValue* first = list_get(decoded, 0);
        BValue* second = list_get(decoded, 1);

        if (first && first->type == BSTRING)
        {
            printf("String: %.*s\n",
                   first->value.string.length,
                   first->value.string.data);
        }

        if (second && second->type == BINT)
        {
            printf("Integer: %lld\n",
                   second->value.integer.value);
        }
    }

    destroy_value(decoded);

    return 0;
}