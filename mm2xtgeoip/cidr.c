#ifndef _STDIO_H_
#include <stdio.h>
#endif

#ifndef _STDINT_H
#include <stdint.h>
#endif

#ifndef __bool_true_false_are_defined
#include <stdbool.h>
#endif

#ifndef _CTYPE_H
#include <ctype.h>
#endif

#ifndef _STRING_H
#include <string.h>
#endif

#ifndef _ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifndef _ERRNO_H
#include <errno.h>
#endif

#include "cidr.h"

//parses a CIDR string and populates an AddressRange
bool parse_cidr(char *cidr, AddressRange *range) {
    char *separator;
    int base_valid;
    size_t mask_bytes;
    unsigned remaining_mask_bits;
    unsigned i;
    unsigned long prefix_length;
    
    if (!cidr[0] || !cidr[1] || !cidr[2] || !cidr[3]) {
        //nothing to do
        errno = 1;
        return false;
    }
    
    //detect ipv4/ipv6
    if (cidr[3] == '.' || cidr[2] == '.' || cidr[1] == '.') {
        range->addr_family = AF_INET;
        range->addr_bytes = IPV4_BYTES;
    }
    else {
        range->addr_family = AF_INET6;
        range->addr_bytes = IPV6_BYTES;
    }
    
    separator = strchr(cidr, '/');
    if (separator == NULL) {
        //cidr must include a slash
        errno = 2;
        return false;
    }
    
    //strtoul accepts whitespace, which isn't valid,
    //so ensure there's a number after the slash
    //
    //this also simplifies error handling by ensuring
    //there's at least one digit to parse, obviating
    //the endptr argument
    if (!isdigit(separator[1])) {
        errno = 3;
        return false;
    }
    
    //convert prefix length after the slash to int
    errno = 0;
    prefix_length = strtoul(separator + 1, NULL, 10);
    if (errno && !prefix_length) {
        //invalid prefix length value
        errno = 4;
        return false;
    }
    
    if (prefix_length > range->addr_bytes * 8) {
        //prefix can't be bigger than the address itself
        errno = 5;
        return false;
    }
    
    range->prefix_length = prefix_length;
    
    //terminate base address at the beginning of the prefix length
    *separator = '\0';
    
    //validate base address and convert it to a byte array
    base_valid = inet_pton(range->addr_family, cidr, range->base);
    
    //put the slash back so that the input remains unchanged
    *separator = '/';
    
    if (base_valid != 1) {
        errno = 6;
        return false;
    }
    
    //calculate mask size
    mask_bytes = prefix_length / 8;
    remaining_mask_bits = prefix_length % 8;
    
    //generate mask
    memset(range->mask, '\0', range->addr_bytes);
    
    if (mask_bytes) {
        memset(range->mask, '\xFF', mask_bytes);
    }
    
    if (remaining_mask_bits) {
        range->mask[mask_bytes] = '\xFF' << (8 - remaining_mask_bits);
    }
    
    //calculate start and end addresses
    for (i = 0; i < range->addr_bytes; i++) {
        range->start[i] = range->base[i] & range->mask[i];
        range->end[i] = range->start[i] | ~range->mask[i];
    }
    
    errno = 0;
	return true;
}

//turns an AddressRange back to a CIDR string
bool unparse_cidr(AddressRange *range, char *dest, size_t length) {
    char buf[INET6_ADDRSTRLEN];
    
    if (inet_ntop(range->addr_family, range->base, buf, sizeof(buf)) == NULL) {
        errno = 1;
        return false;
    }
    
    if (snprintf(dest, length, "%s/%u", buf, range->prefix_length) <= 0) {
        errno = 2;
        return false;
    }
    
    errno = 0;
    return true;
}

//compares 2 addresses of the same family
//returns 0 if the addresses are equal
//returns negative if addr1 comes before addr2 
//returns positive if addr1 comes after addr2
int compare_addrs(uint8_t *addr1, uint8_t *addr2, int addr_family) {
    size_t addr_bytes;
    
    if (addr_family == AF_INET6) {
        addr_bytes = IPV6_BYTES;
    }
    else {
        addr_bytes = IPV4_BYTES;
    }
    
    return memcmp(addr1, addr2, addr_bytes);
}

//increments or decrements an address by 1
//increments if inc_dec is positive
//decrements if inc_dec is negative
//does nothing if inc_dec is 0
bool inc_addr(uint8_t *addr, int addr_family, int inc_dec) {
    size_t addr_bytes;
    uint8_t find;
    uint8_t replace;
    unsigned i;
    
    if (addr_family == AF_INET) {
        addr_bytes = IPV4_BYTES;
    }
    else if (addr_family == AF_INET6){
        addr_bytes = IPV6_BYTES;
    }
    else {
        errno = 1;
        return false;
    }
    
    if (inc_dec > 0) {
        inc_dec = 1;
        find = '\xFF';
        replace = '\0';
    }
    else if (inc_dec < 0) {
        inc_dec = -1;
        find = '\0';
        replace = '\xFF';
    }
    else {
        errno = 2;
        return false;
    }
    
    for (i = addr_bytes - 1; i >= 0; i--) {
        if (addr[i] == find) {
            addr[i] = replace;
        }
        else {
            addr[i] += inc_dec;
            break;
        }
    }
    
    errno = 0;
    return true;
}

//returns true if the ranges are contiguous, false otherwise
//the ranges can be in any order
bool ranges_contiguous(AddressRange *range1, AddressRange *range2) {
    AddressRange *first;
    AddressRange *second;
    int addr_family;
    int comparison;
    uint8_t end1[IPV6_BYTES];
    
    if (range1->addr_family != range2->addr_family) {
        //different families
        return false;
    }
    
    addr_family = range1->addr_family;
    
    //sort the ranges by start address
    comparison = compare_addrs(range1->start, range2->start, addr_family);
    if (comparison < 0) {
        first = range1;
        second = range2;
    }
    else if (comparison > 0) {
        first = range2;
        second = range1;
    }
    else {
        //same start address
        return false;
    }
    
    //increment the end address of the first range in end1
    memcpy(end1, first->end, first->addr_bytes);
    if (!inc_addr(end1, addr_family, 1)) {
        //possibly invalid addr_family
        return false;
    }
    
    if (compare_addrs(end1, second->start, addr_family) == 0) {
        //the end address of the first range is immediately before the start address of the second
        return true;
    }
    
    return false;
}
