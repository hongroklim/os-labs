#define XEMQSIZE       (64*2) // size of semaphore's queue capacity

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint           pde_t;
typedef unsigned int   thread_t;

typedef struct {
  int value;
  int lockidx;
  int queue[XEMQSIZE];
  int front;
  int rear;
} xem_t;

typedef struct {
  xem_t lock;
  xem_t writelock;
  int queue[XEMQSIZE];
  int readers;
  int wlowner;
} rwlock_t;

typedef struct {
  int fd;
  rwlock_t rwlock;
} thread_safe_guard;
