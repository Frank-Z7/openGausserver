/* -------------------------------------------------------------------------
 *
 * src/port/pgcheckdir.cpp
 *
 * A simple subroutine to check whether a directory exists and is empty or not.
 * Useful in both initdb and the backend.
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * -------------------------------------------------------------------------
 */

#include "c.h"

#include <dirent.h>
#include "storage/file/fio_device.h"

/*
 * Test to see if a directory exists and is empty or not.
 *
 * Returns:
 *		0 if nonexistent
 *		1 if exists and empty
 *		2 if exists and not empty
 *		-1 if trouble accessing directory (errno reflects the error)
 */
int pg_check_dir(const char* dir)
{
    int result = 1;
    DIR* chkdir = NULL;
    struct dirent* file = NULL;

    errno = 0;
    chkdir = opendir(dir);

    if (chkdir == NULL) {
        return (is_file_delete(errno) ? 0 : -1);
    }

    while ((file = gs_readdir(chkdir)) != NULL) {
        if (strcmp(".", file->d_name) == 0 || strcmp("..", file->d_name) == 0) {
            /* skip this and parent directory */
            continue;
        } else {
            result = 2; /* not empty */
            break;
        }
    }

#ifdef WIN32

    /*
     * This fix is in mingw cvs (runtime/mingwex/dirent.c rev 1.4), but not in
     * released version
     */
    if (GetLastError() == ERROR_NO_MORE_FILES)
        errno = 0;
#endif

    closedir(chkdir);

    if (errno != 0)
        result = -1; /* some kind of I/O error? */

    return result;
}
