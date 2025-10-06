/* src/ls.c
 *
 * ls - simplified implementation with -l (long listing)
 * v1.1.0
 *
 * Features:
 *  - parse -l with getopt()
 *  - use lstat() so symlinks are reported as links (not followed)
 *  - resolve owner/group with getpwuid/getgrgid
 *  - permission string including special bits (setuid, setgid, sticky)
 *  - time formatting similar to ls (shows time if mtime within 6 months, else year)
 *  - aligns columns by computing widths before printing
 *  - prints symlink target like: name -> target
 *
 * Build with your Makefile (make).
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <stdint.h>

typedef struct {
    char *name;
    char *fullpath;
    struct stat st;
    int is_link;
    char *link_target; // if symlink
} entry_t;

static char perms_buf[11];

static void build_perm_string(mode_t m, char *buf) {
    buf[0] = S_ISDIR(m) ? 'd' :
             S_ISLNK(m) ? 'l' :
             S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' :
             S_ISFIFO(m) ? 'p' :
             S_ISSOCK(m) ? 's' : '-';

    buf[1] = (m & S_IRUSR) ? 'r' : '-';
    buf[2] = (m & S_IWUSR) ? 'w' : '-';
    // user exec with setuid
    if (m & S_ISUID) {
        buf[3] = (m & S_IXUSR) ? 's' : 'S';
    } else {
        buf[3] = (m & S_IXUSR) ? 'x' : '-';
    }

    buf[4] = (m & S_IRGRP) ? 'r' : '-';
    buf[5] = (m & S_IWGRP) ? 'w' : '-';
    // group exec with setgid
    if (m & S_ISGID) {
        buf[6] = (m & S_IXGRP) ? 's' : 'S';
    } else {
        buf[6] = (m & S_IXGRP) ? 'x' : '-';
    }

    buf[7] = (m & S_IROTH) ? 'r' : '-';
    buf[8] = (m & S_IWOTH) ? 'w' : '-';
    // other exec with sticky bit
    if (m & S_ISVTX) {
        buf[9] = (m & S_IXOTH) ? 't' : 'T';
    } else {
        buf[9] = (m & S_IXOTH) ? 'x' : '-';
    }

    buf[10] = '\0';
}

static int cmp_entries(const void *a, const void *b) {
    const entry_t *ea = a;
    const entry_t *eb = b;
    return strcmp(ea->name, eb->name);
}

static void free_entries(entry_t *ents, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        free(ents[i].name);
        free(ents[i].fullpath);
        if (ents[i].link_target) free(ents[i].link_target);
    }
    free(ents);
}

static void collect_directory(const char *path, entry_t **out_entries, size_t *out_n) {
    DIR *d = opendir(path);
    if (!d) {
        perror(path);
        *out_entries = NULL;
        *out_n = 0;
        return;
    }

    struct dirent *de;
    size_t capacity = 64;
    size_t count = 0;
    entry_t *arr = calloc(capacity, sizeof(entry_t));
    if (!arr) {
        perror("calloc");
        closedir(d);
        *out_entries = NULL;
        *out_n = 0;
        return;
    }

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue; // skip hidden by default (Feature-2 doesn't require -a)
        if (count >= capacity) {
            capacity *= 2;
            entry_t *tmp = realloc(arr, capacity * sizeof(entry_t));
            if (!tmp) {
                perror("realloc");
                free_entries(arr, count);
                closedir(d);
                *out_entries = NULL;
                *out_n = 0;
                return;
            }
            arr = tmp;
        }

        arr[count].name = strdup(de->d_name);
        size_t pathlen = strlen(path);
        size_t namelen = strlen(de->d_name);
        arr[count].fullpath = malloc(pathlen + 1 + namelen + 1);
        if (!arr[count].fullpath) {
            perror("malloc");
            free_entries(arr, count);
            closedir(d);
            *out_entries = NULL;
            *out_n = 0;
            return;
        }
        snprintf(arr[count].fullpath, pathlen + 1 + namelen + 1, "%s/%s", path, de->d_name);

        arr[count].is_link = 0;
        arr[count].link_target = NULL;
        if (lstat(arr[count].fullpath, &arr[count].st) == -1) {
            // on error, fill zeros (but keep name)
            arr[count].st.st_mode = 0;
            arr[count].st.st_nlink = 0;
            arr[count].st.st_uid = 0;
            arr[count].st.st_gid = 0;
            arr[count].st.st_size = 0;
            arr[count].st.st_mtime = 0;
        } else {
            if (S_ISLNK(arr[count].st.st_mode)) {
                arr[count].is_link = 1;
                // read link target
                char buf[PATH_MAX+1];
                ssize_t r = readlink(arr[count].fullpath, buf, PATH_MAX);
                if (r >= 0) {
                    buf[r] = '\0';
                    arr[count].link_target = strdup(buf);
                }
            }
        }

        count++;
    }

    closedir(d);

    // sort entries alphabetically
    qsort(arr, count, sizeof(entry_t), cmp_entries);

    *out_entries = arr;
    *out_n = count;
}

static void print_long_listing(entry_t *ents, size_t n) {
    // compute widths
    size_t link_w = 0;
    size_t owner_w = 0;
    size_t group_w = 0;
    size_t size_w = 0;

    for (size_t i = 0; i < n; ++i) {
        char perm[11];
        build_perm_string(ents[i].st.st_mode, perm);

        // links width
        size_t lwidth = 0;
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%ld", (long)ents[i].st.st_nlink);
            lwidth = strlen(tmp);
        }
        if (lwidth > link_w) link_w = lwidth;

        // owner width
        {
            struct passwd *pw = getpwuid(ents[i].st.st_uid);
            const char *on = pw ? pw->pw_name : "unknown";
            size_t w = strlen(on);
            if (w > owner_w) owner_w = w;
        }
        // group width
        {
            struct group *gr = getgrgid(ents[i].st.st_gid);
            const char *gn = gr ? gr->gr_name : "unknown";
            size_t w = strlen(gn);
            if (w > group_w) group_w = w;
        }
        // size width
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%lld", (long long)ents[i].st.st_size);
            size_t w = strlen(tmp);
            if (w > size_w) size_w = w;
        }
    }

    // print lines
    time_t now = time(NULL);
    const time_t six_months = (time_t) (60LL*60*24*30*6); // approximate 6 months

    for (size_t i = 0; i < n; ++i) {
        build_perm_string(ents[i].st.st_mode, perms_buf);

        // links
        printf("%s ", perms_buf);
        printf("%*ld ", (int)link_w, (long)ents[i].st.st_nlink);

        // owner and group
        struct passwd *pw = getpwuid(ents[i].st.st_uid);
        struct group *gr = getgrgid(ents[i].st.st_gid);
        const char *on = pw ? pw->pw_name : "unknown";
        const char *gn = gr ? gr->gr_name : "unknown";
        printf("%-*s %-*s ", (int)owner_w, on, (int)group_w, gn);

        // size
        printf("%*lld ", (int)size_w, (long long)ents[i].st.st_size);

        // time formatting similar to ls
        char timebuf[64];
        struct tm *tm_info = localtime(&ents[i].st.st_mtime);
        if (!tm_info) {
            strcpy(timebuf, "??? ?? ??:??");
        } else {
            if ( (now - ents[i].st.st_mtime) > six_months || (ents[i].st.st_mtime - now) > six_months ) {
                // older than 6 months: show "Mon DD  YYYY"
                strftime(timebuf, sizeof(timebuf), "%b %e  %Y", tm_info);
            } else {
                // recent: show "Mon DD HH:MM"
                strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", tm_info);
            }
        }
        printf("%s ", timebuf);

        // name
        if (ents[i].is_link && ents[i].link_target) {
            printf("%s -> %s\n", ents[i].name, ents[i].link_target);
        } else {
            printf("%s\n", ents[i].name);
        }
    }
}

static void print_simple(entry_t *ents, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        printf("%s\n", ents[i].name);
    }
}

int main(int argc, char *argv[]) {
    int opt;
    int long_listing = 0;
    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
            case 'l':
                long_listing = 1;
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-l] [directory]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    const char *path = ".";
    if (optind < argc) {
        path = argv[optind];
    }

    entry_t *ents = NULL;
    size_t n = 0;
    collect_directory(path, &ents, &n);

    if (long_listing) {
        print_long_listing(ents, n);
    } else {
        print_simple(ents, n);
    }

    free_entries(ents, n);
    return 0;
}

