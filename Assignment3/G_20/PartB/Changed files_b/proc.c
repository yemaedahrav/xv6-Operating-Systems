#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "stat.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

int swap_out_present=0;
int swap_in_present=0;

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

int
close_process(int fd)
{
  struct file *f;

  if(fd < 0 )
    return -1;
  if(fd >= NOFILE)  
  	return -1;
  if((f=myproc()->ofile[fd]) == 0)
  	return -1;	
  
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
write_process(int fd, char *p, int n)
{
  struct file *f;
   if(fd < 0 )
    return -1;
  if(fd >= NOFILE)  
  	return -1;
  if((f=myproc()->ofile[fd]) == 0)
  	return -1;	
  
  return filewrite(f, p, n);
}


static struct inode*
create_process(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE){
    	if(ip->type == T_FILE)
    		return ip;
    
    }
  
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 )
      panic("create dots");
    if( dirlink(ip, "..", dp->inum) < 0)  
    	panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}


static int
fdalloc_process(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();
  fd=0;
  while(fd < NOFILE){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
    fd++;
  }
  return -1;
}

int open_process(char *path, int omode){

  int fd;
  struct file *f;
  struct inode *ip;

  begin_op();

  if(omode & O_CREATE){
    ip = create_process(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR) {
    	if(omode != O_RDONLY){
    		iunlockput(ip);
      		end_op();
     		 return -1;
    	}
      
    }
  }

  if((f = filealloc()) == 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  if( (fd = fdalloc_process(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;

}

void to_string(int x, char *c){
  if(x==0)
  {
    c[0]='0';
    c[1]='\0';
    return;
  }
 int i;
  for(i=0;x>0;i++){
    c[i]=x%10+'0';
   
    x/=10;
  }
  c[i]='\0';

  for(int j=0;j<i/2;j++){
    char a=c[j];
    c[j]=c[i-j-1];
    c[i-j-1]=a;
  }

}

struct ready_queue{
  struct spinlock lock;
  struct proc* queue[NPROC];
  int start;
  int end;
};

//circular request queue for swapping out requests.
struct ready_queue readyqueue;

struct proc* kpop(){

  acquire(&readyqueue.lock);
  if(readyqueue.start==readyqueue.end){
  	release(&readyqueue.lock);
  	return 0;
  }
  struct proc *p=readyqueue.queue[readyqueue.start];
  (readyqueue.start)++;
  (readyqueue.start)%=NPROC;
  release(&readyqueue.lock);

  return p;
}

int kpush(struct proc *p){

  acquire(&readyqueue.lock);
  if((readyqueue.end+1)%NPROC==readyqueue.start){
  	release(&readyqueue.lock);
    return 0;
  }
  readyqueue.queue[readyqueue.end]=p;
  readyqueue.end++;
  (readyqueue.end)%=NPROC;
  release(&readyqueue.lock);
  
  return 1;
}

//circular request queue for swapping in requests
struct ready_queue readyqueue2;

struct proc* kpop2(){

	acquire(&readyqueue2.lock);
	if(readyqueue2.start==readyqueue2.end){
		release(&readyqueue2.lock);
		return 0;
	}
	struct proc* p=readyqueue2.queue[readyqueue2.start];
	(readyqueue2.start)++;
	(readyqueue2.start)%=NPROC;
	release(&readyqueue2.lock);
	return p;
}

int kpush2(struct proc* p){
	acquire(&readyqueue2.lock);
	if((readyqueue2.end+1)%NPROC==readyqueue2.start){
		release(&readyqueue2.lock);
		return 0;
	}
	readyqueue2.queue[readyqueue2.end]=p;
	(readyqueue2.end)++;
	(readyqueue2.end)%=NPROC;

	release(&readyqueue2.lock);
	return 1;
}

 
void swap_out(){

  acquire(&readyqueue.lock);
  while(readyqueue.start!=readyqueue.end){
    struct proc *p=kpop();

    pde_t* pd = p->pgdir;
    for(int i=0;i<NPDENTRIES;i++){

      //skip page table if accessed. chances are high, not every page table was accessed.
      if(pd[i]&PTE_A)
        continue;
      //else
      pte_t *pgtab = (pte_t*)P2V(PTE_ADDR(pd[i]));
      int j=0;
      while(j<NPTENTRIES){

        //Skip if found
        if((pgtab[j]&PTE_A) || !(pgtab[j]&PTE_P)){
        	j++;
          continue;
        }
        pte_t *pte=(pte_t*)P2V(PTE_ADDR(pgtab[j]));

        //for file name
        int pid=p->pid;
        int virtual = ((1<<22)*i)+((1<<12)*j);

        //file name
        char carray[50];
        to_string(pid,carray);
        int x=strlen(carray);
        carray[x]='_';
        to_string(virtual,carray+x+1);
        safestrcpy(carray+strlen(carray),".swp",5);

        // file management
        int fd=open_process(carray, O_CREATE | O_RDWR);
        if(fd<0){
          cprintf("error creating or opening file: %s\n", carray);
          panic("swap_out_process");
        }

        if(write_process(fd,(char *)pte, PGSIZE) != PGSIZE){
          cprintf("error writing to file: %s\n", carray);
          panic("swap_out_process");
        }
        close_process(fd);

        kfree((char*)pte);
        memset(&pgtab[j],0,sizeof(pgtab[j]));

        //mark this page as being swapped out.
        pgtab[j]=((pgtab[j])^(0x080));

        break;
      }
    }

  }

  release(&readyqueue.lock);
  
  struct proc *p;
  if((p=myproc())==0)
    panic("swap out process");

  swap_out_present=0;
  p->parent = 0;
  p->name[0] = '*';
  p->killed = 0;
  p->state = UNUSED;
  sched();
}

int read_process(int fd, int n, char *p)
{
  struct file *f;
  if(fd < 0 )
  	return -1;
if( fd >= NOFILE )
  	return -1;
if((f=myproc()->ofile[fd]) == 0)
 	 return -1;
  return fileread(f, p, n);

}

void swap_in(){

	acquire(&readyqueue2.lock);

	while(readyqueue2.start!=readyqueue2.end){
		struct proc *p=kpop2();

		int pid=p->pid;
		int virtual=PTE_ADDR(p->addr);

		char carray[50];
	   to_string(pid,carray);
	    int x=strlen(carray);
	    carray[x]='_';
	    to_string(virtual,carray+x+1);
	    safestrcpy(carray+strlen(carray),".swp",5);

	    int fd=open_process(carray,O_RDONLY);
	    if(fd<0){
	    	release(&readyqueue2.lock);
	    	cprintf("could not find page file in memory: %s\n", carray);
	    	panic("swap_in_process");
	    }
	    char *mem=kalloc();
	    read_process(fd,PGSIZE,mem);

	    if(mappages(p->pgdir, (void *)virtual, PGSIZE, V2P(mem), PTE_W|PTE_U)<0){
	    	release(&readyqueue2.lock);
	    	panic("mappages");
	    }
	}


    release(&readyqueue2.lock);
    struct proc *p;
	if((p=myproc())==0)
	  panic("swap_in_process");

	swap_in_present=0;
	p->parent = 0;
	p->name[0] = '*';
	p->killed = 0;
	p->state = UNUSED;
	sched();

}

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&readyqueue.lock, "readyqueue");
  initlock(&sleeping_channel_lock, "sleeping_channel");
  initlock(&readyqueue2.lock, "readyqueue2");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
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
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

void
create_kernel_process(const char *name, void (*entrypoint) ()){
  struct proc *np;
  //struct qnode *qn;
  
  // Allocate process
  if ((np = allocproc()) == 0)
    panic("Failed to allocate kernel process.");
  
  // qn = freenode;
  // freenode = freenode->next;
  // if(freenode != 0)
  //   freenode->prev = 0;
  
  if((np->pgdir = setupkvm()) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    panic("Failed to setup pgdir for kernel process.");
  }
  np->sz = PGSIZE;
  np->parent = initproc; // parent is the first process.
  memset(np->tf, 0, sizeof(*np->tf));
  np->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  np->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  np->tf->es = np->tf->ds;
  np->tf->ss = np->tf->ds;
  np->tf->eflags = FL_IF;
  np->tf->esp = PGSIZE;
  np->tf->eip = 0;  // beginning of initcode.S
  
  // Clear %eax so that fork return 0 in the child
  np->tf->eax = 0;
  /*
  for(int i = 0; i < NOFILE; i++)
    if(proc->ofile[i])                        // Change this. VERY BAD!
      np->ofile[i] = filedup(proc->ofile[i]); // Change this. VERY BAD!*/
  np->cwd = namei("/");
  
  safestrcpy(np->name, name, sizeof(name));
  
  // qn->p = np;
  // lock to force the compiler to emit the np-state write last.
  acquire(&ptable.lock);
  np->context->eip = (uint)entrypoint;
  np->state = RUNNABLE;
  // _queue_add(qn);
  release(&ptable.lock);
}


//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  acquire(&readyqueue.lock);
  readyqueue.start=0;
  readyqueue.end=0;
  release(&readyqueue.lock);

  acquire(&readyqueue2.lock);
  readyqueue2.start=0;
  readyqueue2.end=0;
  release(&readyqueue2.lock);

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0;  // beginning of initcode.S

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
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{

  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
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

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
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
void
exit(void)
{

  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }


  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
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
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
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
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

      //If the swap out process has stopped running, free its stack and name.
      if(p->state==UNUSED ){
      	if(p->name[0]=='*'){
      		 cprintf("A kernel process was killed. PID: %d\n", p->pid);
		        kfree(p->kstack);
		        p->kstack=0;
		        p->name[0]=0;
		        p->pid=0;
      	}

       
      }

      if(p->state != RUNNABLE)
        continue;
    int i=0;
      while(i<NPDENTRIES){
        //If PDE was accessed

        if(((p->pgdir)[i])&PTE_P && ((p->pgdir)[i])&PTE_A){

          pte_t* pgtab = (pte_t*)P2V(PTE_ADDR((p->pgdir)[i]));
          int j=0;

          while(j<NPTENTRIES){
            if(pgtab[j]&PTE_A){
              pgtab[j]^=PTE_A;
            }
            j++;
          }

          ((p->pgdir)[i])^=PTE_A;
        }
        i++;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

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

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
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

  if (first) {
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
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
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
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
