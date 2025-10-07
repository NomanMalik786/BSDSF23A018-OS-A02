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
static void list_dir(const char *path, int mode);
static void print_long_listing(entry_t *ents, size_t count);
static void print_columns(entry_t *ents, size_t count);
static void print_horizontal(entry_t *ents, size_t count);
static int cmp_entries(const void *a, const void *b);
static void build_perm_string(mode_t m, char *out);
static void print_with_color(const char *name, const struct stat *st);

/* ---------- Helper: read directory ---------- */
static entry_t *read_dir_entries(const char *path, size_t *out_count) {
    DIR *dirp = opendir(path);
    if (!dirp) {
        perror("opendir");
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
                free(arr);
                closedir(dirp);
                return NULL;
            }
            arr = tmp;
        }

        arr[count].name = strdup(dp->d_name);
        arr[count].is_link = 0;
        arr[count].link_target[0] = '\0';

        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, dp->d_name);

        if (lstat(fullpath, &arr[count].st) == -1) {
            perror("lstat");
            continue;
        }

        if (S_ISLNK(arr[count].st.st_mode)) {
            arr[count].is_link = 1;
            ssize_t len = readlink(fullpath, arr[count].link_target,
                                   sizeof(arr[count].link_target) - 1);
            if (len != -1)
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
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M",
                 localtime(&ents[i].st.st_mtime));

        printf("%s %3ld %-8s %-8s %8ld %s ",
               perm,
               (long)ents[i].st.st_nlink,
               pw ? pw->pw_name : "unknown",
               gr ? gr->gr_name : "unknown",
               (long)ents[i].st.st_size,
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        term_width = ws.ws_col;

    size_t col_width = max_len + 2;
    size_t cols = term_width / col_width;
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
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

/* ---------- Main directory handling ---------- */
static void list_dir(const char *path, int mode) {
    size_t count = 0;
    entry_t *arr = read_dir_entries(path, &count);
    if (!arr) return;

    qsort(arr, count, sizeof(entry_t), cmp_entries);

    if (mode == 1)
        print_long_listing(arr, count);
    else if (mode == 2)
        print_horizontal(arr, count);
    else
        print_columns(arr, count);

    for (size_t i = 0; i < count; i++)
        free(arr[i].name);
    free(arr);
}

/* ---------- MAIN ---------- */
int main(int argc, char *argv[]) {
    int opt;
    int mode = 0;  // 0=default, 1=-l, 2=-x

    while ((opt = getopt(argc, argv, "lx")) != -1) {
        switch (opt) {
        case 'l': mode = 1; break;
        case 'x': mode = 2; break;
        default:
            fprintf(stderr, "Usage: %s [-l | -x] [path]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    const char *path = (optind < argc) ? argv[optind] : ".";
    list_dir(path, mode);
    return 0;
}
