/* Initial plan was to consider URL-Safe and Unsafe characters for encoding,
but since most trackers encode the whole info-hash, we'll follow the same approach */
void encode_info_hash(const unsigned char hash[20],
    char output[61]) {
    static const char hex[] = "0123456789ABCDEF";

    for (int i = 0; i < 20; i++) {

        output[i * 3]     = '%';
        output[i * 3 + 1] = hex[(hash[i] >> 4) & 0x0F];
        output[i * 3 + 2] = hex[hash[i] & 0x0F];
    }

    output[60] = '\0';
}