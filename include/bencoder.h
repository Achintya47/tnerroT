#include "include/btypes.h"
#include <stdio.h>

void encode_value(FILE* out, const BValue* value);
void encode_int(FILE* out, const BValue* value);
void encode_string(FILE* out, const BValue* value);
void encode_list(FILE* out, const BValue* value);
void encode_dict(FILE* out, const BValue* value);

BValue* decode_value(FILE* in);

BValue* decode_int(FILE* in);
BValue* decode_string(FILE* in, int first_digit);
BValue* decode_list(FILE* in);
BValue* decode_dict(FILE* in);