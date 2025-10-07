/*
 * src/ls.c
 * ls v1.3.0
 * - supports: default multi-column display ("down then across")
 * - supports: -l long listing (uses lstat, getpwuid, getgrgid, ctime)
 * - supports: -x horizontal column display (row-major)
 *
 * Build: make
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>

/* Structure to store file information */
typedef struct {
    char *name;
    char *fullpath;
    struct stat st;
    int is_link;
    char *link_target;
} entry_t;

/* Build permission string into buf (buf must be at least 11 bytes) */
static void build_perm_string(mode_t m, char *buf) {
    buf[0] = S_ISDIR(m) ? 'd' :
             S_ISLNK(m) ? 'l' :
             S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' :
             S_ISFIFO(m) ? 'p' :
             S_ISSOCK(m) ? 's' : '-';

    buf[1] = (m & S_IRUSR) ? 'r' : '-';
    buf[2] = (m & S_IWUSR) ? 'w' : '-';
    if (m & S_ISUID) buf[3] = (m & S_IXUSR) ? 's' : 'S';
    else buf[3] = (m & S_IXUSR) ? 'x' : '-';

    buf[4] = (m & S_IRGRP) ? 'r' : '-';
    buf[5] = (m & S_IWGRP) ? 'w' : '-';
    if (m & S_ISGID) buf[6] = (m & S_IXGRP) ? 's' : 'S';
    else buf[6] = (m & S_IXGRP) ? 'x' : '-';

    buf[7] = (m & S_IROTH) ? 'r' : '-';
    buf[8] = (m & S_IWOTH) ? 'w' : '-';
    if (m & S_ISVTX) buf[9] = (m & S_IXOTH) ? 't' : 'T';
    else buf[9] = (m & S_IXOTH) ? 'x' : '-';

    buf[10] = '\0';
}

/* Comparator for qsort: alphabetical */
static int cmp_entries(const void *a, const void *b) {
    const entry_t *ea = a;
    const entry_t *eb = b;
    return strcmp(ea->name, eb->name);
}

/* Free entries array */
static void free_entries(entry_t *ents, size_t n) {
    if (!ents) return;
    for (size_t i = 0; i < n; ++i) {
        free(ents[i].name);
        free(ents[i].fullpath);
        if (ents[i].link_target) free(ents[i].link_target);
    }
    free(ents);
}

/* Collect directory entries into dynamic array of entry_t (skips hidden files) */
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
        if (de->d_name[0] == '.') continue; /* skip hidden files */
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
            memset(&arr[count].st, 0, sizeof(struct stat));
        } else {
            if (S_ISLNK(arr[count].st.st_mode)) {
                arr[count].is_link = 1;
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
    qsort(arr, count, sizeof(entry_t), cmp_entries);
    *out_entries = arr;
    *out_n = count;
}

/* Print long listing (ls -l) */
static void print_long_listing(entry_t *ents, size_t n) {
    if (n == 0) return;
    size_t link_w = 0, owner_w = 0, group_w = 0, size_w = 0;

    for (size_t i = 0; i < n; ++i) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%ld", (long)ents[i].st.st_nlink);
        if (strlen(tmp) > link_w) link_w = strlen(tmp);

        struct passwd *pw = getpwuid(ents[i].st.st_uid);
        const char *on = pw ? pw->pw_name : "unknown";
        if (strlen(on) > owner_w) owner_w = strlen(on);

        struct group *gr = getgrgid(ents[i].st.st_gid);
        const char *gn = gr ? gr->gr_name : "unknown";
        if (strlen(gn) > group_w) group_w = strlen(gn);

        snprintf(tmp, sizeof(tmp), "%lld", (long long)ents[i].st.st_size);
        if (strlen(tmp) > size_w) size_w = strlen(tmp);
    }

    time_t now = time(NULL);
    const time_t six_months = (time_t)(60LL*60*24*30*6); /* approx */

    for (size_t i = 0; i < n; ++i) {
        char perm[11];
        build_perm_string(ents[i].st.st_mode, perm);
        printf("%s ", perm);
        printf("%*ld ", (int)link_w, (long)ents[i].st.st_nlink);

        struct passwd *pw = getpwuid(ents[i].st.st_uid);
        struct group *gr = getgrgid(ents[i].st.st_gid);
        const char *on = pw ? pw->pw_name : "unknown";
        const char *gn = gr ? gr->gr_name : "unknown";
        printf("%-*s %-*s ", (int)owner_w, on, (int)group_w, gn);

        printf("%*lld ", (int)size_w, (long long)ents[i].st.st_size);

        char timebuf[64];
        struct tm *tm_info = localtime(&ents[i].st.st_mtime);
        if (!tm_info) strcpy(timebuf, "??? ?? ??:??");
        else {
            if ((now - ents[i].st.st_mtime) > six_months || (ents[i].st.st_mtime - now) > six_months)
                strftime(timebuf, sizeof(timebuf), "%b %e  %Y", tm_info);
            else
                strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", tm_info);
        }
        printf("%s ", timebuf);

        if (ents[i].is_link && ents[i].link_target)
            printf("%s -> %s\n", ents[i].name, ents[i].link_target);
        else
            printf("%s\n", ents[i].name);
    }
}

/* Print multi-column "down then across" (default display) */
static void print_columns(entry_t *ents, size_t n) {
    if (n == 0) return;

    size_t max_len = 0;
    char **names = malloc(n * sizeof(char*));
    if (!names) {
        perror("malloc");
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        names[i] = ents[i].name;
        size_t l = strlen(names[i]);
        if (l > max_len) max_len = l;
    }

    struct winsize ws;
    int term_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        term_width = ws.ws_col;

    int spacing = 2;
    size_t col_width = max_len + spacing;
    int cols = (int)(term_width / col_width);
    if (cols < 1) cols = 1;
    int rows = (int)((n + cols - 1) / cols);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = c * rows + r;
            if (idx < (int)n)
                printf("%-*s", (int)col_width, names[idx]);
        }
        printf("\n");
    }

    free(names);
}

/* Print horizontal "across" columns (ls -x) */
static void print_horizontal(entry_t *ents, size_t n) {
    if (n == 0) return;

    size_t max_len = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t len = strlen(ents[i].name);
        if (len > max_len) max_len = len;
    }

    struct winsize ws;
    int term_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        term_width = ws.ws_col;

    int spacing = 2;
    int col_width = (int)max_len + spacing;
    int current_width = 0;

    for (size_t i = 0; i < n; ++i) {
        if (current_width + col_width > term_width) {
            printf("\n");
            current_width = 0;
        }
        printf("%-*s", col_width, ents[i].name);
        current_width += col_width;
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int mode = 0;   /* 0=default, 1=-l, 2=-x */

    while ((opt = getopt(argc, argv, "lx")) != -1) {
        switch (opt) {
            case 'l': mode = 1; break;
            case 'x': mode = 2; break;
            default:
                fprintf(stderr, "Usage: %s [-l | -x] [directory]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    const char *path = ".";
    if (optind < argc) path = argv[optind];

    entry_t *ents = NULL;
    size_t n = 0;
    collect_directory(path, &ents, &n);

    if (mode == 1)
        print_long_listing(ents, n);
    else if (mode == 2)
        print_horizontal(ents, n);
    else
        print_columns(ents, n);

    free_entries(ents, n);
    return 0;
}

