#ifndef PATH_H
#define PATH_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int exe_dir_init(char * argv0);
const char * get_exe_dir(void);
char * get_basename(char * path);
char * build_file_path(const char * path, const char * filename);
char * append_ext(const char * file, const char * ext);
char * strip_ext(const char * filename);

#endif /* PATH_H */
