#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#define DB_OFFSET(idx) ((void *)(&(((struct user *)0)->u_debugreg[idx])))
pid_t pid;
int status;
char buf[4];
void set_hbp(void *addr) {
  if (ptrace(PTRACE_POKEUSER, pid, DB_OFFSET(0), addr) == -1) {
    printf("Failed to set dr_0\n");
    kill(pid, 9);
    exit(-1);
  }
  unsigned long dr_7 =
      (1 << 0) | (1 << 8) | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19);
  if (ptrace(PTRACE_POKEUSER, pid, DB_OFFSET(7), dr_7) == -1) {
    printf("Failed to set dr_7\n");
    kill(pid, 9);
    exit(-1);
  }
}
int main(int argc, char **argv, char **env) {
  pid = fork();
  if (!pid) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);
    sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    raise(SIGSTOP);
    __asm__("mov r15,   0x1111111111111111;"
            "mov r14,   0x2222222222222222;"
            "mov r13,   0x3333333333333333;"
            "mov r12,   0x4444444444444444;"
            "mov rbp,   0x5555555555555555;"
            "mov rbx,   0x6666666666666666;"
            "mov r11,   0x7777777777777777;"
            "mov r10,   0x8888888888888888;"
            "mov r9,    0x9999999999999999;"
            "mov r8,    0xaaaaaaaaaaaaaaaa;"
            "mov rax,   0xbbbbbbbbbbbbbbbb;"
            "mov rcx,   0xcccccccccccccccc;"
            "mov rdx,   0xdddddddddddddddd;"
            "mov rsi,   0xeeeeeeeeeeeeeeee;"
            "mov rdi,   0xffffffffffffffff;");
    buf[0] = 0;
    exit(1);
  }
  waitpid(pid, &status, 0);
  set_hbp(buf);
  ptrace(PTRACE_CONT, pid, NULL, NULL);
  waitpid(pid, &status, 0);
  ptrace(PTRACE_CONT, pid, NULL, NULL);
  waitpid(pid, &status, 0);
  return 0;
}

