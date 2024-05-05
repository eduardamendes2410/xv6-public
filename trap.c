#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256]; // CADA ELEMENEOT descreve uma entrada na tabela de descritores de interrupção.
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock; // usada para sincronização no contador de ticks do sistema.
uint ticks; //será usada para contar o número de ticks do sistema.

//ticks
//são a unidade básica de medição do tempo decorrido pelo sistema operacional. Cada "tick" representa um pequeno intervalo de tempo, 
//geralmente determinado pelo relógio do sistema.
//em escalonadores os "ticks" são usados para agendar a troca de contexto entre diferentes processos. O escalonador do sistema decide
// quando um processo deve ser interrompido e outro deve ser executado com base em contagens de "ticks".


void
tvinit(void) // inicializa a tabela de vetores de interrupção.
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void) // que inicializa a tabela de descritores de interrupção.
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
//ESSE CODIGO SERVE PRA TRATAR INTERRUPÇÕES (TRAPS) NO SISTEMA
void
trap(struct trapframe *tf) //ESSE TRAPFRAME contém o estado das registradores no momento em que a interrupção ou exceção ocorreu.
{
  if(tf->trapno == T_SYSCALL){ //VERIFICA SE A INTERRUPÇÃO OCORREU DEVIDO A UMA CHAMADA DE SISTEMA
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){ //. Esta parte é responsável por atualizar o contador de ticks (ticks) do sistema, acordar processos que estavam dormindo aguardando ticks e liberar o tickslock.
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      update_process_time();
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
    //Trata interrupções específicas de dispositivos, como o disco IDE, teclado, porta serial, etc.
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7: // Lida com interrupções inesperadas.
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER && 
     myproc()->preemption_time==INTERV)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
