#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define TIMERTICK 5
#define STARVETIME1 20
#define STARVETIME2 100
#define STARVETIME3 200
#define STARVETIME4 400

#define TIMEQUANTUM0 TIMERTICK
#define TIMEQUANTUM1 TIMERTICK*2
#define TIMEQUANTUM2 TIMERTICK*4
#define TIMEQUANTUM3 TIMERTICK*8

// int oldpriority=60;
// int newpriority=60;

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void promotion(struct proc *p, int newq)
{
  p->queuepriority = newq;
  p->qstarttime = ticks;
  p->qruntime = 0;
  cprintf("Promoted to %d",newq);
}
// void demotion(struct proc *p, int oldq , int newq)
// {
//   int oldque = oldq;
//   p->queuepriority = newq;
//   p->qstarttime = ticks;
//   p->qruntime = 0;
// }


void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->ctime = ticks;
  p->rtime = 0;

  p->Prio = 60;
  p->queuepriority = 0;
  for(int qno=0; qno<5 ; qno++)
  {
    p->qrt[qno] = 0; 
    p->qwt[qno] = 0; 
  }

  p->qruntime = 0 ;
  p->qstarttime = ticks;
  p->qwaittime = 0;

  
  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  curproc->etime = ticks;
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        p->etime = 0;
        p->ctime = 0;
        p->rtime = 0;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p=0;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // int schedmode=2;

    // Loop over process table looking for process to run.
   
        #ifdef DEFAULT
          // cprintf("DEfault\n");
          acquire(&ptable.lock);
          for(p=ptable.proc ; p < &ptable.proc[NPROC];p++)
          {
            if (p->state != RUNNABLE)
            continue;
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;
            swtch(&(c->scheduler), p->context);
            switchkvm();
            c->proc = 0;
          }
          release(&ptable.lock);
        #endif
      // else if(schedmode == 2)
        
        #ifdef FCFS
          acquire(&ptable.lock);
          // cprintf("FCFS\n");
          struct proc *min = 0;
          struct proc *pengu = 0;
          
          for(pengu = ptable.proc; pengu < &ptable.proc[NPROC]; pengu++)
            {
              if(pengu->state == RUNNABLE)
              {
                 if (min!=0)
                 {
                  if(pengu->ctime < min->ctime)
                  min = pengu;
                  }
                else
                min = pengu;
              }
            }
            if(min!=0)
            {
              if(min->state == RUNNABLE)
              p=min;
              c->proc = p;
              switchuvm(p);
              p->state = RUNNING;
              swtch(&(c->scheduler), p->context);
              switchkvm();
              c->proc = 0;
            }
            release(&ptable.lock);
        #endif
        
        #ifdef PBS
            acquire(&ptable.lock);
            struct proc *maxpriority = 0;
            struct proc *iterator = 0;
          
            int cteqp = 0;
            for(iterator = ptable.proc; iterator < &ptable.proc[NPROC]; iterator++)
            {
              if(iterator->state == RUNNABLE)
              {
                if (maxpriority!=0)
                  {
                    if(maxpriority->Prio == iterator->Prio)
                    {
                      cteqp++;
                    }
                    if(maxpriority->Prio > iterator->Prio )
                      {
                        maxpriority = iterator;
                        cteqp = 0;
                      }
                  }
                else
                  {
                    maxpriority = iterator;
                    cteqp++;
                  }
              }
            }
            
            if(maxpriority!=0)
            {
                if( cteqp > 1)
                {
                  for(iterator = ptable.proc; iterator < &ptable.proc[NPROC]; iterator++)
                  {
                    if(maxpriority->state == RUNNABLE && iterator->Prio == maxpriority->Prio)
                    {
                      if( iterator->state == RUNNABLE )
                        p=iterator;
                      c->proc=p;
                      switchuvm(p);
                      p->state = RUNNING;
                      swtch(&(c->scheduler), p->context);
                      switchkvm();
                    }
                  }
                }
                else
                {
                    if( maxpriority->state == RUNNABLE )
                      p=maxpriority;
                    c->proc=p;
                    switchuvm(p);
                    p->state = RUNNING;
                    swtch(&(c->scheduler), p->context);
                    switchkvm();
                }
            }
            
            release(&ptable.lock);
        #endif
        
        #ifdef MLFQ
            acquire(&ptable.lock);
            struct proc *iter = 0;
            for( iter = ptable.proc ; iter < &ptable.proc[NPROC]; iter++ )
            {
              if(iter->state == RUNNABLE)
              {
                //  SOLVING STARVATION
                iter->qwaittime = ticks - iter->qstarttime - iter->qruntime;
                if( iter->queuepriority == 1 && iter->qwaittime > STARVETIME1 )
                {
                  promotion(iter,0);
                }
                if( iter->queuepriority == 2 && iter->qwaittime > STARVETIME2 )
                {
                  promotion(iter,1);
                } 
                if( iter->queuepriority == 3 && iter->qwaittime > STARVETIME3 )
                {
                  promotion(iter,2);
                } 
                if( iter->queuepriority == 4 && iter->qwaittime > STARVETIME4 )
                {
                  promotion(iter,3);
                }                 
              }
            }
            release(&ptable.lock);


            acquire(&ptable.lock);
            for( iter = ptable.proc ; iter < &ptable.proc[NPROC]; iter++ )
            {
              //  SOLVING PRE-EMPTION
              if( iter->state == RUNNING)
              {
                if( iter->queuepriority == 0 && iter->qruntime > TIMEQUANTUM0 )
                {
                  iter->queuepriority = 1;
                  iter->qstarttime = ticks;
                  iter->qruntime = 0;
                  cprintf("Demoted to 1\n");
                }
                if( iter->queuepriority == 1 && iter->qruntime > TIMEQUANTUM1 )
                {
                  iter->queuepriority = 2;
                  iter->qstarttime = ticks;
                  cprintf("Demoted to 2\n");
                  iter->qruntime = 0;
                }
                if( iter->queuepriority == 2 && iter->qruntime > TIMEQUANTUM2 )
                {
                  iter->queuepriority = 3;
                  iter->qstarttime = ticks;
                  cprintf("Demoted to 3\n");
                  iter->qruntime = 0;
                }
                if( iter->queuepriority == 3 && iter->qruntime > TIMEQUANTUM3 )
                {
                  iter->queuepriority = 4;
                  iter->qstarttime = ticks;
                  iter->qruntime = 0;
                }
              }
            }
            release(&ptable.lock);
            

            for( int qno = 0 ; qno < 5; qno++ )
            {
              acquire(&ptable.lock);
              if( qno < 4 )
              {
                // First 4 queues
                 struct proc *min = 0;
                 for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
                  {
                    if(p->state == RUNNABLE && p->queuepriority==qno)
                    {
                      if (min!=0)
                      {
                        if(p->ctime < min->ctime)
                        min = p;
                      }
                      else
                        min = p;
                    }
                  }
                  if(min!=0)
                  {
                    if(min->state==RUNNABLE)
                      p = min;
                    c->proc = p;
                    switchuvm(p);
                    p->state = RUNNING;
                    swtch(&(c->scheduler), p->context);
                    switchkvm();
                    // Process is done running for now.
                    // It should have changed its p->state before coming back.
                    c->proc = 0;
                  }
              }

              else
              {//Last queue
                for(p=ptable.proc ; p < &ptable.proc[NPROC];p++)
                {
                  if (p->state != RUNNABLE || p->queuepriority != qno)
                  continue;
                  c->proc = p;
                  switchuvm(p);
                  p->state = RUNNING;
                  swtch(&(c->scheduler), p->context);
                  switchkvm();
                  c->proc = 0;
                  //Suimple Round RObin
                }
              }
  
              release(&ptable.lock);

            }


        #endif
        
        
        
        
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        // Process is done running for now.
        // It should have changed its p->state before coming back.
      
    
    
  }
}

  // Enter scheduler.  Must hold only ptable.lock
  // and have changed proc->state. Saves and restores
  // intena because intena is a property of this
  // kernel thread, not this CPU. It should
  // be proc->intena and proc->ncli, but that would
  // break in the few places where a lock is held but
  // there's no process.
  void
  sched(void)
  {
    int intena;
    struct proc *p = myproc();

    if (!holding(&ptable.lock))
      panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
      panic("sched locks");
    if (p->state == RUNNING)
      panic("sched running");
    if (readeflags() & FL_IF)
      panic("sched interruptible");
    intena = mycpu()->intena;
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
  }

  // Give up the CPU for one scheduling round.
  void
  yield(void)
  {
    acquire(&ptable.lock); //DOC: yieldlock
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
  }

  // A fork child's very first scheduling by scheduler()
  // will swtch here.  "Return" to user space.
  void
  forkret(void)
  {
    static int first = 1;
    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first)
    {
      // Some initialization functions must be run in the context
      // of a regular process (e.g., they call sleep), and thus cannot
      // be run from main().
      first = 0;
      iinit(ROOTDEV);
      initlog(ROOTDEV);
    }

    // Return to "caller", actually trapret (see allocproc).
  }

  // Atomically release lock and sleep on chan.
  // Reacquires lock when awakened.
  void
  sleep(void *chan, struct spinlock *lk)
  {
    struct proc *p = myproc();

    if (p == 0)
      panic("sleep");

    if (lk == 0)
      panic("sleep without lk");

    // Must acquire ptable.lock in order to
    // change p->state and then call sched.
    // Once we hold ptable.lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup runs with ptable.lock locked),
    // so it's okay to release lk.
    if (lk != &ptable.lock)
    {                        //DOC: sleeplock0
      acquire(&ptable.lock); //DOC: sleeplock1
      release(lk);
    }
    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    if (lk != &ptable.lock)
    { //DOC: sleeplock2
      release(&ptable.lock);
      acquire(lk);
    }
  }

  //PAGEBREAK!
  // Wake up all processes sleeping on chan.
  // The ptable lock must be held.
  static void
  wakeup1(void *chan)
  {
    struct proc *p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if (p->state == SLEEPING && p->chan == chan)
        p->state = RUNNABLE;
  }

  // Wake up all processes sleeping on chan.
  void
  wakeup(void *chan)
  {
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
  }

  // Kill the process with the given pid.
  // Process won't exit until it returns
  // to user space (see trap in trap.c).

  int kill(int pid)
  {
    struct proc *p;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->pid == pid)
      {
        p->killed = 1;
        // Wake process from sleep if necessary.
        if (p->state == SLEEPING)
          p->state = RUNNABLE;
        release(&ptable.lock);
        return 0;
      }
    }
    release(&ptable.lock);
    return -1;
  }

  int waitx(int *wtime, int *rtime)
  {
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for (;;)
    {
      // Scan through table looking for exited children.
      havekids = 0;
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->parent != curproc)
          continue;
        havekids = 1;
        if (p->state == ZOMBIE)
        {
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          p->state = UNUSED;

          *wtime = p->etime - p->ctime - p->rtime;
          *rtime = p->rtime;

          // printf("%d %d\n",wtime,rtime);

          p->ctime = 0;
          p->etime = 0;
          p->rtime = 0;

          release(&ptable.lock);
          return pid;
        }
      }

      // No point waiting if we don't have any children.
      if (!havekids || curproc->killed)
      {
        release(&ptable.lock);
        return -1;
      }

      // cprintf("Vanakkam da maapla !! Waitx functionla irundhu\n");
      // Wait for children to exit.  (See wakeup1 call in proc_exit.)
      sleep(curproc, &ptable.lock); //DOC: wait-sleep
    }
  }

  //PAGEBREAK: 36
  // Print a process listing to console.  For debugging.
  // Runs when user types ^P on console.
  // No lock to avoid wedging a stuck machine further.
  void
  procdump(void)
  {
    static char *states[] = {
        [UNUSED] "unused",
        [EMBRYO] "embryo",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    int i;
    struct proc *p;
    char *state;
    uint pc[10];

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state == UNUSED)
        continue;
      if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
        state = states[p->state];
      else
        state = "???";
      cprintf("%d %s %s", p->pid, state, p->name);
      if (p->state == SLEEPING)
      {
        getcallerpcs((uint *)p->context->ebp + 2, pc);
        for (i = 0; i < 10 && pc[i] != 0; i++)
          cprintf(" %p", pc[i]);
      }
      cprintf("\n");
    }
  }

  void updatertime()
  {
    struct proc *p;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state == RUNNING)
      {
        p->rtime++;
      }
    }
  }

  int setpriority(int P)
  {
   
    if(P < 0 || P > 100)
    {
      // IF the new priority is not within the given limits [0,100] then DO NOT UPDATE THE PRIORITY
      return 1;
    }
    
    else
    {
      int oldpriority =  myproc()->Prio;
      myproc()->Prio = P;
      // cprintf("New priority = %d and Old priority = %d\n", myproc()->Prio , oldpriority);
      return oldpriority;
    }
  }

  int getpinfo(struct proc_stat *P,int pid)
  {
    cprintf("HEllo %d\n",pid);
    return 1;
  }