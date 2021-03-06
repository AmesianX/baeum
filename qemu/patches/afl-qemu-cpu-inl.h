/*
   american fuzzy lop - high-performance binary-only instrumentation
   -----------------------------------------------------------------

   Written by Andrew Griffiths <agriffiths@google.com> and
              Michal Zalewski <lcamtuf@google.com>

   Idea & design very much by Andrew Griffiths.

   Copyright 2015, 2016 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This code is a shim patched into the separately-distributed source
   code of QEMU 2.2.0. It leverages the built-in QEMU tracing functionality
   to implement AFL-style instrumentation and to take care of the remaining
   parts of the AFL fork server logic.

   The resulting QEMU binary is essentially a standalone instrumentation
   tool; for an example of how to leverage it for other purposes, you can
   have a look at afl-showmap.c.

 */

/***************************
 * VARIOUS AUXILIARY STUFF *
 ***************************/

#define FORKSRV_FD 198
#define TSL_FD (FORKSRV_FD - 1)

extern abi_ulong mmap_next_start;
extern void global_baeum_setup(void);
extern void global_node_update(abi_ulong);

/* Set in the child process in forkserver mode: */

static unsigned char afl_fork_child;
unsigned int afl_forksrv_pid;

/* Function declarations. */

static void afl_forkserver(CPUArchState*);

static void afl_wait_tsl(CPUArchState*, int);
void afl_request_tsl(target_ulong, target_ulong, uint64_t, int);

static TranslationBlock *tb_htable_lookup(CPUState *,
                                          target_ulong,
                                          target_ulong,
                                          uint32_t);

/* Data structure passed around by the translate handlers: */

struct afl_tsl {
  int global;
  target_ulong pc;
  target_ulong cs_base;
  uint64_t flags;
};


/*************************
 * ACTUAL IMPLEMENTATION *
 *************************/

/* Fork server logic, invoked once we hit _start. */

static void afl_forkserver(CPUArchState *env) {

  static unsigned char tmp[4];

  global_baeum_setup();

  /* Tell the parent that we're alive. If the parent doesn't want
     to talk, assume that we're not running in forkserver mode. */

  if (write(FORKSRV_FD + 1, tmp, 4) != 4) return;

  afl_forksrv_pid = getpid();

  /* All right, let's await orders... */

  while (1) {

    pid_t child_pid;
    int status, t_fd[2];

    /* Whoops, parent dead? */

    if (read(FORKSRV_FD, &tmp, 4) != 4) exit(2);

    /* Establish a channel with child to grab translation commands. We'll
       read from t_fd[0], child will write to TSL_FD. */

    if (pipe(t_fd) || dup2(t_fd[1], TSL_FD) < 0) exit(3);
    close(t_fd[1]);

    child_pid = fork();
    if (child_pid < 0) exit(4);

    if (!child_pid) {

      /* Child process. Close descriptors and run free. */

      afl_fork_child = 1;
      close(FORKSRV_FD);
      close(FORKSRV_FD + 1);
      close(t_fd[0]);
      return;

    }

    /* Parent. */

    close(TSL_FD);

    if (write(FORKSRV_FD + 1, &child_pid, 4) != 4) exit(5);

    /* Collect translation requests until child dies and closes the pipe. */

    afl_wait_tsl(env, t_fd[0]);

    /* Get and relay exit status to parent. */

    if (waitpid(child_pid, &status, 0) < 0) exit(6);
    if (write(FORKSRV_FD + 1, &status, 4) != 4) exit(7);

  }

}

/* This code is invoked whenever QEMU decides that it doesn't have a
   translation of a particular block and needs to compute it. When this happens,
   we tell the parent to mirror the operation, so that the next fork() has a
   cached copy. */

void afl_request_tsl(target_ulong pc, target_ulong cb, uint64_t flags, int global) {

  struct afl_tsl t;

  if (!afl_fork_child) return;

  t.global  = global;
  t.pc      = pc;
  t.cs_base = cb;
  t.flags   = flags;

  if (write(TSL_FD, &t, sizeof(struct afl_tsl)) != sizeof(struct afl_tsl))
    return;

}


/* This is the other side of the same channel. Since timeouts are handled by
   afl-fuzz simply killing the child, we can just wait until the pipe breaks. */

static void afl_wait_tsl(CPUArchState *env, int fd) {

  CPUState *cpu = ENV_GET_CPU(env);
  TranslationBlock *tb;
  struct afl_tsl t;

  while (1) {

    /* Broken pipe means it's time to return to the fork server routine. */

    if (read(fd, &t, sizeof(struct afl_tsl)) != sizeof(struct afl_tsl))
      break;

    if (t.global)
      global_node_update(t.pc);
    else if (t.pc < mmap_next_start) {
      mmap_lock();
      tb_lock();

      /* There's a chance that our desired tb has been translated while
       * taking the locks so we check again inside the lock.
       */
      tb = tb_htable_lookup(cpu, t.pc, t.cs_base, t.flags);
      if (!tb) {
          /* if no translated code available, then translate it now */
          tb = tb_gen_code(cpu, t.pc, t.cs_base, t.flags, 0);
      }

      mmap_unlock();

      /* We add the TB in the virtual pc hash table for the fast lookup */
      atomic_set(&cpu->tb_jmp_cache[tb_jmp_cache_hash_func(t.pc)], tb);
      tb_unlock();
    }

  }

  close(fd);

}
