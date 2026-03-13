#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/* 
 * check if a file exists
 * returns true if it does, otherwise false
 */
int file_exists(const char * filename);

/*
 * check if path refers to a file
 * returns true if it does, otherwise false
 */
int file_is_file(const char * filename);

/*
 * opens a file for reading
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_read(const char * filename);

/*
 * opens a file for writing
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_write(const char * filename);

/*
 * creates a new file if one doesn't exist, or overwrites and truncates an existing one
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_truncate(const char * filename);

/*
 * closes an open file
 * returns true on success, otherwise false
 */
int file_close(FILE * stream);

/*
 * reads (size * count) items from an open file
 * returns the number of items successfully read (up to count)
 */
size_t file_read(void * dst, size_t size, size_t count, FILE * stream);

/*
 * writes (size * count) items to an open file
 * returns the number of items successfully written (up to count)
 */
size_t file_write(const void * src, size_t size, size_t count, FILE * stream);

/*
 * reads bytes from an open file
 * returns the number of bytes successfully read
 */
size_t file_read_bytes(void * dst, size_t bytes, FILE * stream);

/*
 * writes bytes to an open file
 * returns the number of bytes successfully written
 */
size_t file_write_bytes(const void * src, size_t bytes, FILE * stream);

/*
 * sets position counter of an open file
 * returns true on success, otherwise false
 */
int file_seek(FILE * stream, long offset, int whence);

/*
 * gets position counter of an open file
 * returns position on success, -1 on failure
 */
long file_tell(FILE * stream);

/*
 * gets size of a file
 * returns size on success, -1 on failure
 */
long file_size(const char * filename);

/*
 * loads a file to a buffer
 * returns true on success, otherwise false
 */
int file_load_to_buffer(const char * filename, unsigned char ** out_buf, size_t * out_size);

#endif /* FILE_H */
