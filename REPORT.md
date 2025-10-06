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
