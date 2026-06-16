/* Since URLs can only safely contain certain characters. 
Some bytes correspond to printable characters, others don't,
 and some have special meanings in URLs.*/

int is_url_safe(unsigned char c) {
    return
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '.' ||
        c == '-' ||
        c == '_' ||
        c == '~';
}