#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define DEBUG
#define N_STRESS 0
#define FILE_SIZE (1024)

#define CHARS_LEN 62
char CHARACTERS[CHARS_LEN+1] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int test_init_destroy(const char* file_name);
int test_write_read(const char* file_name);
int test_stress(const char* file_name);
int test_race(const char* file_name);

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
  }

  exit();
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

  return 0;
}

int
test_write_read(const char* file_name)
{
  int buf_size, fd, size, off, i;
  buf_size = 64;
  char buf[buf_size];
  thread_safe_guard *file_guard;

  fd = open(file_name, O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "open fail\n");
    return -1;
  }

  file_guard = thread_safe_guard_init(fd);

  // Write in reverse
  size = FILE_SIZE % buf_size;
  off = FILE_SIZE - size;
  while(off >= 0){
    // Set buffer
    memset(buf, 0, buf_size);
    for(i = 0; i < buf_size; i++)
#ifdef DEBUG
      printf(1, "%c", buf[i]);
#endif
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
      if(buf[i] != CHARACTERS[(off+i)%CHARS_LEN]){
        printf(1, "\nmatch fail at %d (expected %c, but %c)\n",
            off+i, buf[i], CHARACTERS[(off+i)%CHARS_LEN]);
        thread_safe_guard_destroy(file_guard);
        close(fd);
        return -1;
      }
    }

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
  return 0;
}

int
test_race(const char* file_name)
{
  return 0;
}
