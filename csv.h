#ifndef CSV_H
#define CSV_H

#define CSV_SEPARATOR ','
#define CSV_QUOTE '"'
#define CSV_EOL '\n'
#define CSV_STRIP_EOL true

unsigned tokenize_csv(char *line, char **tokens, size_t max_columns);

unsigned detect_columns(char **header, size_t header_size, const char **required_columns, unsigned *column_positions, size_t max_columns, unsigned *highest_column);

#endif
