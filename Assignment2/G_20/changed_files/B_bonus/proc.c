#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "processInfo.h"
int time_quantum=25;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
struct RQ{
  struct spinlock lock;
  struct proc* proc[NPROC+1];
  int length;
} ;
struct RQ RQ1;
struct RQ RQ2;
int empty(struct RQ *rq ){
	acquire(&(rq->lock));
	if(rq->length==0){
		release(&(rq->lock));
		return 1;
	}
	else{
		release(&(rq->lock));
		return 0;
	}
}
int full(struct RQ *rq){
	acquire(&(rq->lock));
	if(rq->length==NPROC){
		release(&(rq->lock));
		return 1;
	}
	else{
		release(&(rq->lock));
		return 0;
	}
}
void insert(struct proc *p,struct RQ *rq){
	if(full(rq))
		return;
	acquire(&(rq->lock));
	rq->length+=1;
	rq->proc[rq->length] = p;
	int j;
	for( j = rq->length-1;(j>=1)&&((rq->proc[j]->burst_time)>(p->burst_time));j--){
		rq->proc[j+1] = rq->proc[j];
	}
	rq->proc[j+1] = p;
	release(&(rq->lock));
	
}
void sort(struct RQ *rq){
	acquire(&(rq->lock));
	 for(int i=1;i<rq->length;i++)
	{
	    for(int j=1;j<=rq->length-i;j++)
	    {
		if(rq->proc[j]->burst_time>rq->proc[j+1]->burst_time)
		{
		    struct proc* temp = rq->proc[j];
		    rq->proc[j]=rq->proc[j+1];
		    rq->proc[j+1]=temp;
		}
	    }
	}
	release(&(rq->lock));
}
struct proc *min_process(struct RQ *rq){
    if(empty(rq))
        return 0;
    acquire(&(rq->lock));
    struct proc* minimum_process = rq->proc[1];
    if(rq->length==1)
	rq->length = 0;
    else{
	for(int j=2;j<=rq->length;j++){
		rq->proc[j-1] = rq->proc[j];
	}
	rq->length-=1;
   }
   release(&(rq->lock));
   
    return minimum_process;
}
static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&RQ1.lock,"RQ1");
  initlock(&RQ2.lock,"RQ2");
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
  p->contextCount=0;//setting default to 0
  p->burst_time=0;//setting a default burst_time to 0 so that their completion is never hindered.
  p->run_time = 0;
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

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  acquire(&RQ1.lock);
  RQ1.length = 0;
  release(&RQ1.lock);
  acquire(&RQ2.lock);
  RQ2.length = 0;
  release(&RQ2.lock);
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
  insert(p,&RQ1);

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
  //rq->push(np), qtime.push(np->burst_time), RQ qtime , sort.
 // np->burst_time=0;//setting burst time of child process to 0.
  insert(np,&RQ1);
 
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
    acquire(&ptable.lock);
    
  // ************SJF*************************************//
       //int minBurst=1000;
    /*for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
    	if(p->state!=RUNNABLE)
    		continue;
    	if(p->burst_time<minBurst)
    	   minBurst=p->burst_time;
    	
    }
    // Loop over process table looking for process to run.
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if(p->burst_time==minBurst)
      {
       //  cprintf("process running is = %d\n",p->pid);
      	 c->proc = p;
         switchuvm(p);
         p->state = RUNNING;
         p->contextCount++;//keeping count of context
         swtch(&(c->scheduler), p->context);
         switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      }
     
    }
    release(&ptable.lock);*/


//****************Round Robin+SJF*************************//
	if(empty(&RQ1)){
		while(!empty(&RQ2)){
			p = min_process(&RQ2);
			insert(p,&RQ1);
	      	}
	}
		if(empty(&RQ1)){
			release(&ptable.lock);
			continue;
		}
		p = min_process(&RQ1);
		if(p->state!=RUNNABLE){
			release(&ptable.lock);
			continue;
		}
	  	c->proc = p;
	  	switchuvm(p);
	  	p->state = RUNNING;
	  	(p->contextCount)++;
	  	swtch(&(c->scheduler), p->context);
	  	switchkvm();
	  	c->proc = 0;
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
  insert(myproc(),&RQ1);
	
  sched();
  release(&ptable.lock);
}
void 
yield_helper(void){
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  insert(myproc(),&RQ2);
  
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
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      insert(p,&RQ1);
    }
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
      if(p->state == SLEEPING){
	p->state = RUNNABLE;
	insert(p,&RQ1);
      }
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

int
getNumProc(void)
{
	struct proc *p;
	int counter=0;
	acquire(&ptable.lock);
	
	for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state!=UNUSED)
			counter++;
	}
	release(&ptable.lock);
	return counter;
}

int
getMaxPid(void)
{
	struct proc *p;
	int maxId=-1;
	 acquire(&ptable.lock);
	 for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state!=UNUSED)
			{
				if(maxId<p->pid)
					maxId=p->pid;
			}
	}
	release(&ptable.lock);
	return maxId;
	 
}


int
getProcInfo(int gpid,struct processInfo* pinfo)
{
	struct proc *p;
	acquire(&ptable.lock);
	int found=0;
	for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid==gpid){
			struct proc *tempParent;
			tempParent=p->parent;
			pinfo->ppid=tempParent->pid;
			pinfo->psize=(int)p->sz;
			pinfo->numberContextSwitches=p->contextCount;
			found=1;
		}
			
	}
	release(&ptable.lock);
	if(found==0)
		return -1;
	//printf("The ID of the parent process is = %d",pinfo->ppid);
	//printf("The size of the process in bytes is = %d", pinfo->psize);
	//printf("The number of context switches for given process = %d",pinfo->numberContextSwitches);
	return 0;
}


int setBurstTime(int n)
{
	if(n<time_quantum)
		time_quantum=n;
	struct proc *p;
	
	p=myproc();
	p->burst_time=n;
		
	
	yield();
		return 1;
		
	

}
int getBurstTime(void)
{
	struct proc *p;
	acquire(&ptable.lock);
	p=myproc();
	//if(p==0)
	//	return -1;
	
		int ans=p->burst_time;
		release(&ptable.lock);
		return ans;
		
	/*int found=0;
	for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state==RUNNING){
			
			found=1; break;
		}
			
	}
	release(&ptable.lock);
	if(found==0)
		return -1;
	else
		return p->burst_time;	
		*/

}
int
getSelfInfo()
{
	
	return myproc()->pid;
}











