/* xbox_posix.c — the slice of POSIX/MSVCRT file API that pc/ uses, on the
 * nxdk Win32 subset (FindFirstFileA, CreateDirectoryA, GetFileAttributesExA). */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dirent.h"
#include "sys/stat.h"
#include "direct.h"
#include "xbox_io.h"

struct XboxDIR {
    HANDLE h;
    WIN32_FIND_DATAA fd;
    int first;
    struct dirent ent;
};

DIR* opendir(const char* path_in) {
    char pat[MAX_PATH], rp[MAX_PATH];
    /* the save tree lives in UDATA; any other scan is of the XBE's folder */
    const char* path = xbox_resolve(path_in, strncmp(path_in, "save", 4) == 0 ? XBOX_PATH_WRITE : XBOX_PATH_DISC, rp, sizeof rp);
    size_t n;
#ifdef XBOX_DBG_SAVE_FROM_D
    /* test runs: a save folder packed on the disc is scanned instead, as
     * xbox_resolve reads saves from it (harness OCX_STAGE_EXTRA) */
    if (strncmp(path_in, "save", 4) == 0) {
        DWORD a = GetFileAttributesA(xbox_resolve(path_in, XBOX_PATH_DISC, pat, sizeof pat));
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
            path = xbox_resolve(path_in, XBOX_PATH_DISC, rp, sizeof rp);
    }
#endif
    n = strlen(path);
    DIR* d;
    if (n + 5 > sizeof pat) return NULL;
    memcpy(pat, path, n);
    if (n && path[n - 1] != '\\' && path[n - 1] != '/') pat[n++] = '\\';
    /* nxdk wants the "*.*" form (its winapi_filefind sample); bare "*" fails with 2 */
    pat[n++] = '*';
    pat[n++] = '.';
    pat[n++] = '*';
    pat[n] = '\0';
    for (char* p = pat; *p; p++) if (*p == '/') *p = '\\';
    d = (DIR*)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->h = FindFirstFileA(pat, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) {
        xbox_logf("[XBOX] opendir(%s): FindFirstFile(%s) failed, err=%lu\n", path_in, pat, (unsigned long)GetLastError());
        free(d);
        return NULL;
    }
    d->first = 1;
    return d;
}

struct dirent* readdir(DIR* d) {
    if (!d) return NULL;
    if (!d->first && !FindNextFileA(d->h, &d->fd)) return NULL;
    d->first = 0;
    strncpy(d->ent.d_name, d->fd.cFileName, sizeof d->ent.d_name - 1);
    d->ent.d_name[sizeof d->ent.d_name - 1] = '\0';
    return &d->ent;
}

int closedir(DIR* d) {
    if (!d) return -1;
    FindClose(d->h);
    free(d);
    return 0;
}

int stat(const char* path, struct stat* st) {
    char p[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA a;
    xbox_resolve(path, XBOX_PATH_READ, p, sizeof p);
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a)) return -1;
    st->st_mode = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->st_size = a.nFileSizeLow;
    return 0;
}

int _mkdir(const char* path) {
    char p[MAX_PATH];
    xbox_resolve(path, XBOX_PATH_WRITE, p, sizeof p);
    return CreateDirectoryA(p, NULL) ? 0 : -1;
}

int mkdir(const char* path, mode_t mode) {
    (void)mode;
    return _mkdir(path);
}

int strcasecmp(const char* a, const char* b) {
    unsigned char ca, cb;
    do {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
    } while (ca && ca == cb);
    return ca - cb;
}

int strncasecmp(const char* a, const char* b, unsigned int n) {
    unsigned char ca = 0, cb = 0;
    while (n--) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (!ca || ca != cb) break;
    }
    return ca - cb;
}
