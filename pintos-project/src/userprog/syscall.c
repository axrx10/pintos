#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#define STDIN 0
#define STDOUT 1

#define SYS_CALL_NUM 20
// Function to handle system calls
static void syscall_handler (struct intr_frame *f);

// Process identifier
typedef int pid_t;

// Declarations for system call functions
void sys_halt(struct intr_frame* f); /* Halt the operating system. */
void sys_exit(struct intr_frame* f); /* Terminate this process. */
void sys_exec(struct intr_frame* f); /* Start another process. */
void sys_wait(struct intr_frame* f); /* Wait for a child process to die. */
void sys_open(struct intr_frame* f); /*Open a file. */
void sys_read(struct intr_frame* f);  /* Read from a file. */
void sys_write(struct intr_frame* f); /* Write to a file. */

// Declarations for system call wrapper functions
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t exec (const char *file);
int wait (pid_t);
int open (const char *file);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);

// Declarations for file descriptor manipulation functions
static struct file *find_file_by_fd (int fd);
static struct fd_entry *find_fd_entry_by_fd (int fd);
static int alloc_fid (void);
static struct fd_entry *find_fd_entry_by_fd_in_process (int fd);


// Array of system call handlers
static void (*syscall_handlers[SYS_CALL_NUM])(struct intr_frame *);

// Structure for a file descriptor entry
struct fd_entry {
  int fd;
  struct file *file;
  struct list_elem elem;
  struct list_elem thread_elem;
};

// List of file descriptor entries
static struct list file_list;

// Generates a unique file descriptor ID for the current process, starting at 2
static int alloc_fid(void) {
  static int fid = 2;
  return fid++;
}

// Finds a file descriptor entry in the current thread's file descriptor list by ID
static struct fd_entry *find_fd_entry_by_fd_in_process(int fd) {
  struct thread *t = thread_current();
  struct list_elem *l;
  for (l = list_begin(&t->fd_list); l != list_end(&t->fd_list); l = list_next(l)) {
    struct fd_entry *entry = list_entry(l, struct fd_entry, thread_elem);
    if (entry->fd == fd) {
      return entry;
    }
  }
  return NULL;
}

// Finds a file by file descriptor ID
static struct file *find_file_by_fd(int fd) {
  struct fd_entry *entry = find_fd_entry_by_fd_in_process(fd);
  if (entry) {
    return entry->file;
  }
  return NULL;
}



// Waits for a process with the given PID to terminate
int wait(pid_t pid) {
  return process_wait(pid);
}

// Writes the given buffer to stdout or a file
int write(int fd, const void *buffer, unsigned length) {
  if (fd == STDOUT) {
    // Output to stdout
    putbuf((char *)buffer, (size_t)length);
    return (int)length;
  } else {
    // Output to file
    struct file *f = find_file_by_fd(fd);
    if (f == NULL) {
      exit(-1);
    }
    return (int)file_write(f, buffer, length);
  }
}

int open(const char *file) {
if (!file) return -1;
struct file *f = filesys_open(file);
if (!f) return -1;

struct fd_entry *fd_entry = malloc(sizeof(struct fd_entry));
if (!fd_entry) {
file_close(f);
return -1;
}
fd_entry->fd = alloc_fid();
fd_entry->file = f;

struct thread *curr_thread = thread_current();
list_push_back(&curr_thread->fd_list, &fd_entry->thread_elem);
list_push_back(&file_list, &fd_entry->elem);

return fd_entry->fd;
}

// Exits the current thread with the given status
void exit(int status) {
  thread_exit();
}

// Reads data from a file or stdin into the given buffer
int read(int fd, void *buffer, unsigned length) {
  printf("call read %d\n", fd);
  if (fd == STDIN) {
    // Read from stdin
    for (unsigned int i = 0; i < length; i++) {
      *((char **)buffer)[i] = input_getc();
    }
    return length;
  } else {
    // Read from file
    struct file *f = find_file_by_fd(fd);
    if (f == NULL) {
      return -1;
    }
    return file_read(f, buffer, length);
  }
}

// Creates a new process and executes the specified file
pid_t exec(const char *file) {
  return process_execute(file);
}


/*This code initializes the system call functionality in the operating system.
It registers an interrupt handler for system calls, 
and sets up an array of function pointers for each system call.
It also initializes a lock and list that will be 
used to manage file descriptor entries.*/

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  syscall_handlers[SYS_HALT] = &sys_halt;
  syscall_handlers[SYS_EXIT] = &sys_exit;
  syscall_handlers[SYS_WAIT] = &sys_wait;
  syscall_handlers[SYS_OPEN] = &sys_open;
  syscall_handlers[SYS_WRITE] = &sys_write;
  syscall_handlers[SYS_READ] = &sys_read;
  syscall_handlers[SYS_EXEC] = &sys_exec;


  lock_init(&file_lock);
  list_init (&file_list);
}


/* Function to read a single byte from the virtual address specified by the argument UADDR. 
It is assumed that UADDR is a valid address below PHYS_BASE. 
If the function is successful, 
it returns the value of the byte at that address. 
If a segfault occurs, the function returns -1.*/
static int get_user(const uint8_t *uaddr) {
  // Check that the address is a valid user address
  if (!is_user_vaddr((void *)uaddr)) {
    return -1;
  }
  // Check that the page containing the address is present in the page directory
  if (pagedir_get_page(thread_current()->pagedir, uaddr) == NULL) {
    return -1;
  }
  int result;
  // Read the byte at the address
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to the user address UDST.
UDST must be a valid address below PHYS_BASE.
If the function is successful, it returns true.
If a segfault occurs, the function returns false. */
static bool put_user(uint8_t *udst, uint8_t byte) {
  // Check that the address is a valid user address
  if (!is_user_vaddr(udst)) {
    return false;
  }
  int error_code;
  // Write the byte to the address
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

// Returns true if the address range specified by ESP and ARGC is a valid set of pointers.
// Returns false if any of the addresses are invalid.
bool is_valid_pointer(void* esp, uint8_t argc) {
  uint8_t i = 0;
  for (; i < argc; ++i) {
    // Check that the address is a valid user address and that the page is present
    if ((!is_user_vaddr(esp)) || (pagedir_get_page(thread_current()->pagedir, esp) == NULL)) {
      return false;
    }
  }
  return true;
}

// Returns true if the string pointed to by STR is a valid string.
// Returns false if the string is invalid or if a segfault occurred while reading it.
bool is_valid_string(void *str) {
  int ch = -1;
  while ((ch = get_user((uint8_t*)str++)) != '\0' && ch != -1);
  if (ch == '\0') {
    return true;
  } else {
    return false;
  }
}

// Halts the operating system
void sys_halt(struct intr_frame* f) {
  shutdown();
}

// Terminates this process
void sys_exit(struct intr_frame* f) {
  // Check that the status argument is a valid pointer
  if (!is_valid_pointer(f->esp + 4, 4)) {
    exit(-1);
  }
  int status = *(int *)(f->esp + 4);
  exit(status);
}

void sys_open(struct intr_frame* f) {
  // Check that the file name pointer is a valid user pointer
  if (!is_valid_pointer(f->esp + 4, 4)) {
    exit(-1);
  }
  // Check that the file name is a null-terminated string
  if (!is_valid_string(*(char **)(f->esp + 4))) {
    exit(-1);
  }
  // Get the file name
  char *file_name = *(char **)(f->esp + 4);
  // Open the file
  lock_acquire(&file_lock);
  int fd = open(file_name);
  lock_release(&file_lock);
  // Return the file descriptor
  f->eax = fd;
}


// Starts another process
void sys_exec(struct intr_frame* f) {
  // Check that the file name argument is a valid string
  if (!is_valid_pointer(f->esp + 4, 4) || !is_valid_string(*(char **)(f->esp + 4))) {
    exit(-1);
  }
  char *file_name = *(char **)(f->esp + 4);
  // Acquire the file lock before executing the process
  lock_acquire(&file_lock);
  f->eax = exec(file_name);
  lock_release(&file_lock);
}


// Wait for a child process to die
void sys_wait(struct intr_frame* f) {
  pid_t pid;
  // Check that the pid argument is a valid pointer
  if (!is_valid_pointer(f->esp + 4, 4)) {
    exit(-1);
  }
  pid = *((int*)f->esp + 1);
  f->eax = wait(pid);
}

// Read from a file
void sys_read(struct intr_frame* f) {
  // Check that the arguments are valid pointers
  if (!is_valid_pointer(f->esp + 4, 12)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  void *buffer = *(char**)(f->esp + 8);
  unsigned size = *(unsigned *)(f->esp + 12);
  // Check that the buffer and buffer + size addresses are valid
  if (!is_valid_pointer(buffer, 1) || !is_valid_pointer(buffer + size, 1)) {
    exit(-1);
  }
  // Acquire the file lock before reading from the file
  lock_acquire(&file_lock);
  f->eax = read(fd, buffer, size);
  lock_release(&file_lock);
}

// Write to a file
void sys_write(struct intr_frame* f) {
  // Check that the arguments are valid pointers
  if (!is_valid_pointer(f->esp + 4, 12)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  void *buffer = *(char**)(f->esp + 8);
  unsigned size = *(unsigned *)(f->esp + 12);
  // Check that the buffer and buffer + size addresses are valid
  if (!is_valid_pointer(buffer, 1) || !is_valid_pointer(buffer + size, 1)) {
    exit(-1);
  }
  // Acquire the file lock before writing to the file
  lock_acquire(&file_lock);
  f->eax = write(fd, buffer, size);
  lock_release(&file_lock);
  return;
}



// Handle system calls
static void syscall_handler (struct intr_frame *f) {
  // Check that the system call number is a valid pointer
  if (!is_valid_pointer(f->esp, 4)) {
    exit(-1);
    return;
  }
  int syscall_num = *(int *)f->esp;
  // Check that the system call number is within a valid range
  if (syscall_num <= 0 || syscall_num >= SYS_CALL_NUM) {
    exit(-1);
  }
  // Invoke the appropriate system call handler function
  syscall_handlers[syscall_num](f);
}




// Find a file descriptor entry in the list of open files
static struct fd_entry *
find_fd_entry_by_fd (int fd) {
  struct fd_entry *ret;
  struct list_elem *l;
  // Iterate through the list of open files and find the entry with the specified file descriptor
  for (l = list_begin (&file_list); l != list_end (&file_list); l = list_next (l)) {
    ret = list_entry (l, struct fd_entry, elem);
    if (ret->fd == fd) {
      return ret;
    }
  }
  return NULL;
}