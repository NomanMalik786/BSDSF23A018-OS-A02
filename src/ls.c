#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>   // for PATH_MAX
#include <getopt.h>
#include <ctype.h>

/* ---------- ANSI COLOR CODES ---------- */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_REVERSE "\033[7m"

/* ---------- STRUCT DEFINITION ---------- */
typedef struct {
    char *name;
    struct stat st;
    int is_link;
    char link_target[PATH_MAX];
} entry_t;

/* ---------- PROTOTYPES ---------- */
static void list_dir(const char *path, int mode, int recursive);
static void join_path(const char *parent, const char *child, char *out, size_t outlen);
static void print_long_listing(entry_t *ents, size_t count);
static void print_columns(entry_t *ents, size_t count);
static void print_horizontal(entry_t *ents, size_t count);
static int cmp_entries(const void *a, const void *b);
static void build_perm_string(mode_t m, char *out);
static void print_with_color(const char *name, const struct stat *st);
static entry_t *read_dir_entries(const char *path, size_t *out_count);

/* ---------- Helper: read directory ---------- */
static entry_t *read_dir_entries(const char *path, size_t *out_count) {
    DIR *dirp = opendir(path);
    if (!dirp) {
        perror(path);
        return NULL;
    }

    size_t cap = 64, count = 0;
    entry_t *arr = malloc(cap * sizeof(entry_t));
    if (!arr) {
        perror("malloc");
        closedir(dirp);
        return NULL;
    }

    struct dirent *dp;
    while ((dp = readdir(dirp)) != NULL) {
        if (dp->d_name[0] == '.')
            continue; // skip hidden files

        if (count == cap) {
            cap *= 2;
            entry_t *tmp = realloc(arr, cap * sizeof(entry_t));
            if (!tmp) {
                perror("realloc");
                for (size_t k = 0; k < count; ++k) free(arr[k].name);
                free(arr);
                closedir(dirp);
                return NULL;
            }
            arr = tmp;
        }

        arr[count].name = strdup(dp->d_name);
        if (!arr[count].name) {
            perror("strdup");
            for (size_t k = 0; k < count; ++k) free(arr[k].name);
            free(arr);
            closedir(dirp);
            return NULL;
        }
        arr[count].is_link = 0;
        arr[count].link_target[0] = '\0';

        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, dp->d_name);

        if (lstat(fullpath, &arr[count].st) == -1) {
            /* on lstat failure, print error and continue (skip this entry) */
            perror(fullpath);
            free(arr[count].name);
            continue;
        }

        if (S_ISLNK(arr[count].st.st_mode)) {
            arr[count].is_link = 1;
            ssize_t len = readlink(fullpath, arr[count].link_target,
                                   sizeof(arr[count].link_target) - 1);
            if (len >= 0)
                arr[count].link_target[len] = '\0';
        }
        count++;
    }

    closedir(dirp);
    *out_count = count;
    return arr;
}

/* ---------- Comparator for qsort ---------- */
static int cmp_entries(const void *a, const void *b) {
    const entry_t *ea = a;
    const entry_t *eb = b;
    return strcmp(ea->name, eb->name);
}

/* ---------- Helper: build permissions ---------- */
static void build_perm_string(mode_t m, char *out) {
    /* out must have space for at least 11 chars */
    strcpy(out, "----------");
    if (S_ISDIR(m)) out[0] = 'd';
    else if (S_ISLNK(m)) out[0] = 'l';
    else if (S_ISCHR(m)) out[0] = 'c';
    else if (S_ISBLK(m)) out[0] = 'b';
    else if (S_ISFIFO(m)) out[0] = 'p';
    else if (S_ISSOCK(m)) out[0] = 's';

    if (m & S_IRUSR) out[1] = 'r';
    if (m & S_IWUSR) out[2] = 'w';
    if (m & S_IXUSR) out[3] = 'x';
    if (m & S_IRGRP) out[4] = 'r';
    if (m & S_IWGRP) out[5] = 'w';
    if (m & S_IXGRP) out[6] = 'x';
    if (m & S_IROTH) out[7] = 'r';
    if (m & S_IWOTH) out[8] = 'w';
    if (m & S_IXOTH) out[9] = 'x';
}

/* ---------- Color helpers ---------- */
static int has_archive_ext(const char *name) {
    if (!name) return 0;
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return (strcmp(dot, ".tar") == 0 ||
            strcmp(dot, ".gz")  == 0 ||
            strcmp(dot, ".zip") == 0);
}

static void print_with_color(const char *name, const struct stat *st) {
    const char *color = COLOR_RESET;

    if (S_ISDIR(st->st_mode)) {
        color = COLOR_BLUE;
    } else if (S_ISLNK(st->st_mode)) {
        color = COLOR_MAGENTA;
    } else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode) ||
               S_ISFIFO(st->st_mode) || S_ISSOCK(st->st_mode)) {
        color = COLOR_REVERSE;
    } else if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
        color = COLOR_GREEN;
    } else if (has_archive_ext(name)) {
        color = COLOR_RED;
    } else {
        color = COLOR_RESET;
    }

    printf("%s%s%s", color, name, COLOR_RESET);
}

/* ---------- Long listing ---------- */
static void print_long_listing(entry_t *ents, size_t count) {
    for (size_t i = 0; i < count; i++) {
        char perm[11];
        build_perm_string(ents[i].st.st_mode, perm);

        struct passwd *pw = getpwuid(ents[i].st.st_uid);
        struct group  *gr = getgrgid(ents[i].st.st_gid);

        char timebuf[64];
        struct tm *tm_info = localtime(&ents[i].st.st_mtime);
        if (tm_info)
            strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", tm_info);
        else
            strncpy(timebuf, "??? ?? ??:??", sizeof(timebuf));

        printf("%s %3ld %-8s %-8s %8lld %s ",
               perm,
               (long)ents[i].st.st_nlink,
               pw ? pw->pw_name : "unknown",
               gr ? gr->gr_name : "unknown",
               (long long)ents[i].st.st_size,
               timebuf);

        if (ents[i].is_link && ents[i].link_target[0]) {
            print_with_color(ents[i].name, &ents[i].st);
            printf(" -> %s\n", ents[i].link_target);
        } else {
            print_with_color(ents[i].name, &ents[i].st);
            printf("\n");
        }
    }
}

/* ---------- Down-then-across (default) ---------- */
static void print_columns(entry_t *ents, size_t count) {
    if (count == 0) return;

    size_t max_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(ents[i].name);
        if (len > max_len) max_len = len;
    }

    struct winsize ws;
    size_t term_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        term_width = ws.ws_col;

    size_t col_width = max_len + 2;
    size_t cols = term_width / (col_width == 0 ? 1 : col_width);
    if (cols < 1) cols = 1;
    size_t rows = (count + cols - 1) / cols;

    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            size_t idx = c * rows + r;
            if (idx >= count) continue;

            const char *n = ents[idx].name;
            print_with_color(n, &ents[idx].st);
            int pad = (int)col_width - (int)strlen(n);
            if (pad < 0) pad = 0;
            for (int p = 0; p < pad; ++p) putchar(' ');
        }
        putchar('\n');
    }
}

/* ---------- Horizontal (across) ---------- */
static void print_horizontal(entry_t *ents, size_t count) {
    if (count == 0) return;

    size_t max_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(ents[i].name);
        if (len > max_len) max_len = len;
    }

    struct winsize ws;
    size_t term_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        term_width = ws.ws_col;

    size_t col_width = max_len + 2;
    size_t current = 0;

    for (size_t i = 0; i < count; i++) {
        const char *n = ents[i].name;
        size_t len = strlen(n);
        if (current + len + 1 > term_width) {
            putchar('\n');
            current = 0;
        }

        print_with_color(n, &ents[i].st);
        int pad = (int)col_width - (int)len;
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) putchar(' ');
        current += len + pad;
    }
    putchar('\n');
}

/* ---------- Path join helper ---------- */
static void join_path(const char *parent, const char *child, char *out, size_t outlen) {
    if (!parent || parent[0] == '\0') {
        snprintf(out, outlen, "%s", child);
        return;
    }
    size_t plen = strlen(parent);
    if (parent[plen - 1] == '/') {
        snprintf(out, outlen, "%s%s", parent, child);
    } else {
        snprintf(out, outlen, "%s/%s", parent, child);
    }
}

/* ---------- Main directory handling (recursive capable) ---------- */
static void list_dir(const char *path, int mode, int recursive) {
    size_t count = 0;
    entry_t *arr = read_dir_entries(path, &count);
    if (!arr) return;

    /* sort alphabetically */
    qsort(arr, count, sizeof(entry_t), cmp_entries);

    /* Print directory header when recursive (ls -R shows "path:") */
    if (recursive) {
        printf("%s:\n", path);
    }

    /* Use existing display logic */
    if (mode == 1)
        print_long_listing(arr, count);
    else if (mode == 2)
        print_horizontal(arr, count);
    else
        print_columns(arr, count);

    /* If recursive, after listing current directory, recurse into subdirectories */
    if (recursive) {
        for (size_t i = 0; i < count; ++i) {
            if (S_ISDIR(arr[i].st.st_mode)) {
                /* skip . and .. if they ever show up (we skip hidden files but be safe) */
                if (strcmp(arr[i].name, ".") == 0 || strcmp(arr[i].name, "..") == 0)
                    continue;
                /* build full path */
                char child_path[PATH_MAX];
                join_path(path, arr[i].name, child_path, sizeof(child_path));
                /* blank line before each sub-directory listing to match `ls -R` style */
                printf("\n");
                list_dir(child_path, mode, recursive);
            }
        }
    }

    /* free memory */
    for (size_t i = 0; i < count; i++)
        free(arr[i].name);
    free(arr);
}

/* ---------- MAIN ---------- */
int main(int argc, char *argv[]) {
    int opt;
    int mode = 0;       /* 0=default, 1=-l, 2=-x */
    int recursive = 0;  /* -R */

    while ((opt = getopt(argc, argv, "lxR")) != -1) {
        switch (opt) {
        case 'l': mode = 1; break;
        case 'x': mode = 2; break;
        case 'R': recursive = 1; break;
        default:
            fprintf(stderr, "Usage: %s [-l] [-x] [-R] [paths...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* If user provided paths, list each; otherwise list current directory */
    if (optind < argc) {
        for (int i = optind; i < argc; ++i) {
            if (i > optind) printf("\n");
            list_dir(argv[i], mode, recursive);
        }
    } else {
        list_dir(".", mode, recursive);
    }

    return 0;
}
