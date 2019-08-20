#ifndef MM2XTGEOIP_H
#define MM2XTGEOIP_H

#define MAX_LINE 256
#define MAX_COLS 16
#define COUNTRY_CODE_SIZE 2
#define MAX_COUNTRIES UINT16_MAX
#define PROXY_GEONAME_ID (ULONG_MAX - 3)
#define SAT_GEONAME_ID (ULONG_MAX - 2)
#define OTHER_GEONAME_ID (ULONG_MAX - 1)
#define PROXY_COUNTRY_CODE "A1"
#define SAT_COUNTRY_CODE "A2"
#define OTHER_COUNTRY_CODE "O1"
#define DEFAULT_COUNTRY_FILE_NAME "GeoLite2-Country-Locations-en.csv"
#define DEFAULT_IPV4_RANGE_FILE_NAME "GeoLite2-Country-Blocks-IPv4.csv"
#define DEFAULT_IPV6_RANGE_FILE_NAME "GeoLite2-Country-Blocks-IPv6.csv"
#define DEFAULT_OUTPUT_DIRECTORY "/usr/share/xt_geoip"
#define IPV4_SUFFIX ".iv4"
#define IPV6_SUFFIX ".iv6"


typedef struct Arguments {
    bool forbid_filtered_countries;
    char *filtered_countries;
    bool no_virtual_countries;
    char *country_file;
    char *ipv4_file;
    char *ipv6_file;
    char *target_dir;
    bool verbose;
} Arguments;

typedef struct Country {
    unsigned long geoname_id;
    char country_code[COUNTRY_CODE_SIZE + 1];
    bool forbidden;
} Country;


static error_t parse_opt(int key, char *arg, struct argp_state *state);
inline bool str2bool(char *s);
inline bool geoname_id_reserved(unsigned long geoname_id);
inline Country *get_country(unsigned long geoname_id, bool proxy, bool sat, unsigned num_countries, Country *countries);
inline uint16_t country_code_pos(char *country_code);
void init_country_code_lookup(Country **country_code_lookup);
unsigned read_country_file(char *country_file_name, Country *countries, Country **country_code_lookup, char **err_msg);
unsigned add_virtual_countries(unsigned num_countries, Country *countries, Country **country_code_lookup);
unsigned set_filtered_countries(unsigned num_countries, Country *countries, Country **country_code_lookup, uint16_t *country_positions, bool forbid);
unsigned parse_country_code_list(char *country_codes, uint16_t *country_positions);
unsigned process_range_file(char *range_file_name, int addr_family, unsigned num_countries, Country *countries, char *output_directory, char **err_msg);
int main(int argc, char **argv);

#endif
