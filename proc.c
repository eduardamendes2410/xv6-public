#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "stddef.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct proc* queue[4][NPROC]; //filas de processos
  int count_process_queue[4]; //conta a quantidade de processos na fila
  int shorter_process_vruntime;
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
  //Implementação TP
  ptable.count_process_queue[0] = 0;
  ptable.count_process_queue[1] = 0;
  ptable.count_process_queue[2] = 0;
  ptable.count_process_queue[3] = 0;
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

  //Implementação TP
  p->ctime = ticks; //Assim que criado é necessario atualizar esse campo, ELE RECEBE O VALOR DE QUANDO FOI CRIADO, QUE É O NUMERO DE TICKS, JA QUE ELE COMEÇA A CONTAR DESDE QUE INICIA O PROGRMA
  p->retime = 0;
  p->rutime = 0;
  p->stime = 0;
  p->aging = 0;
  p->burst_time = 0; //seria o tempo que ele ta executando ate parar em algum momento?
  p->estimate_burst_time_ = 1; //tempo que eu acho que ele vai gastar executando
  p->vruntime = ptable.shorter_process_vruntime; //recebe o valor do menor virtual runtime

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

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->ctime = ticks;
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

  //Implementação TP
  p->priority = 2; //Prioridade padrão
  ptable.queue[1][ptable.count_process_queue[1]] = p; //adiciono na segunda fila o processo que chega
  ptable.count_process_queue[1]++; //Fila padrão que possui prioridade 2
  
  // cprintf("FUNÇÃO USERINIT\n");
  // cprintf("FILA 0: %d\n", ptable.count_process_queue[0]);
  // cprintf("FILA 1: %d\n", ptable.count_process_queue[1]);
  // cprintf("FILA 2: %d\n", ptable.count_process_queue[2]);
  // cprintf("FILA 3: %d\n", ptable.count_process_queue[3]);
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

  //Implementação TP
  np->priority = 2; //Prioridade padrao
  ptable.queue[1][ptable.count_process_queue[1]] = np;
  ptable.count_process_queue[1]++;
  np->state = RUNNABLE;
  
  // cprintf("FUNÇÃO FORK -> pid %d\n", pid);
  // cprintf("FILA 0: %d\n", ptable.count_process_queue[0]);
  // cprintf("FILA 1: %d\n", ptable.count_process_queue[1]);
  // cprintf("FILA 2: %d\n", ptable.count_process_queue[2]);
  // cprintf("FILA 3: %d\n", ptable.count_process_queue[3]);
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
        
         //Implementação TP
        p->ctime = 0;

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
// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       // cprintf("Escalonador: %d\n", p->pid);
//       if(p->state != RUNNABLE)
//         continue;

//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }

//Implementação TP - Escalonadores
void remove_from_queue(int position, int quantity){
  for(int i = position; i < ptable.count_process_queue[quantity]; i++){
        ptable.queue[quantity][i] = ptable.queue[quantity][i + 1]; //tiro o processo que ja foi executado
    }
    ptable.count_process_queue[quantity]--; //diminui a quantidade 
}


int estimate_burst_time(struct proc *p) {
  p->estimate_burst_time_ = ALPHA * p->estimate_burst_time_ + (1 - ALPHA) * p->burst_time;
  return p->estimate_burst_time_;
}


void sjf (struct proc *p, struct cpu *c, int position)
{
  //SJF
  int smallest_process = 0;
  int shorter_burst_time = 10000;
  for (int i = 0; i < ptable.count_process_queue[3]; i++)
  {
    int estimate_burst_time_ = estimate_burst_time(ptable.queue[3][i]);
    if(shorter_burst_time > estimate_burst_time_)
    {
      shorter_burst_time = estimate_burst_time_;
      smallest_process = i;
    }
  }
  p = ptable.queue[3][smallest_process]; //É O PRIMEIRO PROCESO
  if(p != NULL)
  {
    position = smallest_process;
    p->aging = 0;
    p->preemption_time = 0;

    c->proc = p; //define o processo escolhido como o processo que vai executar agora
    switchuvm(p); //muda o contexto de PAGINAÇÃO (???????) PARA O PROCESSO
    remove_from_queue(position, 3);
    p->state = RUNNING; //MUDA O ESTADO PRA EXECUTANDO
    // cprintf("SJF: %d\n",p->pid);  
    swtch(&(c->scheduler), p->context);  //transfere o controle da CPU pra esse processo
    switchkvm(); //VOLTA RPA CPU

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0; //Limpa o processo atual da CPU, indicando que nenhum processo está atualmente em execução.
  }
}


void round_robin(struct proc *p, struct cpu *c, int position)
{
  // cprintf("Escalonador RR pid %d\n",ptable.queue[2][0]->pid);
  p = ptable.queue[2][0]; //É O PRIMEIRO PROCESO
  if (p != NULL)
  {
    position = 0;
    p->aging = 0;
    p->preemption_time = 0;

    c->proc = p; //define o processo escolhido como o processo que vai executar agora
    switchuvm(p); //muda o contexto de PAGINAÇÃO (???????) PARA O PROCESSO
    remove_from_queue(position, 2);    
    p->state = RUNNING; //MUDA O ESTADO PRA EXECUTANDO
    // cprintf("RR: %d\n",p->pid);  
    swtch(&(c->scheduler), p->context);  //transfere o controle da CPU pra esse processo
    switchkvm(); //VOLTA PRA CPU

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0; //Limpa o processo atual da CPU, indicando que nenhum processo está atualmente em execução.
  }
}


void cfs(struct proc *p, struct cpu *c, int position){
      int shorter_vruntime = 10000000;
      int smallest_process = 0;
      for (int i = 0; i < ptable.count_process_queue[1]; i++)
      {
        if(shorter_vruntime >= ptable.queue[1][i]->vruntime)
        {
          shorter_vruntime = ptable.queue[1][i]->vruntime;
          smallest_process = i;
        }
      }
      // cprintf("Escalonador pid %d\n",ptable.queue[1][0]->pid);
      p = ptable.queue[1][smallest_process];
      if(p != NULL)
      {
        ptable.shorter_process_vruntime = shorter_vruntime;
        position = smallest_process;
        p->aging = 0;
        p->preemption_time = 0;

        c->proc = p; //define o processo escolhido como o processo que vai executar agora
        switchuvm(p); //muda o contexto de PAGINAÇÃO (???????) PARA O PROCESSO
        remove_from_queue(position, 1);
        p->state = RUNNING; //MUDA O ESTADO PRA EXECUTANDO
        // cprintf("cfs: %d\n",p->pid);  
        swtch(&(c->scheduler), p->context); //transfere o controle da CPU pra esse processo
        switchkvm(); //VOLTA RPA CPU

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0; //Limpa o processo atual da CPU, indicando que nenhum processo está atualmente em execução.
      }
}


void fcfs(struct proc *p, struct cpu *c, int position)
{
  p = ptable.queue[0][0]; //É O PRIMEIRO PROCESO
  if (p != NULL)
  {
    position = 0;
    p->aging = 0;
    p->preemption_time = 0;

    c->proc = p; //define o processo escolhido como o processo que vai executar agora
    switchuvm(p); //muda o contexto de PAGINAÇÃO (???????) PARA O PROCESSO
    remove_from_queue(position, 0);
    p->state = RUNNING; //MUDA O ESTADO PRA EXECUTANDO
    // cprintf("FCFS: %d\n",p->pid);  
    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
  }
}


void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  int position = 0;
  for(;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    p = NULL;
    //prioridade 4 -> sjf 3-> rr 2 -> cfs 1-> fcfs
    // Escalonador SJF
    if(ptable.count_process_queue[3] > 0)
    {
      sjf(p,c,position);
    }
    // Escalonador RR
    else if(ptable.count_process_queue[2] > 0)
    {
      round_robin(p,c,position);
    }
    // Escalonador CFS
    else if(ptable.count_process_queue[1] > 0)
    {
      cfs(p,c,position);
    }
    // Escalonador FCFS
    else if(ptable.count_process_queue[0] > 0)
    {
      fcfs(p,c,position);
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
// void
// yield(void)
// {
//   acquire(&ptable.lock);  //DOC: yieldlock
//   myproc()->state = RUNNABLE;
//   sched();
//   release(&ptable.lock);
// }


void
yield(void)
{
  struct proc *p = myproc();
  acquire(&ptable.lock);
  int position = p->priority - 1; //para indicar em qual linha da matriz ele está
  ptable.queue[position][ptable.count_process_queue[position]] = p;
  ptable.count_process_queue[position]++;
  p->state = RUNNABLE;
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

  estimate_burst_time(p);
  p->burst_time = 0;

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
    {
      int position = p->priority - 1;
      ptable.queue[position][ptable.count_process_queue[position]] = p;
      ptable.count_process_queue[position]++;
      p->state = RUNNABLE;
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
      if(p->state == SLEEPING)
      {
        int position = p->priority - 1;
        ptable.queue[position][ptable.count_process_queue[position]] = p;
        ptable.count_process_queue[position]++;
        p->state = RUNNABLE;
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

//Funções implementadas para o TP
void update_process_time()
{
  struct proc *process;
  acquire(&ptable.lock);
  process = ptable.proc;
  // cprintf("Função update_process_time\n");
  while (process < &ptable.proc[NPROC])
  {
    if (process->state == RUNNABLE) //processo ta na fila de pronto
    {
      // cprintf("runable\n %d", process->pid);
      process->retime++;
      process->preemption_time=0; 
      process->aging++; //aumento pra mostrar o tempo que ta esperando
    }
    else if (process->state == SLEEPING) //processo ta dormindo
    {
      // cprintf("sleeping\n %d", process->pid);
      process->stime++;
      process->preemption_time=0;
    }
    else if (process->state == RUNNING) //processo ta executando
    {
      process->rutime++;
      // cprintf("running: pid %d, tempo de rutime %d \n ", process->pid, process->rutime);
      process->preemption_time++;
      process->burst_time++;
    }
    process++;
  }
  release(&ptable.lock);
}

// Atribuir ao parâmetro retime o tempo em que o processo esteve no estado READY
// Atribui ao parâmetro rutime o tempo em que o processe esteve no estado RUNNING
// Atribui ao parâmetro stime o tempo em que o processo esteve no estado SLEEPING
int wait2(int* retime, int* rutime, int* stime){
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
        // Implementação em relação aos testes do TP
        pid = p->pid;
        *retime = p->retime;
        *rutime = p->rutime;
        *stime = p->stime;

        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;

        // Implementação em relação aos testes do TP
        p->ctime = 0; //Seto tudo igual a 0 para limpar as informações desse processo
        p->retime = 0; 
        p->rutime = 0;
        p->stime = 0;
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


void print_queue() {
    for (int i = 0; i < 4; i++) {
        cprintf("Fila de prioridade %d:\n", i);
        for (int j = 0; j < NPROC; j++) 
        {
            if (ptable.queue[i][j] != NULL) 
            {
                cprintf("PID: %d\n", ptable.queue[i][j]->pid);
            }
        }
    }
    cprintf("\n");
}


int change_prio(int priority){
    struct proc *process = myproc();
    process->priority = priority;
    yield();
    return 0;
}


// Realiza politica de envelhecimento de processos.
void aging_mechanism(void)
{
  struct proc *p;
  int new_position = -1; //representa a nova fila que o processo em questão irá ocupar
  int initial_position = -1; //representa a fila que o processo em questão estava ocupando
  int j = 0, i = 0, position = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == RUNNABLE)
    {
      if (p->priority == 1 && p->aging > TO1) //14 ticks
      {
          // cprintf("Aging - fila 0 para fila 1 -> pid: %d\n", p->pid);
          initial_position = p->priority - 1;
          p->priority = 2;
          new_position = p->priority - 1;
      }
      else if (p->priority == 2 && p->aging > TO2) //12
      {
        // cprintf("Aging - fila 1 para fila 2 -> pid: %d \n", p->pid);
          initial_position = p->priority - 1;
          p->priority = 3;
          new_position = p->priority - 1;
          
      }
      else if (p->priority == 3 && p->aging > TO3) // 10
      {
          // cprintf("Aging - fila 2 para fila 3 -> pid: %d\n", p->pid);
          initial_position = p->priority - 1;
          p->priority = 4;
          new_position = p->priority - 1;
      }
      else{ //se caso nao entrar em nenhuma condição acima
        continue;
      }
      //PROCESSO DE MUDANÇA DE FILA DO PROCESSO EM QUESTÃO
      while(j < ptable.count_process_queue[initial_position])
      {
        if(p->pid == ptable.queue[initial_position][j]->pid) //procuro a posição do processo em questão na fila de prontos
        {
          position = j;
          break;
        }
        j++;
      }

      i = position;
      while(i < ptable.count_process_queue[initial_position]) //Ajusto a fila, a partir da posição indicada, avançando os processos para ocupar posições anteriores.
      {
        ptable.queue[initial_position][i] = ptable.queue[initial_position][i + 1];
        i++;
      }

      ptable.count_process_queue[initial_position]--; //diminui a quantidade de processos que existe na fila em questão
      
      ptable.queue[new_position][ptable.count_process_queue[new_position]] = p; //coloco o processo na nova fila
      ptable.count_process_queue[new_position]++; //aumento a quantidade de processos da fila
      // print_queue();
      p->aging = 0; //seto o tempo que o processo está esperando pra ser executado como 0 
    }
  }
}