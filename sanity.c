#include "types.h"
#include "user.h"

#define CPU_BOUND 0
#define S_CPU_BOUND 1
#define IO_BOUND 2

void cpu_bound()
{
    for (int i = 0; i < 100; i++)
    {
        for (volatile int j = 0; j < 1000000; j++)
            ;
    }
}

void s_cpu_bound()
{
    for (int i = 0; i < 20; i++)
    {
        for (volatile int j = 0; j < 1000000; j++)
            ;
        yield();
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
    printf(1, "Average ready time: %d\n", retime / n);
    printf(1, "Average running time: %d\n", rutime / n);
    printf(1, "Average sleeping time: %d\n", stime / n);
    printf(1, "Average turnaround time: %d\n", turnaround_time / n);
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
        if (pid == 0)
        {
            int type = getpid() % 3;
            if (type == CPU_BOUND)
            {
                cpu_bound();
            }
            else if (type == S_CPU_BOUND)
            {
                s_cpu_bound();
            }
            else if (type == IO_BOUND)
            {
                io_bound();
            }
            else
            {
                break;
            }
            exit();
        }
    }

    int retime, rutime, stime;
    // Variáveis para armazenar os valores acumulados
    int cpu_stime = 0, s_stime = 0, io_stime = 0;
    int cpu_retime = 0, s_retime = 0, io_retime = 0;
    int cpu_rutime = 0, s_rutime = 0, io_rutime = 0;
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