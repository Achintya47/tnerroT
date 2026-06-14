/**
 * @authors     : @Achintya47, @loki533
 * @date        : 12/06/2026
 * @brief       : Bencoding is a way to specify and organize data in a terse format. 
 * It supports the following types: byte strings, integers, lists, and dictionaries.
 * @details      : Bencoding Specifications
 * 1. Byte strings are encoded as follows: <string length encoded in base ten ASCII>:<string data>
 * 2. Integers are encoded as follows: i<integer encoded in base ten ASCII>e
 *      The initial i and trailing e are beginning and ending delimiters.
 * 3. Lists are encoded as follows: l<bencoded values>e
 *      The initial l and trailing e are beginning and ending delimiters. 
 *      Lists may contain any bencoded type, including integers, strings, dictionaries, 
 *      and even lists within other lists.  
 * 4. Dictionaries are encoded as follows: d<bencoded string><bencoded element>e
 *      The initial d and trailing e are the beginning and ending delimiters. Note that the keys must be bencoded strings.
 * @note        : For Bit-Torrent, dictionaries are the most important as the .torrent file contains the info-dict
 *              which is necessary for establishing connection and further downloading and verifying pieces.
 *              Thus for easy retrieval, dictionaries are maintained in sorted order.
 * @cite        : https://wiki.theory.org/BitTorrentSpecification#Metainfo_File_Structure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/bencoder.h"

/**
 * @brief Integers Encoding function, i<integer encoded in base ten ASCII>e
 * @param FILE* stream object, BValue* object
 * @return void
 */
void encode_int(FILE* out, const BValue* value)
{
    fprintf(out, "i%llde", value->value.integer.value);
}

/**
 * @brief String Encoding function, <string length encoded in base ten ASCII>:<string data>
 * @param FILE* stream object, BValue* object
 * @return void
 */
void encode_string(FILE* out, const BValue* value)
{
    const BString* str = &value->value.string;

    fprintf(out, "%d:", str->length);
    fwrite(str->data, 1, str->length, out);
}

/**
 * @brief List Encoding function, l<bencoded values>e
 * @param FILE* stream object, BValue* object
 * @return void
 */
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

/**
 * @brief Dictionary Encoding function, d<bencoded string><bencoded element>e
 * @param FILE* stream object, BValue* object
 * @return void
 */
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

/**
 * @brief Distributer function, calls object wise encoders
 * @param FILE* stream object, BValue* object
 * @return void
 */
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


/**
 * @brief Distributer Function, calls object based decoders
 * @param FILE stream pointer
 * @return BValue object : A wrapper for string, lists, dictionaries and integers
 */
BValue* decode_value(FILE* in) {
    int c = fgetc(in);

    if (c == EOF)
        return NULL;

    if (c == 'i')
        return decode_int(in);

    if (c == 'l')
        return decode_list(in);

    if (c == 'd')
        return decode_dict(in);

    if (c >= '0' && c <= '9')
        return decode_string(in, c);

    return NULL;
}

/**
 * @brief Decodes integers. Example : 'i32e' -> 32
 * @param FILE stream pointer
 * @return BValue object
 */
BValue* decode_int(FILE* in) {
    char buffer[32];
    int pos = 0;

    int c;

    while ((c = fgetc(in)) != 'e')
    {
        if (c == EOF)
            return NULL;

        /*
        BUG FIX : Buffer Overflow risk, thus buffer index validation
        */
        if (pos >= (int)sizeof(buffer)-1)
            return NULL;

        buffer[pos++] = (char)c;
    }

    buffer[pos] = '\0';

    long long value = atoll(buffer);

    return create_int(value);
}

/**
 * @brief Decodes strings. Example : '4:swag' -> 'swag'
 * @param FILE stream pointer
 * @return BValue object
 */
BValue* decode_string(FILE* in, int first_digit) {
    int length = first_digit - '0';

    int c;

    while ((c = fgetc(in)) != ':')
    {
        if (c == EOF)
            return NULL;
        
        /*
        BUG FIX : Protecting against malformed input
        Example : 4a:spam, here without validation, 'a' will be evaluated
        and 'a' - '0' will yield and accumulate in the length
        */
        if (c < '0' || c > '9')
            return NULL;

        length = length * 10 + (c - '0');
    }

    char* buffer = malloc(length);

    /*
    BUG FIX : File reading issue
    */
    if (fread(buffer, 1, length, in) != (size_t)length) {
        free(buffer);
        return NULL;
    }

    BValue* value =
        create_string(buffer, length);

    free(buffer);

    return value;
}

/**
 * @brief Decodes lists. Example : 'l4:eggs4:eggse' -> ['eggs', 'eggs']
 * @param FILE stream pointer
 * @return BValue object
 */
BValue* decode_list(FILE* in) {
    BValue* list = create_list();

    while (1)
    {
        int c = fgetc(in);

        if (c == EOF)
            return NULL;

        if (c == 'e')
            break;

        ungetc(c, in);

        BValue* item =
            decode_value(in);

        list_append(list, item);
    }

    return list;
}

/**
 * @brief Decodes Dictionaries.
 * Example : 'd3:cow3:moo4:spam4:eggse' -> { "cow" => "moo", "spam" => "eggs" }
 * @param FILE stream pointer
 * @return BValue object
 */
BValue* decode_dict(FILE* in) {
    BValue* dict = create_dict();

    while (1)
    {
        int c = fgetc(in);

        if (c == EOF)
            return NULL;

        if (c == 'e')
            break;

        if (!(c >= '0' && c <= '9'))
            return NULL;

        BValue* key =
            decode_string(in, c);

        BValue* value =
            decode_value(in);

        dict_insert(
            dict,
            key->value.string.data,
            key->value.string.length,
            value);

        destroy_value(key);
    }

    return dict;
}