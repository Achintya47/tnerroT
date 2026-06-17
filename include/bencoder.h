#include <stdio.h>
#include "btypes.h"

/* Bencoder Parser Struct */
typedef struct {
    const char* data;
    size_t size;
    size_t pos;
} Parser;

int parser_get(Parser* p);
int parser_peek(Parser* p);
int parser_read(Parser* p, void* destination, size_t length);

void encode_value(FILE* out, const BValue* value);
void encode_int(FILE* out, const BValue* value);
void encode_string(FILE* out, const BValue* value);
void encode_list(FILE* out, const BValue* value);
void encode_dict(FILE* out, const BValue* value);

BValue* decode_value(Parser* in);
BValue* decode_int(Parser* in);
BValue* decode_string(Parser* in, int first_digit);
BValue* decode_list(Parser* in);
BValue* decode_dict(Parser* in);