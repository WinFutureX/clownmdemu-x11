#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#include "path.h"

#include <libgen.h>
#include <unistd.h>

static const char path_sep = '/';
static const char path_sep_str[2] = "/";
static const char path_list_sep[8] = ":"; /* could be ":;" */

static char exe_dir[PATH_MAX]; /* NOTE: global variable! */

int exe_dir_init(char * argv0)
{
	char save_pwd[PATH_MAX];
	char save_argv0[PATH_MAX];
	char save_path[PATH_MAX];
	char new_path[PATH_MAX];
	char new_path_2[PATH_MAX];
	char * dir_tmp;
	
	getcwd(save_pwd, sizeof(save_pwd));
	strncpy(save_argv0, argv0, sizeof(save_argv0));
	save_argv0[sizeof(save_argv0) - 1] = 0;
	strncpy(save_path, getenv("PATH"), sizeof(save_path));
	save_path[sizeof(save_path) - 1] = 0;
	
	exe_dir[0] = 0;
	if (save_argv0[0] == path_sep)
	{
		/* absolute path */
		realpath(save_argv0, new_path);
		if (!access(new_path, F_OK))
		{
			dir_tmp = dirname(new_path);
			strncpy(exe_dir, dir_tmp, PATH_MAX);
			exe_dir[PATH_MAX - 1] = 0;
			return 1;
		}
		else
		{
			perror("access 1");
		}
	}
	else if (strchr(save_argv0, path_sep))
	{
		/* relative path */
		strncpy(new_path_2, save_pwd, sizeof(new_path_2));
		new_path_2[sizeof(new_path_2) - 1] = 0;
		strncat(new_path_2, path_sep_str, sizeof(new_path_2) - 1);
		new_path_2[sizeof(new_path_2) - 1] = 0;
		strncat(new_path_2, save_argv0, sizeof(new_path_2) - 1);
		new_path_2[sizeof(new_path_2) - 1] = 0;
		realpath(new_path_2, new_path);
		if (!access(new_path, F_OK))
		{
			dir_tmp = dirname(new_path);
			strncpy(exe_dir, dir_tmp, PATH_MAX);
			exe_dir[PATH_MAX - 1] = 0;
			return 1;
		}
		else
		{
			perror("access 2");
		}
	}
	else
	{
		/* search $PATH */
		char * save_ptr;
		char * path_item;
		for (path_item = strtok_r(save_path, path_list_sep, &save_ptr); path_item; path_item = strtok_r(NULL, path_list_sep, &save_ptr))
		{
			strncpy(new_path_2, path_item, sizeof(new_path_2));
			new_path_2[sizeof(new_path_2) - 1] = 0;
			strncat(new_path_2, path_sep_str, sizeof(new_path_2) - 1);
			new_path_2[sizeof(new_path_2) - 1] = 0;
			strncat(new_path_2, save_argv0, sizeof(new_path_2) - 1);
			new_path_2[sizeof(new_path_2) - 1] = 0;
			realpath(new_path_2, new_path);
			if (!access(new_path, F_OK))
			{
				dir_tmp = dirname(new_path);
				strncpy(exe_dir, dir_tmp, PATH_MAX);
				exe_dir[PATH_MAX - 1] = 0;
				return 1;
			}
		}
		/* end for */
		perror("access 3");
	}
	/* if we have reached here, we have exhausted all methods, so give up */
	return 0;
}

const char * get_exe_dir(void)
{
	return exe_dir;
}

char * get_basename(char * path)
{
	return strdup(basename(path));
}

char * build_file_path(const char * path, const char * filename)
{
	int ret_size;
	char * ret;
	if (!path || !filename)
	{
		return NULL;
	}
	if (path[0] == '\0' || filename[0] == '\0')
	{
		return NULL;
	}
	ret_size = PATH_MAX * sizeof(char);
	ret = (char *) malloc(ret_size);
	if (!ret)
	{
		return NULL;
	}
	strncpy(ret, path, ret_size - 1);
	ret[ret_size - 1] = 0;
	strncat(ret, path_sep_str, ret_size - 1);
	ret[ret_size - 1] = 0;
	strncat(ret, filename, ret_size - 1);
	ret[ret_size - 1] = 0;
	
	/* you must free file names after using them! */
	return ret;
}

char * append_ext(const char * file, const char * ext)
{
	char * ret;
	size_t len_ret, len_file, len_ext;
	if (!file || !ext)
	{
		return NULL;
	}
	if (file[0] == '\0' || ext[0] == '\0')
	{
		return NULL;
	}
	len_file = strlen(file);
	len_ext = strlen(ext);
	len_ret = len_file + len_ext + 2; /* includes space for '.' and '\0' */
	ret = (char *) malloc(len_ret);
	if (!ret)
	{
		return NULL;
	}
	memcpy(ret, file, len_file);
	ret[len_file] = '.';
	memcpy(ret + len_file + 1, ext, len_ext);
	ret[len_ret - 1] = '\0';
	return ret;
}

char * strip_ext(const char * filename)
{
	char * end;
	int diff;
	char * ret = NULL;
	char * copy = strdup(filename);
	if (!copy)
	{
		return ret;
	}
	end = copy + strlen(copy);
	while (end > copy && *end != '.' && *end != '\\' && *end != '/')
	{
		--end;
	}
	if ((end > copy && *end == '.') && (*(end - 1) != '\\' && *(end - 1) != '/'))
	{
		diff = (int) (end - copy);
		/*printf("filename %p end %p diff %d\n", copy, end, diff);*/
		ret = (char *) malloc(diff + 1);
		if (ret)
		{
			memcpy(ret, copy, diff);
			ret[diff] = '\0';
		}
		free(copy);
	}
	else
	{
		ret = copy;
	}
	return ret;
}
