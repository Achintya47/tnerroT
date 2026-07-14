#include <stdlib.h>
#include <string.h>

#include "RFC1738_url_parser.h"

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

/**
 * @brief Splits "<scheme>host[:port][/path]" into host / port / path.
 *        Shared by parse_announce_url() (http) and parse_udp_announce_url() (udp).
 */
static int parse_url_generic(const char* url, const char* scheme,
    char** host_out, unsigned short* port_out, char** path_out) {

    if (!url || !scheme || !host_out || !port_out)
        return 0;

    size_t scheme_len = strlen(scheme);

    if (strncmp(url, scheme, scheme_len) != 0)
        return 0;

    const char* p = url + scheme_len;

    /* host runs until ':' (port), '/' (path) or end of string */
    const char* host_end = p;

    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    size_t host_len = (size_t)(host_end - p);

    if (host_len == 0)
        return 0;

    char* host = malloc(host_len + 1);

    if (!host)
        return 0;

    memcpy(host, p, host_len);
    host[host_len] = '\0';

    /* optional ":port" */
    unsigned short port = 80;
    const char* cursor = host_end;

    if (*cursor == ':') {
        cursor++;

        long parsed_port = strtol(cursor, (char**)&cursor, 10);

        if (parsed_port <= 0 || parsed_port > 65535) {
            free(host);
            return 0;
        }

        port = (unsigned short)parsed_port;
    }

    *host_out = host;
    *port_out = port;

    if (path_out) {
        /* whatever remains is the path; default to "/" if none given */
        const char* path_src = (*cursor == '/') ? cursor : "/";
        size_t path_len = strlen(path_src);

        char* path = malloc(path_len + 1);

        if (!path) {
            free(host);
            return 0;
        }

        memcpy(path, path_src, path_len + 1); /* includes null terminator */
        *path_out = path;
    }

    return 1;
}

/**
 * @brief Splits "http://host[:port][/path]" into host / port / path.
 * @see RFC1738_url_parser.h for full contract.
 */
int parse_announce_url(const char* url, char** host_out,
    unsigned short* port_out, char** path_out) {

    if (!path_out)
        return 0;

    return parse_url_generic(url, "http://", host_out, port_out, path_out);
}

/**
 * @brief Splits "udp://host:port[/anything]" into host / port.
 *        UDP trackers (BEP 15) don't use a path, so unlike the http
 *        variant there's nothing to hand back besides host and port.
 * @see RFC1738_url_parser.h for full contract.
 */
int parse_udp_announce_url(const char* url, char** host_out,
    unsigned short* port_out) {

    return parse_url_generic(url, "udp://", host_out, port_out, NULL);
}