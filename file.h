#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

int file_exists(const char * filename);
int file_is_file(const char * filename);
FILE * file_open_read(const char * filename);
FILE * file_open_write(const char * filename);
FILE * file_open_truncate(const char * filename);
int file_close(FILE * stream);
size_t file_read(void * dst, size_t bytes, FILE * stream);
size_t file_write(const void * src, size_t bytes, FILE * stream);
int file_seek(FILE * stream, long offset, int whence);
long file_tell(FILE * stream);
long file_size(const char * filename);
int file_load_to_buffer(const char * filename, unsigned char ** out_buf, size_t * out_size);

#endif /* FILE_H */
