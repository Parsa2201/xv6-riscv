struct stat;
struct proc_info;
struct child_processes;
struct report;
struct report_traps;
struct top_proc_info;
struct top;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int child_processes(struct child_processes *);
int report_traps(struct report_traps *);
int load_traps(void);
int create_thread(uint *, void *(*)(void *arg), void *, void *, uint64);
int join_thread(uint);
uint cpu_usage();
int top(struct top *);
int set_cpu_quota(int, int);
int hotfork(int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);

// umalloc.c
void* malloc(uint);
void free(void*);

#define NPROC 64
struct cpu_usage {
  uint sum;
  uint start;
  uint quota;
  uint last_tick;
  uint deadline;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct top_proc_info {
  char name[16];
  int pid;
  int ppid;
  enum procstate state;
  struct cpu_usage usage;
};

struct top {
  int count;
  struct top_proc_info procs[NPROC];
};
