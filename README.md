# Operating Systems - XV6

## Cscope

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
