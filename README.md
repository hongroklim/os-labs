# Operating Systems - XV6

This project is forked from [xv6-public](https://github.com/mit-pdos/xv6-public).

## Documents

1. [MLFQ and Stride Scheduling](./docs/01-MLFQ-and-Stride.md)
1. [Light-weight Process](./docs/02-Light-weight-Process.md)
1. [Semaphore and Read-write Lock](./docs/03-Semaphore-and-RW-Lock.md)
1. [File System](./docs/04-File-System.md)

## Cscope

Install [cscope](http://cscope.sourceforge.net) first
and append [vim configuration](./cscope-conf.vim).

```sh
$ cd xv6-public
$ mkcscope.sh
```

* **Command** `cs find [s|g|c|t|e|f|i|d]`
* **Hotkey** `ctrl-\, [s|g|c|t|e|f|i|d]`

|     |          |                                                     |
| --- | -------- | --------------------------------------------------- |
| 's' | symbol   | find all references to the token under cursor       |
| 'g' | global   | find global definition(s) of the token under cursor |
| 'c' | calls    | find all calls to the function name under cursor    |
| 't' | text     | find all instances of the text under cursor         |
| 'e' | egrep    | egrep search for the word under cursor              |
| 'f' | file     | open the filename under cursor                      |
| 'i' | includes | find files that include the filename under cursor   |
| 'd' | called   | find functions that function under cursor calls     |

## QEMU

### Build and Launch

```sh
$ make clean
$ make
$ make CPUS=1 qemu-nox
```

### Exit

`ctrl-A, X`
