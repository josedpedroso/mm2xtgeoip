//#define _GNU_SOURCE

#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <arpa/inet.h>
#include <argp.h>

#include "csv.h"
#include "cidr.h"
#include "mm2xtgeoip.h"



const char *argp_program_version = "mm2xtgeoip 0.9";

const char *argp_program_bug_address = "https://github.com/josedpedroso/mm2xtgeoip/issues";

static char argp_doc[] = "mm2xtgeoip -- converts MaxMind geoip CSV databases to the format used by the xtables geoip match module\v"
                         "Return values:\n"
                         "    0 - Success\n"
                         "    1 - Unable to process country file\n"
                         "    2 - Unable to process range files\n"
                         "Other - Unable to parse command-line arguments";

static struct argp_option argp_options[] = {
    {"allow-countries",      'a', "COUNTRIES", 0, "Process ranges only from the specified comma-separated country codes. "
                                                  "Can't be used with -f (--forbid-countries)."},
    {"forbid-countries",     'f', "COUNTRIES", 0, "Process all ranges but those from the specified comma-separated country codes. "
                                                  "Can't be used with -a (--allow-countries)."},
    {"no-virtual-countries", 'n', 0, 0, "Do not process ranges for virtual countries "
                                        "(A1 -- proxies; A2 -- satellite providers; O1 -- unknown). "
                                        "Same as -f A1,A2,O1."},
    {"country-file",         'c', "FILE", 0, "Use the specified CSV file as source for country data. "
                                             "Default: " DEFAULT_COUNTRY_FILE_NAME},
    {"ipv4-file",            '4', "FILE", OPTION_ARG_OPTIONAL, "Use the specified CSV file as source for IPv4 ranges. "
                                                               "If you use this option without specifying a FILE, no IPv4 ranges will be processed. "
                                                               "Default: " DEFAULT_IPV4_RANGE_FILE_NAME},
    {"ipv6-file",            '6', "FILE", OPTION_ARG_OPTIONAL, "Use the specified CSV file as source for IPv6 ranges. "
                                                               "If you use this option without specifying a FILE, no IPv6 ranges will be processed. "
                                                               "Default: " DEFAULT_IPV6_RANGE_FILE_NAME},
    {"target-dir",           'd', "DIRECTORY", 0, "Write output files to the specified directory. "
                                                  "Default: " DEFAULT_OUTPUT_DIRECTORY},
    {"verbose",              'v', 0, 0, "Write details of the program's activity to stdout. "
                                        "Without this option, only error messages will be written (to stderr)."},
    {0}
};


static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    Arguments *arguments = state->input;
    
    switch (key) {
        case 'a':
            if (arguments->filtered_countries != NULL) {
                fputs("Can't specify both allowed and forbidden countries.\n", stderr);
                argp_usage(state);
            }
            
            arguments->forbid_filtered_countries = false;
            arguments->filtered_countries = arg;
            break;
        
        case 'f':
            if (arguments->filtered_countries != NULL) {
                fputs("Can't specify both forbidden and allowed countries.\n", stderr);
                argp_usage(state);
            }
            
            arguments->forbid_filtered_countries = true;
            arguments->filtered_countries = arg;
            break;
        
        case 'n':
            arguments->no_virtual_countries = true;
            break;
        
        case 'c':
            arguments->country_file = arg;
            break;
        
        case '4':
            if (arg)
                arguments->ipv4_file = arg;
            else
                arguments->ipv4_file = NULL;
            break;
        
        case '6':
            if (arg)
                arguments->ipv6_file = arg;
            else
                arguments->ipv6_file = NULL;
            break;
        
        case 'd':
            arguments->target_dir = arg;
            break;
        
        case 'v':
            arguments->verbose = true;
            break;
        
        case ARGP_KEY_END:
            break;
        
        case ARGP_KEY_ARG:
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static struct argp argp_parser = {argp_options, parse_opt, 0, argp_doc};


inline bool str2bool(char *s) {
    if (!s[0] || strcmp(s, "0") == 0) {
        return false;
    }
    else {
        return true;
    }
}

//checks whether a geoname_id is reserved for internal use by the program
inline bool geoname_id_reserved(unsigned long geoname_id) {
    if (geoname_id == PROXY_GEONAME_ID || geoname_id == SAT_GEONAME_ID ||
        geoname_id == OTHER_GEONAME_ID) {
        return true;
    }
    else {
        return false;
    }
}

//searches for a country in countries by geoname_id
inline Country *get_country(unsigned long geoname_id, bool proxy, bool sat, unsigned num_countries, Country *countries) {
    unsigned start = 0;
    unsigned mid;
    unsigned end = num_countries - 1;
    static unsigned long cached_geoname_id = 0;
    static Country *cached_country_arr = NULL;
    static Country *cached_country = NULL;
    
    //nothing to do
    if (!num_countries) {
        return NULL;
    }
    
    if (proxy) {
        geoname_id = PROXY_GEONAME_ID;
    }
    else if (sat) {
        geoname_id = SAT_GEONAME_ID;
    }
    else if (!geoname_id) {
        geoname_id = OTHER_GEONAME_ID;
    }
    
    //ranges for a particular country are often contiguous
    //caching the last country might save some time
    if (cached_country_arr == countries && geoname_id == cached_geoname_id) {
        return cached_country;
    }
    
    cached_geoname_id = geoname_id;
    cached_country_arr = countries;
    
    //the countries are sorted, so use binary search
    while (start <= end) {
        mid = start + (end - start) / 2;
        
        if (countries[mid].geoname_id == geoname_id) {
            //found
            cached_country = &countries[mid];
            return cached_country;
        }
        
        if (countries[mid].geoname_id < geoname_id) {
            start = mid + 1;
        }
        else {
            end = mid - 1;
        }
    }
    
    //not found
    cached_country = NULL;
    return NULL;
}

//converts a 2-letter country code to an uint16_t that can be used as an index
inline uint16_t country_code_pos(char *country_code) {
    if (!country_code[0] || !country_code[1] || country_code[2]) {
        return 0;
    }
    
    char buf[COUNTRY_CODE_SIZE];
    
    buf[0] = toupper(country_code[0]);
    buf[1] = toupper(country_code[1]);
    
    uint16_t *pos = (uint16_t *)buf;
    
    return *pos;
}

//initializes country_code_lookup with null pointers
void init_country_code_lookup(Country **country_code_lookup) {
    unsigned i;
    
    for (i = 0; i < MAX_COUNTRIES; i++) {
        country_code_lookup[i] = NULL;
    }
}

//populates a country array and its country code lookup array with data from a country file
//assumes country_code_lookup has been initialized with NULL pointers for its empty slots
unsigned read_country_file(char *country_file_name, Country *countries, Country **country_code_lookup, char **err_msg) {
    const unsigned MIN_COLS = 3;
    const unsigned GEONAME_ID_COL_IDX = 0;
    const unsigned CONTINENT_CODE_COL_IDX = 1;
    const unsigned COUNTRY_CODE_COL_IDX = 2;
    const char* REQUIRED_COLS[] = {
        "geoname_id",
        "continent_code",
        "country_iso_code"
    };
    
    FILE *country_file;
    static char err_msg_buf[MAX_LINE];
    char line[MAX_LINE];
    char *line_data[MAX_COLS];
    char *country_code;
    unsigned i;
    unsigned num_cols;
    unsigned highest_col;
    unsigned geoname_id_col;
    unsigned continent_code_col;
    unsigned country_code_col;
    unsigned line_num = 0;
    unsigned num_countries = 0;
    unsigned column_positions[MIN_COLS];
    unsigned long last_geoname_id = 0;
    unsigned long geoname_id;
    uint16_t country_pos;
    
    //default error message
    *err_msg = "No usable data in file.";
    
    country_file = fopen(country_file_name, "r");
    if (country_file == NULL) {
        *err_msg = "Error opening file.";
        return 0;
    }
    
    for (line_num = 1; ; line_num++) {
        //read line
        if (fgets(line, MAX_LINE, country_file) == NULL) {
            //could be an error or could be eof
            if (ferror(country_file)) {
                *err_msg = "Read error.";
                num_countries = 0;
            }
            goto end;
        }
        
        if (line_num == MAX_COUNTRIES) {
            *err_msg = "File too long.";
            num_countries = 0;
            goto end;
        }
        
        switch (strlen(line)) {
            case 0:
                //skip empty lines
                continue;
            
            case MAX_LINE - 1:
                *err_msg = "Line too long.";
                num_countries = 0;
                goto end;
        }
        
        num_cols = tokenize_csv(line, line_data, MAX_COLS);
        
        if (line_num == 1) {
            //this is the header, find the position of the required columns
            if (detect_columns(line_data, num_cols, REQUIRED_COLS, column_positions, MIN_COLS, &highest_col) != MIN_COLS) {
                *err_msg = "Required columns not found in header.";
                num_countries = 0;
                goto end;
            }
            
            geoname_id_col = column_positions[GEONAME_ID_COL_IDX];
            continent_code_col = column_positions[CONTINENT_CODE_COL_IDX];
            country_code_col = column_positions[COUNTRY_CODE_COL_IDX];
            
            //nothing else to do with the header, move on to the next line
            continue;
        }
        
        if (num_cols < highest_col + 1) {
            *err_msg = "Insufficient columns.";
            num_countries = 0;
            goto end;
        }
        
        geoname_id = strtoul(line_data[geoname_id_col], NULL, 10);
        if (geoname_id <= last_geoname_id) {
            *err_msg = "Invalid, duplicate, or unsorted geoname_id.";
            num_countries = 0;
            goto end;
        }
        
        if (geoname_id_reserved(geoname_id)) {
            *err_msg = "Reserved geoname_id.";
            num_countries = 0;
            goto end;
        }
        
        //country code may be empty
        //if so, use continent code
        if (line_data[country_code_col][0]) {
            country_code = line_data[country_code_col];
        }
        else {
            country_code = line_data[continent_code_col];
        }
        
        country_pos = country_code_pos(country_code);
        if (!country_pos) {
            //invalid country code, skip line
            continue;
        }
        
        if (country_code_lookup[country_pos] != NULL) {
            //duplicate country code, skip line
            continue;
        }
        
        //store data
        countries[num_countries].geoname_id = geoname_id;
        strcpy(countries[num_countries].country_code, country_code);
        countries[num_countries].forbidden = false;
        country_code_lookup[country_pos] = &countries[num_countries];
        
        num_countries++;
    }
    
    end:
    
    fclose(country_file);
    
    //clear default error message
    if (num_countries) {
        *err_msg = NULL;
    }
    else if (line_num) {
        //add line number to error message
        snprintf(err_msg_buf, MAX_LINE, "%s (Line %u)", *err_msg, line_num);
        *err_msg = err_msg_buf;
    }
    
    return num_countries;
}

//adds virtual countries (proxies, sat providers, and unknown ranges) to a country array and its country code lookup array
//because virtual countries have very high geoname_ids, this should be called only after adding real countries from a country file
unsigned add_virtual_countries(unsigned num_countries, Country *countries, Country **country_code_lookup) {
    uint16_t country_pos;
    
    //add proxies (A1)
    countries[num_countries].geoname_id = PROXY_GEONAME_ID;
    strcpy(countries[num_countries].country_code, PROXY_COUNTRY_CODE);
    countries[num_countries].forbidden = false;
    country_pos = country_code_pos(PROXY_COUNTRY_CODE);
    country_code_lookup[country_pos] = &countries[num_countries];
    
    num_countries++;
    
    //add sat providers (A2)
    countries[num_countries].geoname_id = SAT_GEONAME_ID;
    strcpy(countries[num_countries].country_code, SAT_COUNTRY_CODE);
    countries[num_countries].forbidden = false;
    country_pos = country_code_pos(SAT_COUNTRY_CODE);
    country_code_lookup[country_pos] = &countries[num_countries];
    
    num_countries++;
    
    //add unknown ranges (O1)
    countries[num_countries].geoname_id = OTHER_GEONAME_ID;
    strcpy(countries[num_countries].country_code, OTHER_COUNTRY_CODE);
    countries[num_countries].forbidden = false;
    country_pos = country_code_pos(OTHER_COUNTRY_CODE);
    country_code_lookup[country_pos] = &countries[num_countries];
    
    return 3;
}

//sets or clears the forbidden attribute of countries specified by a 0-terminated array of country positions
//if forbid is true, only the countries specified are forbidden
//else, only those are allowed
unsigned set_filtered_countries(unsigned num_countries, Country *countries, Country **country_code_lookup, uint16_t *country_positions, bool forbid) {
    unsigned i;
    unsigned processed = 0;
    uint16_t country_pos;
    
    if (!num_countries) {
        //nothing to do
        return;
    }
    
    if (!forbid) {
        //only the countries in country_positions are allowed
        //so forbid all by default
        for (i = 0; i < num_countries; i++) {
            countries[i].forbidden = true;
        }
    }
    
    i = 0;
    country_pos = country_positions[0];
    for (; country_pos != 0; country_pos = country_positions[++i]) {
        if (country_code_lookup[country_pos] == NULL) {
            //country_positions may contain positions for unknown country codes
            continue;
        }
        
        if (country_code_lookup[country_pos]->forbidden != forbid) {
            //country_positions may contain duplicates
            country_code_lookup[country_pos]->forbidden = forbid;
            processed++;
        }
    }
    
    return processed;
}

//parses a comma-separated list of country codes into a 0-terminated array of country positions
unsigned parse_country_code_list(char *country_codes, uint16_t *country_positions) {
    char *parsed_country_codes[MAX_COUNTRIES];
    unsigned num_countries;
    unsigned i;
    
    num_countries = tokenize_csv(country_codes, parsed_country_codes, MAX_COUNTRIES);
    
    for (i = 0; i < num_countries; i++) {
        *country_positions = country_code_pos(parsed_country_codes[i]);
        
        //ensure only valid country codes are left in the array
        if (*country_positions) {
            country_positions++;
        }
    }
    *country_positions = 0;
    
    return num_countries;
}

//writes ranges from a range file to multiple binary files
unsigned process_range_file(char *range_file_name, int addr_family, unsigned num_countries, Country *countries, char *output_directory, char **err_msg) {
    const unsigned MIN_COLS = 5;
    const unsigned CIDR_COL_IDX = 0;
    const unsigned GEONAME_ID_COL_IDX = 1;
    const unsigned REGISTERED_GEONAME_ID_COL_IDX = 2;
    const unsigned PROXY_COL_IDX = 3;
    const unsigned SAT_COL_IDX = 4;
    const char* REQUIRED_COLS[] = {
        "network",
        "geoname_id",
        "registered_country_geoname_id",
        "is_anonymous_proxy",
        "is_satellite_provider"
    };
    
    FILE *range_file;
    FILE *out_files[MAX_COUNTRIES];
    static char err_msg_buf[MAX_LINE];
    char line[MAX_LINE];
    char *line_data[MAX_COLS];
    char *geoname_id_str;
    char *country_code;
    char *file_name_suffix;
    char *output_file_name = NULL;
    unsigned output_file_name_len;
    unsigned i;
    unsigned num_cols;
    unsigned highest_col;
    unsigned cidr_col;
    unsigned geoname_id_col;
    unsigned registered_geoname_id_col;
    unsigned proxy_col;
    unsigned sat_col;
    unsigned line_num = 0;
    unsigned num_ranges = 0;
    unsigned column_positions[MIN_COLS];
    unsigned long geoname_id;
    uint16_t country_pos;
    uint16_t last_country_pos = 0;
    Country *country;
    AddressRange range;
    AddressRange last_range;
    bool proxy;
    bool sat;
    bool write_start;
    
    //default error message
    *err_msg = "No usable data in file.";
    
    if (!num_countries) {
        //nothing to do
        *err_msg = "No countries to process.";
        return 0;
    }
    
    if (addr_family == AF_INET) {
        file_name_suffix = IPV4_SUFFIX;
    }
    else if (addr_family == AF_INET6) {
        file_name_suffix = IPV6_SUFFIX;
    }
    else {
        *err_msg = "Invalid address family.";
        return 0;
    }
    
    range_file = fopen(range_file_name, "r");
    if (range_file == NULL) {
        *err_msg = "Error opening file.";
        return 0;
    }
    
    for (i = 0; i < MAX_COUNTRIES; i++) {
        out_files[i] = NULL;
    }
    
    //allocate buffer for output file name
    output_file_name_len = strlen(output_directory) + 1;
    output_file_name_len += COUNTRY_CODE_SIZE + strlen(file_name_suffix) + 1;
    output_file_name = malloc(output_file_name_len);
    if (output_file_name == NULL) {
        *err_msg = "Error allocating buffer for output file name.";
        goto end;
    }
    
    //open output files
    for (i = 0; i < num_countries; i++) {
        if (countries[i].forbidden) {
            //don't open files for forbidden countries
            continue;
        }
        
        //get array position for ith country
        country_code = countries[i].country_code;
        country_pos = country_code_pos(country_code);
        
        //generate file name
        strcpy(output_file_name, output_directory);
        strcat(output_file_name, "/");
        strcat(output_file_name, country_code);
        strcat(output_file_name, file_name_suffix);
        
        //open file for writing
        out_files[country_pos] = fopen(output_file_name, "w");
        
        if (out_files[country_pos] == NULL) {
            *err_msg = "Error opening an output file.";
            goto end;
        }
    }
    
    for (line_num = 1; ; line_num++) {
        //read line
        if (fgets(line, MAX_LINE, range_file) == NULL) {
            if (ferror(range_file)) {
                *err_msg = "Read error.";
                num_ranges =  0;
                goto end;
            }
            else {
                break;
            }
        }
        
        switch (strlen(line)) {
            case 0:
                //skip empty lines
                continue;
            
            case MAX_LINE - 1:
                *err_msg = "Line too long.";
                num_ranges = 0;
                goto end;
        }
        
        num_cols = tokenize_csv(line, line_data, MAX_COLS);
        
        if (line_num == 1) {
            //this is the header, find the position of the required columns
            if (detect_columns(line_data, num_cols, REQUIRED_COLS, column_positions, MIN_COLS, &highest_col) != MIN_COLS) {
                *err_msg = "Required columns not found in header.";
                num_ranges = 0;
                goto end;
            }
            
            cidr_col = column_positions[CIDR_COL_IDX];
            geoname_id_col = column_positions[GEONAME_ID_COL_IDX];
            registered_geoname_id_col = column_positions[REGISTERED_GEONAME_ID_COL_IDX];
            proxy_col = column_positions[PROXY_COL_IDX];
            sat_col = column_positions[SAT_COL_IDX];
            
            //nothing else to do with the header, move on to the next line
            continue;
        }
        
        if (num_cols < highest_col + 1) {
            *err_msg = "Insufficient columns.";
            num_ranges = 0;
            goto end;
        }
        
        //geoname_id may be empty
        //if so, use registered_geoname_id
        if (isdigit(line_data[geoname_id_col][0])) {
            geoname_id_str = line_data[geoname_id_col];
        }
        else {
            geoname_id_str = line_data[registered_geoname_id_col];
        }
        
        geoname_id = strtoul(geoname_id_str, NULL, 10);
        if (geoname_id_reserved(geoname_id)) {
            *err_msg = "Reserved geoname_id.";
            num_ranges = 0;
            goto end;
        }
        
        proxy = str2bool(line_data[proxy_col]);
        sat = str2bool(line_data[sat_col]);
        
        country = get_country(geoname_id, proxy, sat, num_countries, countries);
        if (country == NULL) {
            //country not found, use O1
            country = get_country(OTHER_GEONAME_ID, false, false, num_countries, countries);
        }
        if (country == NULL) {
            //country not found, skip line
            continue;
        }
        
        if (country->forbidden) {
            //ignore ranges belonging to forbidden countries
            continue;
        }
        
        country_pos = country_code_pos(country->country_code);
        
        //country code was looked up from previously gathered info,
        //so it must be valid
        assert(country_pos);
        
        //there must be a valid open file for the country
        assert(out_files[country_pos] != NULL);
        
        //parse cidr to get start and end addresses
        if (!parse_cidr(line_data[cidr_col], &range)) {
            *err_msg = "Invalid CIDR.";
            num_ranges = 0;
            goto end;
        }
        
        if (range.addr_family != addr_family) {
            *err_msg = "Wrong address family.";
            num_ranges = 0;
            goto end;
        }
        
        write_start = true;
        
        //merge with last range?
        //this relies on the range file being sorted
        if (country_pos == last_country_pos && ranges_contiguous(&range, &last_range)) {
            //to merge, overwrite previous end address with current end address
            if (fseek(out_files[country_pos], range.addr_bytes * -1, SEEK_CUR) == 0) {
                write_start = false;
            }
            else {
                *err_msg = "Seek error.";
                num_ranges = 0;
                goto end;
            }
        }
        
        //write start and end addresses
        if (write_start && !fwrite(range.start, range.addr_bytes, 1, out_files[country_pos])) {
            *err_msg = "Error writing start address.";
            num_ranges = 0;
            goto end;
        }
        
        if (!fwrite(range.end, range.addr_bytes, 1, out_files[country_pos])) {
            *err_msg = "Error writing end address.";
            num_ranges = 0;
            goto end;
        }
        
        num_ranges++;
        last_country_pos = country_pos;
        last_range = range;
    }
    
    end:
    
    fclose(range_file);
    
    if (output_file_name != NULL) {
        free(output_file_name);
    }
    
    //close all output files
    for (i = 0; i < MAX_COUNTRIES; i++) {
        if (out_files[i] == NULL) {
            continue;
        }
        
        fclose(out_files[i]);
    }
    
    if (num_ranges) {
        //clear default error message
        *err_msg = NULL;
    }
    else if (line_num) {
        //add line number to error message
        snprintf(err_msg_buf, MAX_LINE, "%s (Line %u)", *err_msg, line_num);
        *err_msg = err_msg_buf;
    }
    
    return num_ranges;
}

int main(int argc, char **argv) {
    Arguments arguments;
    Country countries[MAX_COUNTRIES];
    Country *country_code_lookup[MAX_COUNTRIES];
    uint16_t filtered_country_pos[MAX_COUNTRIES];
    char *err_msg;
    unsigned num_countries;
    unsigned num_virtual_countries;
    unsigned num_filtered_countries;
    unsigned num_ipv4_ranges;
    unsigned num_ipv6_ranges;
    
    
    //set default arguments
    arguments.forbid_filtered_countries = false;
    arguments.filtered_countries = NULL;
    arguments.no_virtual_countries = false;
    arguments.country_file = DEFAULT_COUNTRY_FILE_NAME;
    arguments.ipv4_file = DEFAULT_IPV4_RANGE_FILE_NAME;
    arguments.ipv6_file = DEFAULT_IPV6_RANGE_FILE_NAME;
    arguments.target_dir = DEFAULT_OUTPUT_DIRECTORY;
    arguments.verbose = false;
    
    //parse arguments from command line
    argp_parse(&argp_parser, argc, argv, 0, 0, &arguments);
    
    
    init_country_code_lookup(country_code_lookup);
    
    
    //get countries from country file
    if (arguments.verbose) {
        printf("Processing country file (%s)...\n", arguments.country_file);
    }
    
    num_countries = read_country_file(arguments.country_file, countries, country_code_lookup, &err_msg);
    if (!num_countries) {
        fprintf(stderr, "Unable to process country file: %s\n", err_msg);
        return 1;
    }
    
    if (arguments.verbose) {
        printf("Read %u countries.\n", num_countries);
    }
    
    
    //add virtual countries (A1, A2, O1)
    if (!arguments.no_virtual_countries) {
        if (arguments.verbose) {
            printf("Adding virtual countries...\n");
        }
        
        num_virtual_countries = add_virtual_countries(num_countries, countries, country_code_lookup);
        assert(num_virtual_countries);
        num_countries += num_virtual_countries;
        
        if (arguments.verbose) {
            printf("Added %u virtual countries.\n", num_virtual_countries);
        }
    }
    
    
    //setup country filtering
    if (arguments.filtered_countries != NULL) {
        if (arguments.verbose) {
            printf("Setting up country filtering...\n");
        }
        
        num_filtered_countries = parse_country_code_list(arguments.filtered_countries, filtered_country_pos);
        num_filtered_countries = set_filtered_countries(num_countries, countries, country_code_lookup, filtered_country_pos, arguments.forbid_filtered_countries);
        
        if (arguments.verbose) {
            printf("Filtered by %u countries.\n", num_filtered_countries);
        }
    }
    
    
    //process IPv4 range file
    if (arguments.ipv4_file != NULL) {
        if (arguments.verbose) {
            printf("Processing IPv4 range file (%s)...\n", arguments.ipv4_file);
        }
        
        num_ipv4_ranges = process_range_file(arguments.ipv4_file, AF_INET, num_countries, countries, arguments.target_dir, &err_msg);
        if (num_ipv4_ranges) {
            if (arguments.verbose) {
                printf("Processed %u IPv4 ranges.\n", num_ipv4_ranges);
            }
        }
        else {
            fprintf(stderr, "Unable to process IPv4 range file: %s\n", err_msg);
        }
    }
    
    
    //process IPv6 range file
    if (arguments.ipv6_file != NULL) {
        if (arguments.verbose) {
            printf("Processing IPv6 range file (%s)...\n", arguments.ipv6_file);
        }
        
        num_ipv6_ranges = process_range_file(arguments.ipv6_file, AF_INET6, num_countries, countries, arguments.target_dir, &err_msg);
        if (num_ipv6_ranges) {
            if (arguments.verbose) {
                printf("Processed %u IPv6 ranges.\n", num_ipv6_ranges);
            }
        }
        else {
            fprintf(stderr, "Unable to process IPv6 range file: %s\n", err_msg);
        }
    }
    
    
    //return success if at least one of the range files had usable info
    if (num_ipv4_ranges || num_ipv6_ranges) {
        return EXIT_SUCCESS;
    }
    else {
        return 2;
    }
}
