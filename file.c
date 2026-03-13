#include "file.h"

int file_exists(const char * filename)
{
	return access(filename, F_OK) == 0 ? 1 : 0;
}

int file_is_file(const char * filename)
{
	struct stat attr;
	if (stat(filename, &attr) != 0)
	{
		return 0;
	}
	return S_ISREG(attr.st_mode) ? 1 : 0;
}

FILE * file_open_read(const char * filename)
{
	if (!file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "rb");
}

FILE * file_open_write(const char * filename)
{
	if (!file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "wb");
}

FILE * file_open_truncate(const char * filename)
{
	if (file_exists(filename) && !file_is_file(filename))
	{
		return NULL;
	}
	return fopen(filename, "w+b");
}

int file_close(FILE * stream)
{
	if (!stream)
	{
		return EOF;
	}
	return fclose(stream) == 0 ? 1 : 0;
}

size_t file_read(void * dst, size_t size, size_t count, FILE * stream)
{
	if (!dst || !stream || size < 1 || count < 1)
	{
		return 0;
	}
	return fread(dst, size, count, stream);
}

size_t file_write(const void * src, size_t size, size_t count, FILE * stream)
{
	if (!src || !stream || size < 1 || count < 1)
	{
		return 0;
	}
	return fwrite(src, size, count, stream);
}

size_t file_read_bytes(void * dst, size_t bytes, FILE * stream)
{
	if (!dst || !stream || bytes < 1)
	{
		return 0;
	}
	return fread(dst, 1, bytes, stream);
}

size_t file_write_bytes(const void * src, size_t bytes, FILE * stream)
{
	if (!src || !stream || bytes < 1)
	{
		return 0;
	}
	return fwrite(src, 1, bytes, stream);
}

int file_seek(FILE * stream, long offset, int whence)
{
	if (!stream)
	{
		return 0;
	}
	return fseek(stream, offset, whence) == 0 ? 1 : 0;
}

long file_tell(FILE * stream)
{
	if (!stream)
	{
		return -1;
	}
	return ftell(stream);
}

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
				if (file_read_bytes(buf, (size_t) size, f) == (size_t) size)
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
