void encode_info_hash(const unsigned char hash[20], char output[61]);

/**
 * @brief Splits an "http://host[:port][/path]" announce URL into its parts.
 *
 * @param url        Null-terminated announce URL (as read from the .torrent).
 * @param host_out   Receives a malloc'd, null-terminated hostname. Caller frees.
 * @param port_out   Receives the port (defaults to 80 if none given).
 * @param path_out   Receives a malloc'd, null-terminated path+query prefix
 *                    (e.g. "/announce"). Defaults to "/" if none given. Caller frees.
 * @return 1 on success, 0 on malformed input or unsupported scheme.
 *
 * @note Only the "http://" scheme is accepted. "https://" and "udp://" are
 *       rejected (return 0) — they are handled by separate code paths.
 */
int parse_announce_url(const char* url, char** host_out,
    unsigned short* port_out, char** path_out);