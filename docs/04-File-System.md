# File System

## Indirect Block

### Structures

In order to increase the number of blocks which inode can contain, some
defined constants should be changed or added. `NDBDIRECT` refers to the number
of double-linked indirect blocks and `NTRDIRECT` refers to the number of
triple-linked indirect blocks. Each constants are defined with the `n` power
of `NINDIRECT`.

```c
// fs.h
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDBDIRECT (NINDIRECT * NINDIRECT)
#define NTRDIRECT (NINDIRECT * NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDBDIRECT + NTRDIRECT)
```

`addrs` of `inode` and `dinode` also should be modified to reflect the changes.
Its size is changed to `NDIRECT + 3`, the number of direct blocks with
indirect, double indirect and triple indirect block for each.

```c
// file.h
struct inode {
  // ...
  uint addrs[NDIRECT+3];
  // ...
}

// fs.h
struct dinode {
  // ...
  uint addrs[NDIRECT+3];
  // ...
}
```

### Mechanisms

Two changes are required to apply the double and triple indirect blocks;
`bmap()`  and `itrunc()`.

#### bmap

This function returns the disk block address of the nth block in inode.
It dynamically allocates the requested block if not existing.

The way to get a block in the range of double indirect blocks is to
combine the ways of finding the direct and indirect blocks. First of all, the
indexes of each block should be figured out. This is quite simple because nth
is defined like `a * 1024^1 + b * 1024^0`. The following codes are to find the
indexes of double indirect blocks.

```c
// fs.c

// Get index of 1st and 2nd indirect block
// bn is subtracted by NDIRECT
bn1 = bn / NINDIRECT;      // 1st
bn2 = bn - bn1*NINDIRECT;  // 2nd
```

The steps to find the address of double indirect block is the followings. Take
(or create) the first block which is located at `addrs[NDIRECT+1]`. Within the
block, find the next block by taking (or create) with the index of `bn1`.
Then, in this block, find the address with the index of `bn2`. These steps are
similar to find the address of triple indirect blocks.

#### itrunc

This function truncates the inode by discarding all blocks.

Similar to the indirect blocks freed by a loop, nested loop is used to free
the double indirect blocks. While traversing the entire tree, free the leaf
and its parent node recursively. Finally, the root is freed and set to zero.
triple indirect blocks are freed with a triple-nested loops.

### Test Cases

> It takes a long time to create and read 16Mbytes file. The test program
> should be guaranteed to run for an enough period.

`test_file1.c` tests the extension of the maximum file size.
There are three test cases; `test_write`, `test_read` and `test_stress`.

In `test_write`, it creates a new file and repeatedlyy write the characters
`CHARACTERS` until the offset reaches to the maximum file size.

In `test_read`, it opens the created file and reads it. The size of reading
the file for each is same as buffer size. While reading, it checks the buffer
whether all characters are correctly written in the file. If not matching, the
function will return non zero value.

In `test_stress`, it repeats `test_write` and `test_read` for the given
times.

## pread and pwrite

### System Call

`pread` and `pwrite` system calls are similar to `read` and `write` except
that they don't change the offset. Instead, they take `off` parameter to
indicate the offset.

Even if they can reach any offset defined in `off`, the size of write or read
is restricted by the file size. For the exceeding the file size, return value
won't be the given size.

In `file.c`, `filepread` and `filepwrite` are based on `fileread` and
`filewrite`. In those functions, `readi` and `writei` are called with the
offset passed in the parameter. In addition, the code of changing `f->off` is
discarded.

### User Library

In `types.h`, structure `thread_safe_guard` is defined. It contains `int fd`
and `rwlock_t rwlock`.

The user APIs are defined in `pfile.c`. In `thread_safe_guard_init`,
`thread_safe_guard` is allocated via `malloc` and its memory is set zero. Its
`fd` and `rwlock` are also initialized. Then, it returns the address of newly
allocated `thread_safe_guard`. `thread_safe_guard_destroy` just frees the
allocated memory.

In `thread_safe_pread` and `thread_safe_pwrite`, `pread` and `pwrite` system
calls are wrapped by rwlock's acquire/release functions. They return the same
results of `pread` and `pwrite`.

### Test Cases

> `test_race` is based on *"Student-created Readers-Writers Lock Test Code"* in
> Piazza @147 posted by Ji Seong, Ham.

There are 4 test cases in `test_file2.c`; `test_init_destroy`,
`test_write_read`, `test_stress` and `test_race`.

`test_init_destroy` opens a new file, initializes a `thread_safe_guard` and
destroy it.

`test_write_read` opens a new file, writes and reads it. During the writing
and reading, it is done in the reverse order. In order to test whether the
mannual offset works properly or not, it matches the characters between the
read buffer and given one.

`test_stress` runs `test_write_read` for the give number of times.

`test_race` opens a new file and writes first. Then it creates multiple
threads performing `test_thread_pwrite` or `test_thread_pread`. The number of
each start routine is decided by `READERS_RATIO`.
For each function, `pwrite` or `pread` are repeatedly called and it exits.

