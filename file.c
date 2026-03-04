#include "file.h"

/* 
 * check if a file exists
 * returns true if it does, otherwise false
 */
int file_exists(const char * filename)
{
	return access(filename, F_OK) == 0 ? 1 : 0;
}

/*
 * check if path refers to a file
 * returns true if it does, otherwise false
 */
int file_is_file(const char * filename)
{
	struct stat attr;
	if (stat(filename, &attr) != 0)
	{
		return 0;
	}
	return S_ISREG(attr.st_mode) ? 1 : 0;
}

/*
 * opens a file for reading
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_read(const char * filename)
{
	if (!file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "rb");
}

/*
 * opens a file for writing
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_write(const char * filename)
{
	if (!file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "wb");
}

/*
 * creates a new file if one doesn't exist, or overwrites and truncates an existing one
 * returns a FILE pointer on success, NULL on failure
 */
FILE * file_open_truncate(const char * filename)
{
	if (file_exists(filename) && !file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "w+b");
}

/*
 * closes an open file
 * returns true on success, otherwise false
 */
int file_close(FILE * stream)
{
	if (!stream)
	{
		return EOF;
	}
	return fclose(stream) == 0 ? 1 : 0;
}

/*
 * reads bytes from an open file
 * returns the number of bytes successfully read
 */
size_t file_read(void * dst, size_t bytes, FILE * stream)
{
	if (!dst || !stream || bytes < 1)
	{
		return 0;
	}
	return fread(dst, 1, bytes, stream);
}

/*
 * writes bytes to an open file
 * returns the number of bytes successfully written
 */
size_t file_write(const void * src, size_t bytes, FILE * stream)
{
	if (!src || !stream || bytes < 1)
	{
		return 0;
	}
	return fwrite(src, 1, bytes, stream);
}

/*
 * sets position counter of an open file
 * returns true on success, otherwise false
 */
int file_seek(FILE * stream, long offset, int whence)
{
	if (!stream)
	{
		return 0;
	}
	return fseek(stream, offset, whence) == 0 ? 1 : 0;
}

/*
 * gets position counter of an open file
 * returns position on success, -1 on failure
 */
long file_tell(FILE * stream)
{
	if (!stream)
	{
		return -1;
	}
	return ftell(stream);
}

/*
 * gets size of a file
 * returns size on success, -1 on failure
 */
long file_size(const char * filename)
{
	FILE * f;
	size_t size;
	f = file_open_read(filename);
	if (!f)
	{
		return -1;
	}
	if (!file_seek(f, 0, SEEK_END))
	{
		return -1;
	}
	size = file_tell(f);
	file_close(f);
	return size;
}

/*
 * loads a file to a buffer
 * returns true on success, otherwise false
 */
int file_load_to_buffer(const char * filename, unsigned char ** out_buf, size_t * out_size)
{
	long size;
	size_t buf_size;
	FILE * f;
	int ret = 0;
	size = file_size(filename);
	if (size < 1)
	{
		return ret;
	}
	buf_size = size % 2 == 1 ? size + 1 : size;
	f = file_open_read(filename);
	if (f)
	{
		unsigned char * buf = (unsigned char *) malloc(buf_size);
		if (buf)
		{
			if (file_seek(f, 0, SEEK_SET))
			{
				if (file_read(buf, (size_t) size, f) == (size_t) size)
				{
					*out_buf = buf;
					*out_size = size;
					buf = NULL;
					ret = 1;
				}
			}
			free(buf);
		}
		file_close(f);
	}
	return ret;
}
