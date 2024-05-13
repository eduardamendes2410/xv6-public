//TP: TESTES
#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){

    int pid;
    int qtd_process = atoi(argv[1]);
    int prioridades[qtd_process];

    printf(1, "\n\nQuantidade de processos criados será: %d\n", qtd_process);
    printf(1, "Prioridades de cada processo sera: ");

    for(int i = 0; i < qtd_process; i++)
    {
        prioridades[i] = (i % 4) + 1;
        // prioridades[i] = 1; //todos irão executar no FCFS
        // prioridades[i] = 4; //todos irão executar no SJF
        printf(1, "%d, ", prioridades[i]);
    }
    
    printf(1, "\nPrioridade 1 -> FCFS \nPrioridade 2 -> CFS \nPrioridade 3 -> RR \nPrioridade 4 -> SJF \n\n");
    
    uint start_time = uptime();

    for(int i = 0; i < qtd_process; i++) {
		pid = fork();
		if (pid == 0) {
            change_prio(prioridades[i]);
            for (int j = 0; j < 100; j++)
            {   
                for (volatile int z = 0; z < 1000000; z++) //volatile para evitar que o compilador otimize este loop
                    ;
            }
			exit();
		}
		continue;
	}
    //tempo total
    int retime, rutime, stime;
    for(int i = 0; i < qtd_process; i++){
        pid = wait2(&retime, &rutime, &stime);
        printf(1, "pid: %d, ready time: %d, running time: %d, sleeping time: %d\n", pid, retime, rutime, stime);
    }
    uint end_time = uptime();
    printf(1, "\nTempo total de execução: %d ticks\n", end_time - start_time);
    exit();
}