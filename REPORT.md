## Feature-2: ls-v1.1.0 — Complete Long Listing Format

### Summary
In this feature, I implemented the `-l` option in my `ls` utility to display files in the **long listing format**, similar to the real `ls -l` command on Linux.  
The program now uses several important system calls — `lstat()`, `getpwuid()`, `getgrgid()`, and `ctime()` — to collect and display complete file metadata, including permissions, number of links, owner, group, size, and modification time.

---

### Q1. What is the crucial difference between the `stat()` and `lstat()` system calls?  
Both `stat()` and `lstat()` are used to retrieve detailed information about a file, but they differ in how they handle **symbolic links**.

- `stat()` **follows** the symbolic link and returns information about the **target file** that the link points to.  
- `lstat()` **does not follow** the link; it provides information about the **link itself**, such as its path, permissions, and ownership.

In the context of the `ls` command, using `lstat()` is more appropriate because it allows `ls -l` to correctly display symbolic links as links (showing the link name and arrow) instead of the target file they point to.

---

### Q2. How does the `st_mode` field in `struct stat` store file type and permissions, and how can bitwise operators be used to extract them?  
The `st_mode` field inside `struct stat` is an integer that encodes **two types of information**:
1. **File type** (e.g., regular file, directory, symbolic link, etc.)
2. **Permission bits** (read, write, execute rights for owner, group, and others)

To extract this information, we use **bitwise AND (`&`)** with **predefined macros** provided in `<sys/stat.h>`.

#### Example Code:
```c
// Determine file type
if ((st.st_mode & S_IFMT) == S_IFDIR)
    printf("This is a directory.\n");
else if ((st.st_mode & S_IFMT) == S_IFREG)
    printf("This is a regular file.\n");

// Determine user permissions
(st.st_mode & S_IRUSR) ? printf("r") : printf("-");
(st.st_mode & S_IWUSR) ? printf("w") : printf("-");
(st.st_mode & S_IXUSR) ? printf("x") : printf("-");

## Feature-3: ls-v1.2.0 — Column Display (Down Then Across)

### Summary
In this feature, I upgraded my custom `ls` utility so that its **default behavior** (when used without any options) now prints files in **multiple columns**, formatted *“down then across.”*  
The program dynamically adjusts the number of columns and rows based on the **terminal width** and **length of the longest filename**, producing an output that closely resembles the default `ls` command in Linux.

This version also retains the `-l` option from Feature-2 for detailed listings.  
Key system calls introduced here include **`ioctl()`** (for terminal size detection) and dynamic memory allocation functions (`malloc`, `realloc`, and `free`).

---

### Implementation Details

1. **Reading Directory Entries**
   - The program first reads all filenames in a directory using `readdir()` and stores them in a dynamically allocated array of strings.  
   - Hidden files (starting with '.') are skipped.  
   - While reading, the program tracks the **longest filename length** (`max_len`) to help determine column widths later.

   ```c
   names[count] = strdup(entry->d_name);
   if (strlen(entry->d_name) > max_len)
       max_len = strlen(entry->d_name);

### Q1. Explain the general logic for printing items in a "down then across" columnar format.
To print items in the "down then across" layout we:
1. Read all filenames first into an array (so we know the total count and the longest filename).
2. Compute the longest filename length (max_len) and add spacing (e.g., 2).
3. Ask the terminal for its width (term_width). If unavailable, use fallback (80).
4. Compute the number of columns that can fit: `cols = term_width / (max_len + spacing)`. Ensure cols >= 1.
5. Compute rows = ceil(count / cols).
6. Print row by row. For row r and column c the index in the names array is `idx = c * rows + r`. If `idx < count` print `names[idx]` padded to column width.
A simple single loop (printing names sequentially) is insufficient because it prints left-to-right, top-to-bottom sequentially and cannot produce the vertical-first ordering required by "down then across".

### Q2. What is the purpose of ioctl in this context, and limitations of fixed-width fallback?
`ioctl()` with `TIOCGWINSZ` is used to query the terminal window size (columns and rows). Knowing the terminal width allows the program to dynamically calculate how many columns will fit and adapt the layout to the user's current terminal size. If only a fixed-width fallback (e.g., 80) is used, the program will not adapt when the user resizes the terminal; output may either wrap unexpectedly on small windows or waste space on large ones, reducing usability and not matching the behavior of the standard `ls`.

### Feature 4 — Horizontal Column Display (-x)

**Q1.** Compare the complexity of “down then across” vs. “across” printing logic.  
**A1.** The “down then across” format requires pre-calculating both rows and columns and indexing into the filename array using computed offsets, which is more complex.  
The “across” (horizontal) method only tracks current screen width and wraps when needed, requiring minimal pre-calculation.

**Q2.** How did you manage the different display modes (-l, -x, and default)?  
**A2.** I introduced a `mode` variable. The getopt parser sets `mode = 1` for `-l`, `mode = 2` for `-x`, and `mode = 0` for default.  
After collecting filenames, `main()` checks this variable and calls the appropriate display function, keeping logic clear and modular.

