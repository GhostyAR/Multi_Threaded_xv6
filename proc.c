#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

typedef struct resource
{
  int resourceid;
  char name[4];
  int acquired;
  void* startaddr;
  struct spinlock lock;//changed
}Resource;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

typedef struct Node {
    int vertex;
    enum nodetype type;
    struct Node* next;
} Node;
typedef struct Graph{
  struct spinlock lock;
  Node* adjList[MAXTHREAD+NRESOURCE];
  int visited[MAXTHREAD+NRESOURCE];
  int recStack[MAXTHREAD+NRESOURCE];
} Graph;

struct resource* resources[NRESOURCE];//changed
int nextnid= NRESOURCE+1;//changed
struct spinlock nextnid_lock;//changed
char *next_addr;//changed
struct spinlock next_addr_lock;
//################ADD Your Implementation Here######################
int isCyclic(Graph* graph, int v) {
    graph->visited[v] = 1;
    graph->recStack[v] = 1;

    for (Node* neighbor = graph->adjList[v]; neighbor != 0; neighbor = neighbor->next) {
        int adjVertex = neighbor->vertex;
        if (!graph->visited[adjVertex] && isCyclic(graph, adjVertex))
            return 1; 
        else if (graph->recStack[adjVertex])
            return 1; 
    }

    graph->recStack[v] = 0;
    return 0;
}
Graph *g;

Graph* initGraph(char * address){
    Graph *graph = (Graph*)address;
    initlock(&graph->lock, "dlgraph");
    for(int i=0;i<MAXTHREAD+NRESOURCE;i++){
        graph->adjList[i]=0;
        graph->visited[i]=0;
        graph->recStack[i]=0;
    }
    return graph;
}

int addEdge(Graph* graph, int src, int dest, enum edgetype type) {
    acquire(&next_addr_lock);
    Node* newNode = (Node*)next_addr;
    next_addr+=sizeof(Node*);
    release(&next_addr_lock);
    newNode->vertex = dest;
    newNode->next = 0;
    if(type == REQUEST)
        newNode->type = RESOURCE;
    else
        newNode->type = PROCESS;
    if(graph->adjList[src] == 0){
        graph->adjList[src] = newNode;
        return 1;
    }
    Node* front_node = graph->adjList[src];
    while(front_node->next != 0){
      front_node = front_node->next;
    }
    front_node->next = newNode;
    return 1;
}

int removeEdge(Graph* graph, int src, int dest){
    Node* front_node = graph->adjList[src];
    Node* prev_node = 0;
    while(front_node != 0){
      if(front_node->vertex == dest){
        if(prev_node == 0){
          graph->adjList[src] = front_node->next;
        }else{
          prev_node->next = front_node->next;
        }
        return 1;
      } 
      prev_node = front_node;
      front_node = front_node->next;
    }
    return 0;
}

int add_request_edge(Graph* graph, int src, int dest){
    acquire(&graph->lock);
    addEdge(graph, src, dest, REQUEST);
    if(isCyclic(graph, src)){
        cprintf("Deadlock detected.\n");
        release(&graph->lock);
        return 0;
    }
    release(&graph->lock);
    return 0;
}

Resource* get_resource_by_id(int id){
  Resource* resource = 0;
  for(int i = 0; i < NRESOURCE; i++){
      if(resources[i]->resourceid == id){
        resource = resources[i];
        break;
      }
  }
  if(resource == 0)
    return 0;
  return resource;
}

int add_assign_edge(Graph* graph, int src, int dest){
    Resource* resource = 0;
    if(dest >= NRESOURCE){
        return -1;
    }
    resource = resources[dest];
    acquire(&resource->lock);
    resource->acquired = 1;
    acquire(&graph->lock);
    removeEdge(graph, src, dest);
    addEdge(graph, dest, src, ASSIGN);
    if(isCyclic(graph, src)){
        cprintf("Deadlock detected.\n");
    }
    release(&graph->lock);
    return 0;
}
      
void acquireResource(Graph* graph, int src, int dest){
    add_request_edge(graph, src, dest);
    add_assign_edge(graph, src, dest);
}

int releaseResource(Graph* graph, int src, int dest){
    Resource* resource = 0;
    if(dest >= NRESOURCE){
        return -1;
    }
    resource = resources[dest];
    acquire(&graph->lock);
    removeEdge(graph, src, dest);
    release(&graph->lock);
    resource->acquired = 0;
    release(&resource->lock);
    return 0;
}

//##################################################################

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
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
  p->Is_Thread=0;
  p->Thread_Num=0;
  p->tstack=0;
  p->tid=0;
  p->thread_index=0;//changed
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
//################ADD Your Implementation Here######################
  //Resource page handling and creation
  char* first_half=kalloc();
  int limit=PGSIZE/2/NRESOURCE;
  char* second_half=first_half+2048;
  for(int i=0;i<NRESOURCE;i++)
  {
    struct resource* rsrs=(struct resource*)first_half;
    rsrs->resourceid=i;
    initlock(&rsrs->lock, "resourcelock");
    //sprintf(rsrs->name,"r%d",rsrs->resourceid);
    rsrs->name[0]='R';
    rsrs->name[1]='0'+i;
    rsrs->name[2]='\0';
    rsrs->acquired=0;
    rsrs->startaddr=first_half+sizeof(rsrs->resourceid);
    resources[i]=rsrs;
    first_half+=limit;
  }
  g=initGraph(second_half);
  next_addr=kalloc();
//##################################################################
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
  //np->tid=-1;
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
  if(curproc->tid == 0 && curproc->Thread_Num!=0) {
    panic("Parent cannot exit before its children");
  }
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
      if(p->state != RUNNABLE)
        continue;

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
int clone(void (*worker)(void*,void*),void* arg1,void* arg2,void* stack)
{
  //int i, pid;
  struct proc *New_Thread;
  struct proc *curproc = myproc();
  uint sp,HandCrafted_Stack[3];
  // Allocate process.
  if((New_Thread = allocproc()) == 0){
    return -1;
  }
  if(curproc->tid!=0){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      cprintf("Clone called by a thread\n");
      return -1;
  }
  //The new thread parent would be curproc
  New_Thread->pid=curproc->pid;
  New_Thread->sz=curproc->sz;

  //The tid of the thread will be determined by Number of current threads 
  //of a process
  curproc->Thread_Num++;
  New_Thread->tid=curproc->Thread_Num;
  acquire(&nextnid_lock);//changed
  New_Thread->thread_index=nextnid;//changed
  nextnid++;//changed
  release(&nextnid_lock);//changed
  New_Thread->Is_Thread=1;
  //The parent of thread will be the process calling clone
  New_Thread->parent=curproc;

  //Sharing the same virtual address space
  New_Thread->pgdir=curproc->pgdir;
  if(!stack){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      curproc->Thread_Num--;
      New_Thread->tid=0;
      New_Thread->Is_Thread=0;
      cprintf("Child process wasn't allocated a stack\n");    
  }
  //Assuming that child_stack has been allocated by malloc
  New_Thread->tstack=(char*)stack;
  //Thread has the same trapframe as its parent
  *New_Thread->tf=*curproc->tf;

  HandCrafted_Stack[0]=(uint)0xfffeefff;
  HandCrafted_Stack[1]=(uint)arg1;
  HandCrafted_Stack[2]=(uint)arg2;
  
  sp=(uint)New_Thread->tstack;
  sp-=3*4;
  if(copyout(New_Thread->pgdir, sp,HandCrafted_Stack, 3 * sizeof(uint)) == -1){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      curproc->Thread_Num--;
      New_Thread->tid=0;
      New_Thread->Is_Thread=0;      
      return -1;
  }
  New_Thread->tf->esp=sp;
  New_Thread->tf->eip=(uint)worker;
  //Duplicate all the file descriptors for the new thread
  for(uint i = 0; i < NOFILE; i++){
    if(curproc->ofile[i])
      New_Thread->ofile[i] = filedup(curproc->ofile[i]);
  }
  New_Thread->cwd = idup(curproc->cwd);
  safestrcpy(New_Thread->name, curproc->name, sizeof(curproc->name));
  acquire(&ptable.lock);
  New_Thread->state=RUNNABLE;
  release(&ptable.lock);
  //cprintf("process running Clone has  %d threads\n",curproc->Thread_Num);  
  return New_Thread->tid;
}
int join(int Thread_id)
{
  struct proc  *p,*curproc=myproc();
  int Join_Thread_Exit=0,jtid;
  if(Thread_id==0)
     return -1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->tid == Thread_id && p->parent == curproc) {
      Join_Thread_Exit=1; 
      break;
    }
  }
  if(!Join_Thread_Exit || curproc->killed){
    //cprintf("Herere");
    return -1;
  }  
  acquire(&ptable.lock);
  for(;;){
    // thread is killed by some other thread in group
    //cprintf("I am waiting\n");
    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    if(p->state == ZOMBIE){
      // Found the thread 
      curproc->Thread_Num--;
      jtid = p->tid;
      kfree(p->kstack);
      p->kstack = 0;
      p->pgdir = 0;
      p->pid = 0;
      p->tid = 0;
      p->tstack = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
      release(&ptable.lock);
      //cprintf("Parent has  %d threads\n",curproc->Thread_Num);
      return jtid;
    } 

    sleep(curproc, &ptable.lock);  
  }     
  //curproc->Thread_Num--;
  return 0;
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

int requestresource(int Resource_ID)
{
    struct proc *curproc = myproc();
    cprintf("thread %d is requeseting %d\n", curproc->thread_index, Resource_ID);
    acquireResource(g, curproc->thread_index, Resource_ID);
    return -1;
}
int releaseresource(int Resource_ID)
{
    struct proc *curproc = myproc();
    cprintf("thread %d is releasing %d\n", curproc->thread_index, Resource_ID);
    releaseResource(g, curproc->thread_index, Resource_ID);
    return -1;
}
int writeresource(int Resource_ID,void* buffer,int offset, int size)
{
//################ADD Your Implementation Here######################

//##################################################################
  return -1;
}
int readresource(int Resource_ID,int offset, int size,void* buffer)
{
//################ADD Your Implementation Here######################

//##################################################################
  return -1;
}