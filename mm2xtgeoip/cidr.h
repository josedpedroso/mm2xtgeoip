#ifndef CIDR_H
#define CIDR_H

#define IPV4_BYTES 4
#define IPV6_BYTES 16

typedef struct AddressRange {
    int addr_family;
    size_t addr_bytes;
    uint8_t prefix_length;
    uint8_t base[IPV6_BYTES];
    uint8_t mask[IPV6_BYTES];
    uint8_t start[IPV6_BYTES];
    uint8_t end[IPV6_BYTES];
} AddressRange;

bool parse_cidr(char *cidr, AddressRange *range);
bool unparse_cidr(AddressRange *range, char *dest, size_t length);
int compare_addrs(uint8_t *addr1, uint8_t *addr2, int addr_family);
bool inc_addr(uint8_t *addr, int addr_family, int inc_dec);
bool ranges_contiguous(AddressRange *range1, AddressRange *range2);

#endif
