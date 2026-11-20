/* ESP-IDF glue for libpd: extras registration and POSIX shims. */
#include "libpd_esp_compat.h"

void bob_tilde_setup(void);
void bonk_tilde_setup(void);
void choice_setup(void);
void fiddle_tilde_setup(void);
void loop_tilde_setup(void);
void lrshift_tilde_setup(void);
void pique_setup(void);
void sigmund_tilde_setup(void);

void libpd_esp_extras_setup(void)
{
    bob_tilde_setup();
    bonk_tilde_setup();
    choice_setup();
    fiddle_tilde_setup();
    loop_tilde_setup();
    lrshift_tilde_setup();
    pique_setup();
    sigmund_tilde_setup();
}

/* ------------------------------------------------------------------------
 * POSIX shims. Pd references these from code paths that never run in
 * libpd on ESP32 (starting the Tk GUI) or that degrade gracefully when
 * they fail ([file glob], [file stat]).
 * ---------------------------------------------------------------------- */
#include <errno.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* GUI startup (sys_do_startgui): libpd never starts a GUI. */
int pipe(int fds[2])
{
    (void)fds;
    errno = ENOSYS;
    return -1;
}

int dup2(int oldfd, int newfd)
{
    (void)oldfd;
    (void)newfd;
    errno = ENOSYS;
    return -1;
}

int execl(const char *path, const char *arg, ...)
{
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

/* No symlinks on LittleFS/FAT: lstat == stat. */
int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

/* Single user. */
uid_t geteuid(void)
{
    return 0;
}

/* [file glob]: report no matches. */
int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *g)
{
    (void)pattern;
    (void)flags;
    (void)errfunc;
    if (g)
    {
        g->gl_pathc = 0;
        g->gl_pathv = NULL;
    }
    return GLOB_NOMATCH;
}

void globfree(glob_t *g)
{
    (void)g;
}
