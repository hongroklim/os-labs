typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint           pde_t;
typedef unsigned int   thread_t;

typedef struct {
  int value;
  uint locked;
  int queue[64];
  int front;
  int rear;
} xem_t;

typedef struct {
  
} rwlock_t;
