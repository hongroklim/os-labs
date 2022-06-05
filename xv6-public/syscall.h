// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21

#define SYS_yield  22
#define SYS_getlev 23
#define SYS_set_cpu_share 24

#define SYS_thread_create 25
#define SYS_thread_exit   26
#define SYS_thread_join   27

#define SYS_xem_init      28
#define SYS_xem_wait      29
#define SYS_xem_unlock    30

#define SYS_rwlock_init               31
#define SYS_rwlock_acquire_readlock   32
#define SYS_rwlock_acquire_writelock  33
#define SYS_rwlock_release_readlock   34
#define SYS_rwlock_release_writelock  35
