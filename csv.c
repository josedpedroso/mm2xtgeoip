#ifndef __bool_true_false_are_defined
#include <stdbool.h>
#endif

#ifndef _STRING_H
#include <string.h>
#endif

#include "csv.h"

unsigned tokenize_csv(char *line, char **tokens, size_t max_columns) {
    if (!max_columns) {
        //nothing to do
        return 0;
    }
    
    char c = line[0];
    
    //empty line means one empty token
    if (!c) {
        tokens[0] = line;
        return 1;
    }
    
    char next = line[1];
    char *begin = line;
    unsigned tokenized_columns = 0;
    unsigned i = 0;
    unsigned j;
    unsigned end;
    bool in_quotes = false;
    
    for (; c != '\0'; c = line[++i], next = line[i + 1]) {
        //double quote inside quotes, make it a single quote
        if (in_quotes && c == CSV_QUOTE && next == CSV_QUOTE) {
            for (j = i; line[j] != '\0'; line[j] = line[++j]);
            continue;
        }
        
        //single quote at the beginning of the column
        //remove it and set in_quotes
        if (!in_quotes && &line[i] == begin && c == CSV_QUOTE) {
            begin++;
            in_quotes = true;
            continue;
        }
        
        //single quote inside quotes
        if (in_quotes && c == CSV_QUOTE) {
            in_quotes = false;
            
            //found at the end of the column, remove it
            if (next == CSV_SEPARATOR || next == '\0') {
                line[i] = '\0';
            }
            //elsewhere means malformed csv
            
            continue;
        }
        
        //separator
        if (!in_quotes && c == CSV_SEPARATOR) {
            tokens[tokenized_columns] = begin;
            line[i] = '\0';
            begin = &line[i + 1];
            tokenized_columns++;
            
            if (tokenized_columns >= (max_columns - 1)) {
                break;
            }
            else {
                continue;
            }
        }
    }
    
    //handle last remaining column
    tokens[tokenized_columns] = begin;
    tokenized_columns++;
    
    //strip off trailing EOL from last column
    if (CSV_STRIP_EOL) {
        end = strlen(begin);
        if (begin[end - 1] == CSV_EOL) {
            begin[end - 1] = '\0';
        }
    }
    
    return tokenized_columns;
}

unsigned detect_columns(char **header, size_t header_size, const char **required_columns, unsigned *column_positions, size_t max_columns, unsigned *highest_column) {
    unsigned i;
    unsigned j;
    unsigned first = 0;
    unsigned num_found = 0;
    
    if (!header_size || !max_columns) {
        //nothing to do
        return 0;
    }
    
    *highest_column = 0;
    
    for (i = 0; i < header_size; i++) {
        for (j = first; j < max_columns; j++) {
            if (strcmp(header[i], required_columns[j]) != 0) {
                continue;
            }
            
            //found one of the required columns, save its position
            column_positions[j] = i;
            
            //update the highest column
            //the caller should use this value to confirm that lines tokenized
            //after the header contain the required number of columns
            if (i > *highest_column) {
                *highest_column = i;
            }
            
            //don't look for this column anymore if it's the first
            if (j == first) {
                first++;
            }
            
            num_found++;
        }
    }
    
    return num_found;
}

/*int main(void) {
    char *tokens[16];
    char line[256];
    unsigned i;
    gets(line);
    unsigned num_tokens = tokenize_csv(line, tokens, 16);
    printf("%d tokens:\n", num_tokens);
    for (i=0; i<num_tokens; i++) {
        printf("\t***%s***\n", tokens[i]);
    }
    puts("end.");
    return 0;
}*/
