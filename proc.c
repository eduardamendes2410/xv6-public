#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


struct { //1 o spinlock ta bloqueado e 0 nao
  struct spinlock lock; //Spinlock é um mecanismo de sincronização para garantir que apenas um thread (ou processo) tenha acesso exclusivo a um recurso compartilhado por vez
  struct proc proc[NPROC]; //NPROC define o número máximo de processos que o sistema pode lidar.
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan); // É uma função de callback usada para acordar um processo que está esperando em um canal específico.

void
pinit(void)
{
  initlock(&ptable.lock, "ptable"); //inicializa o spinlock ptable.lock para garantir que a tabela de processos seja acessada de forma segura por várias partes do sistema. 
}

// Must be called with interrupts disabled = Deve ser chamado com interrupções desabilitadas
int
cpuid() { //É uma função que retorna o ID da CPU atual.
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.

// Deve ser chamado com interrupções desabilitadas para evitar que o chamador seja
// reprogramado entre a leitura do lapicid e a execução do loop.

struct cpu*
//É uma função que retorna um ponteiro para a estrutura cpu representando a CPU atual.
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
//Desabilita interrupções para que não sejamos reprogramados
// enquanto lê proc da estrutura da CPU
struct proc*
myproc(void) { //: É uma função que retorna um ponteiro para o processo 
//atualmente em execução na CPU. Ela desativa temporariamente as interrupções 
//(pushcli() e popcli()) para evitar que o processo seja alterado enquanto o processo é acessado.
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

//QUEBRA DE PÁGINA: 32
// Procure na tabela de processos um proc NÃO UTILIZADO.
// Se encontrado, muda o estado para EMBRIÃO e inicializa
// estado necessário para rodar no kernel.
// Caso contrário, retorne 0.
//ENTAO VAI PROCURAR ALGUM PROCESSO PRA COMEÇAR A RODAR, SE ACHAR PEGA ELE SE NAO RETORNA 0
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock); //BLOQUEIA A TABELA DE PROCESSOS

//PASSA PELOS PROCESSOS E PROCURA UM QUE TEM O ESTADO DE UNUSED
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found; // é uma instrução de controle de fluxo em C que transfere o controle do programa para uma etiqueta denominada found.

  release(&ptable.lock); //se nao achar ele so vai destravar o lock e retorna 0
  return 0; //indica que nenhum processo foi alocado

found: //se achar vem pra ca
  p->state = EMBRYO; //muda o estado para embriao
  p->pid = nextpid++; //esse processo recebe um ID do processo
  //TESTE
  p->ctime = ticks; //Assim que criado é necessario atualizar esse campo
  p->retime = 0;
  p->rutime = 0;
  p->stime = 0;
  p->priority = 2; //Prioridade padrão
  // p->lastruntime=0;
  release(&ptable.lock); 

  // Allocate kernel stack. -> Alocação da Pilha do Kernel
  //TENTO ALOCAR UM ESPAÇO PARA A PILHA DO KERNEL DESSE PROCESSO, ESSA PILHA
  //É ONDE EU ARMAZENO AS VARIAVEIS LOCAIS DAS FUNÇÕES, OS PARAMETROS DE CHAMADAS DE FUNÇÃO
  //E OUTRAS INFO RELACIONADAS A EXECUÇÃO DO CODIGO NO KERNEL
  if((p->kstack = kalloc()) == 0){ //SE DER ERRADO SIGNIFICA QUE NAO TEM MEMORIA SUFICIENTE PRA ALOCAR A PILHA DO KERNEL DO PROCESSO
    p->state = UNUSED;
    return 0;
  }
  //p->kstack ponteiro pra pilha do kernel que acabou de ser alocado
  //ele vai ter o endereço de memoria alocado para a pilha do kernel
  //KSTACKSIZE representa o tamanho da pilha do kernel
  sp = p->kstack + KSTACKSIZE; //Isso permite que o sistema gerencie o espaço disponível na pilha do kernel

  // Leave room for trap frame.
  // esse trapframe é usado para armazenar o estado do processador no momento em que ocorre uma interrupção, exceção ou trap
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;
//ESSE CODIGO NAO ENTENDI BEM MAS É PRA LIDAR COM INTERRUPÇÃO
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
// INICIALIZA O PRIMIERO PROCESSO DE USUARIO NO SISTEMA
void
userinit(void)
{
  struct proc *p; //CRIA UM PROCESSO
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc(); // alocar e inicializar uma nova estrutura de processo.
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0) //inicialização do espaço de endereçamento do processo
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  //isso aqui vai setar as info do processo 
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

// A função growproc é responsável por aumentar a memória do processo atual em n bytes. 
// Ela obtém o processo atual, calcula o novo tamanho da memória (sz) e chama allocuvm 
// ou deallocuvm, dependendo se n é positivo ou negativo. Se a alocação ou desalocação 
// for bem-sucedida, atualiza o tamanho do processo e altera o contexto de paginação com 
// switchuvm, retornando 0 em caso de sucesso ou -1 em caso de falha.
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
// a função fork cria um novo processo copiando o processo atual como o processo pai.
// Ele aloca uma nova estrutura de processo, copia o estado do processo pai com copyuvm, 
// configura as propriedades do novo processo e define o estado como "RUNNABLE". Após a conclusão, 
// retorna o PID do novo processo filho ou -1 em caso de falha.

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
  np->priority = 2; //Prioridade padrao
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

  np->state = RUNNABLE; //SETA ELE PRA PRONTO PRA EXECUTAR

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.

// Sai do processo atual. Não retorna.
// Um processo encerrado permanece no estado zumbi
// até que seu pai chame wait() para descobrir que ele saiu.
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
// Aguarde a saída de um processo filho e retorne seu pid.
// Retorna -1 se este processo não tiver filhos.
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
        p->ctime = 0; //Teste
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


//TESTES
int wait2(int* retime, int* rutime, int* stime){
//Atribuir ao parâmetro retime o tempo em que o processo esteve no estado READY
// atribui ao parâmetro rutime o tempo em que o processe esteve no estado RUNNING
//stime o tempo em que o processo esteve no estado SLEEPING
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
        *retime = p->retime;
        *rutime = p->rutime;
        *stime = p->stime;
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        //Seto tudo igual a 0 para limpar as informações desse processo
        p->ctime = 0; //Teste
        p->retime = 0; 
        p->rutime = 0;
        p->stime = 0;
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

//QUEBRA DE PÁGINA: 42
// Agendador de processos por CPU.
// Cada CPU chama agendador() após se configurar.
// O agendador nunca retorna. Ele faz um loop, fazendo:
// - escolha um processo para executar
// - switch para iniciar a execução desse processo
// - eventualmente esse processo transfere o controle
// via switch de volta ao agendador.
void
scheduler(void)
{
  struct proc *p; //processo atual
  struct cpu *c = mycpu(); //cpu atual
  c->proc = 0; //indica que na CPU nao tem nenhum processo em execução
  
  for(;;){ //esse loop vai continuar indefinidamente pra garantir uque o sistema sempre esteja pronto pra executar um processo
    // Enable interrupts on this processor.
    sti(); //habilita interrupções neste processador.

    // Loop over process table looking for process to run.
    acquire(&ptable.lock); //bloqueia a gtabela de processos
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ //busca processos que estao prontos pra ser exevutados
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      p->preemption_time=0;
      c->proc = p; //define o processo escolhido como o processo que vai executar agora
      switchuvm(p); //muda o contexto de PAGINAÇÃO (QUE PORRA É ESSE) PARA O PROCESSO
      p->state = RUNNING; //MUDA O ESTADO PRA EXECUTANDO

      swtch(&(c->scheduler), p->context);  //transfere o controle da CPU pra esse processo
      switchkvm(); //VOLTA RPA CPU

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0; //Limpa o processo atual da CPU, indicando que nenhum processo está atualmente em execução.
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

//ESSA FUNÇÃO TEM OBJETIVO DE transferir o controle de execução do processo atual para o escalonador da CPU.
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
// Desistir da CPU por uma rodada de agendamento.
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

//é chamada quando um processo filho criado pela chamada do sistema fork está pronto para ser executado pela primeira vez pelo escalonador (scheduler).
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  //se for a primeira vez ele inciializa algumas coisas 
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
  
  //SE O PROCESSO NAO EXISTE OU SE ELE 
  if(p == 0)
    panic("sleep"); // interrupção imediata da execução do programa

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
  //ESSE CHAN É O CANAL ONDE O PROCESSO VAI DORMIR, PQ AI QUANDO QUISER ACORDAR ELE 
  //O WAKEUP VAI SER CHAMADO NESSE CANAL AI
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

// é responsável por imprimir uma lista de processos no console. Esta função é usada 
//principalmente para fins de depuração e é executada quando o usuário digita ^P (Ctrl + P) no console.

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
  struct proc *p; //CRIA UMA ESTRUTURA DE PROCESSO
  char *state; //ARMAZENA O ESTADO DO PROCESSO
  uint pc[10];
  //Um loop que itera sobre todos os processos na tabela de processos
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED) //Se o estado do processo for UNUSED, o loop continua para o próximo processo.
      continue;
    //verifica se o estado do processo está dentro dos limites da matriz states, verifica se o estado do processo é valido
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){ // imprimir informações adicionais sobre a pilha do processo quando está em estado de SLEEPING.
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void update_process_time()
{
  struct proc *process;
  acquire(&ptable.lock);
  process = ptable.proc;
  while (process < &ptable.proc[NPROC])
  {
    if (process->state == RUNNABLE)
    {
      // p->lastruntime++;
      process->retime++;
      process->preemption_time=0;
    }
    else if (process->state == SLEEPING)
    {
      // p->lastruntime=0;
      process->stime++;
      process->preemption_time=0;
    }
    else if (process->state == RUNNING)
    {
      // p->lastruntime=0;
      process->rutime++;
      process->preemption_time++;
    }
    process++;
  }
  release(&ptable.lock);
}

int change_prio(int pid, int priority){
    struct proc *process;
    acquire(&ptable.lock);
    process = ptable.proc;
    while (process < &ptable.proc[NPROC])
    {
      if (process->pid == pid)
      {
        process->priority = priority;
        break;
      }
      process++;
    }
    release(&ptable.lock);
    return pid;
}