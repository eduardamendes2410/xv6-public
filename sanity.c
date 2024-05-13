#include "types.h"
#include "user.h"

#define CPU_BOUND 0
#define S_CPU_BOUND 1
#define IO_BOUND 2

void cpu_bound() //simula uma carga de trabalho intensiva de CPU
{
    for (int i = 0; i < 100; i++)
    {
        for (volatile int j = 0; j < 1000000; j++) //volatile para evitar que o compilador otimize este loop
            ;
    }
}

void s_cpu_bound() //simula uma carga de trabalho nao tao intensiva de CPU
{
    for (int i = 0; i < 20; i++)
    {
        for (volatile int j = 0; j < 1000000; j++)
            ;
        yield();//depois de rodar um milhao de vezes chamo o yield
    }
}

void io_bound()
{
    for (int i = 0; i < 100; i++)
    {
        sleep(1);
    }
}

// Função para calcular os tempos de CPU
void calculateTimeCPU(int retime, int rutime, int stime, int *cpu_retime, int *cpu_rutime, int *cpu_stime)
{
    *cpu_retime += retime;
    *cpu_rutime += rutime;
    *cpu_stime += stime;
}

// Função para calcular os tempos de S
void calculateTimeS(int retime, int rutime, int stime, int *s_retime, int *s_rutime, int *s_stime)
{
    *s_retime += retime;
    *s_rutime += rutime;
    *s_stime += stime;
}

// Função para calcular os tempos de IO
void calculateTimeIO(int retime, int rutime, int stime, int *io_retime, int *io_rutime, int *io_stime)
{
    *io_retime += retime;
    *io_rutime += rutime;
    *io_stime += stime;
}

// Função para imprimir os resultados médios
void printAverageResults(char *process_type, int retime, int rutime, int stime, int turnaround_time, int n) {
    printf(1, "\n%s:\n", process_type);
    printf(1, "Média ready time: %d\n", retime / n);
    printf(1, "Média running time: %d\n", rutime / n);
    printf(1, "Média sleeping time: %d\n", stime / n);
    printf(1, "Média turnaround time: %d\n", turnaround_time / n);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf(1, "Usage: %s <n>\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);

    for (int i = 0; i < 3 * n; i++)
    {
        int pid = fork();
        if (pid == 0) //FILHO
        {
            int type = getpid() % 3; //Vai obter um valor entre 0 e 2, definindo se o processo vai ser CPU-bound, i/o, s-bound
            if (type == CPU_BOUND)
            {
                change_prio(4);
                cpu_bound();
            }
            else if (type == S_CPU_BOUND)
            {
                change_prio(3);
                s_cpu_bound();
            }
            else if (type == IO_BOUND)
            {
                change_prio(1);
                io_bound();
            }
            else
            {
                break;
            }
            exit();
        }
    }

    int retime;
    int rutime;
    int stime;

    // Variáveis para armazenar os valores acumulados
    int cpu_stime = 0;
    int s_stime = 0;
    int io_stime = 0;

    int cpu_retime = 0;
    int s_retime = 0; 
    int io_retime = 0;

    int cpu_rutime = 0;
    int s_rutime = 0;
    int io_rutime = 0;

    for (int i = 0; i < 3 * n; i++)
    {   
        int pid = wait2(&retime, &rutime, &stime);
        int type = pid % 3;
        if (type == CPU_BOUND)
        {
            printf(1, "CPU-Bound, pid: %d, ready time: %d, running time: %d, sleeping time: %d\n", pid, retime, rutime, stime);
            calculateTimeCPU(retime, rutime, stime, &cpu_retime, &cpu_rutime, &cpu_stime);
        }
        else if (type == S_CPU_BOUND)
        {
            printf(1, "S-Bound, pid: %d, ready time: %d, running time: %d, sleeping time: %d\n", pid, retime, rutime, stime);
            calculateTimeS(retime, rutime, stime, &s_retime, &s_rutime, &s_stime);
        }
        else if (type == IO_BOUND)
        {
            printf(1, "IO-Bound, pid: %d, ready time: %d, running time: %d, sleeping time: %d\n", pid, retime, rutime, stime);
            calculateTimeIO(retime, rutime, stime, &io_retime, &io_rutime, &io_stime);
        }
        else
        {
            break;
        }
    }

    int cpu_turnaround_time = cpu_retime + cpu_rutime + cpu_stime;
    int s_turnaround_time = s_retime + s_rutime + s_stime;
    int io_turnaround_time = io_retime + io_rutime + io_stime;

    printAverageResults("CPU-Bound", cpu_retime, cpu_rutime, cpu_stime, cpu_turnaround_time, n);
    printAverageResults("S-Bound", s_retime, s_rutime, s_stime, s_turnaround_time, n);
    printAverageResults("I/O-Bound", io_retime, io_rutime, io_stime, io_turnaround_time, n);
	exit();
}