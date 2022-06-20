#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

//#define DEBUG
#define N_STRESS 2
#define FILE_SIZE (128)

#define REP 10
#define NTHREADS 10
#define READERS_RATIO 0.9

#define CHARS_LEN 62
char CHARACTERS[CHARS_LEN+1] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int test_init_destroy(const char* file_name);
int test_write_read(const char* file_name);
int test_stress(const char* file_name);
int test_race(const char* file_name);

void * test_thread_pwrite(void *arg);
void * test_thread_pread(void *arg);

thread_safe_guard *race_guard;

int
main(int argc, char *argv[])
{
  int i;
  char file_name[8] = "test_f0";

  // Init and Destroy Test
  if(test_init_destroy(file_name) != 0){
    printf(1, "painc at test_init_destroy\n");
    exit();
  }else{
    printf(1, "test_init_destroy ok\n");
  }

  // Write and Read Test
  if(test_write_read(file_name) != 0){
    printf(1, "painc at test_write_read\n");
    exit();
  }else{
    printf(1, "test_write_read ok\n");
  }

  // Stress Test
  for(i = 0; i < N_STRESS; i++){
    file_name[6] = '0' + i;
    if(test_stress(file_name) != 0){
      printf(1, "painc at test_stress[%d]\n", i);
      exit();
    }
  }
  printf(1, "test_stress ok\n");

  // Read Test
  if(test_race(file_name) != 0){
    printf(1, "painc at test_race\n");
    exit();
  }else{
    printf(1, "test_race ok\n");
  } exit();
}

int
test_init_destroy(const char* file_name)
{
  int fd;
  thread_safe_guard *file_guard;

  fd = open(file_name, O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "open fail\n");
    return -1;
  }

  // Initialize
  file_guard = thread_safe_guard_init(fd);
  printf(1, "fd: %d, file_guard->fd: %d\n", fd, file_guard->fd);

  // Destroy
  thread_safe_guard_destroy(file_guard);

  return 0; }

int
test_write_read(const char* file_name)
{
  int buf_size, fd, size, off, i;
  buf_size = 32;
  char buf[buf_size];
  thread_safe_guard *file_guard;

  fd = open(file_name, O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "open fail\n");
    return -1;
  }

  file_guard = thread_safe_guard_init(fd);

  // Write in plain
  memset(buf, 0, buf_size);
  off = 0;
  while(off < FILE_SIZE){
    size = (off+buf_size < FILE_SIZE) ? buf_size : (FILE_SIZE-buf_size);

    if(write(fd, buf, size) != size){
      printf(1, "write fail at %d\n", off);
      close(fd);
      return -1;
    }

    off += size;
  }

  // Write in reverse
  size = FILE_SIZE % buf_size;
  off = FILE_SIZE - size;
  while(off >= 0){
    // Set buffer
    memset(buf, 0, buf_size);
    for(i = 0; i < buf_size; i++)
      buf[i] = CHARACTERS[(off+i)%CHARS_LEN];

    // Write
    if(thread_safe_pwrite(file_guard, buf, size, off) != size){
      printf(1, "pwrite fail at %d\n", off);
      thread_safe_guard_destroy(file_guard);
      close(fd);
      return -1;
    }

    size = buf_size;
    off -= buf_size;
  }

  // Read in reverse
  size = FILE_SIZE % buf_size;
  off = FILE_SIZE - size;
  while(off >= 0){
    // Set buffer
    memset(buf, 0, buf_size);

    // Read
    if(thread_safe_pread(file_guard, buf, size, off) != size){
      printf(1, "pread fail at %d\n", off);
      thread_safe_guard_destroy(file_guard);
      close(fd);
      return -1;
    }

    // Match
    for(i = 0; i < size; i++){
#ifdef DEBUG
      printf(1, "%c", buf[i]);
#endif

      if(buf[i] != CHARACTERS[(off+i)%CHARS_LEN]){
        printf(1, "\nmatch fail at %d (expected %c, but %c)\n",
            off+i, buf[i], CHARACTERS[(off+i)%CHARS_LEN]);
        thread_safe_guard_destroy(file_guard);
        close(fd);
        return -1;
      }
    }
#ifdef DEBUG
      printf(1, "\n");
#endif

    size = buf_size;
    off -= buf_size;
  }

  thread_safe_guard_destroy(file_guard);
  close(fd);

  return 0;
}

int
test_stress(const char* file_name)
{
  return test_write_read(file_name);
}

int
test_race(const char* file_name)
{
  int fd;
  thread_t t[NTHREADS];
  void *ret;

  fd = open(file_name, O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "open fail\n");
    return -1;
  }

  race_guard = thread_safe_guard_init(fd);

  // Write first
  if(thread_create(&t[0], test_thread_pwrite, (void *)(0)) < 0) {
    printf(1, "panic at thread create\n");
    thread_safe_guard_destroy(race_guard);
    close(fd);
    return -1;
  }


  if(thread_join(t[0], &ret) < 0) {
    printf(1, "panic at thread join\n");
    thread_safe_guard_destroy(race_guard);
    close(fd);
    return -1;
  }

  for(int i = 0; i < NTHREADS; ++i) {
    void* (*start_routine)(void *) = ((NTHREADS-i) >= READERS_RATIO * NTHREADS)
      ? test_thread_pwrite : test_thread_pread;

    if(thread_create(&t[i], start_routine, (void *)(i)) < 0) {
      printf(1, "panic at thread create\n");
      thread_safe_guard_destroy(race_guard);
      close(fd);
      return -1;
    }
  }

  for(int i = 0; i < NTHREADS; ++i) {
    if(thread_join(t[i], &ret) < 0) {
      printf(1, "panic at thread join\n");
      thread_safe_guard_destroy(race_guard);
      close(fd);
      return -1;
    }
  }

  thread_safe_guard_destroy(race_guard);
  close(fd);

  return 0;
}

void *
test_thread_pwrite(void *arg)
{
#ifdef DEBUG
  int id = (int)arg;
#endif
  int buf_size, i;
  buf_size = 128;

  char buf[buf_size];
  memset(buf, 0, buf_size);

  for(i = 0; i < REP; i++){
    if(thread_safe_pwrite(race_guard, buf, buf_size, i*buf_size) != buf_size){
      printf(1, "pwrite fail at %d\n", i*buf_size);
      thread_exit((void*)(-1));
      return 0;
    }

#ifdef DEBUG
    printf(1, "%d", id);
#endif
  }

  thread_exit((void*)(0));
  return 0;
}

void *
test_thread_pread(void *arg)
{
#ifdef DEBUG
  int id = (int)arg;
#endif
  int buf_size, i;
  buf_size = 128;

  char buf[buf_size];

  for(i = 0; i < REP; i++){
    memset(buf, 0, buf_size);
    if(thread_safe_pread(race_guard, buf, buf_size, i*buf_size) != buf_size){
      printf(1, "pread fail at %d\n", i*buf_size);
      thread_exit((void*)(-1));
      return 0;
    }

#ifdef DEBUG
    printf(1, "%d", id);
#endif
  }

  thread_exit((void*)(0));
  return 0;
}
