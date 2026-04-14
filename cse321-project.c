#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

enum states {
    RUNNING,
    BLOCKED,
    ZOMBIE,
    REAPED
};

struct PCB {
    int pid;
    int ppid;
    enum states state;
    int exit_status;
    int child_num;
    int child_process[100];
};

pthread_mutex_t lock; 
sem_t sem;
struct PCB pcbTable[64];

void initialize_pcb() {
    for (int i = 0; i < 64; i++){
        pcbTable[i].pid = -2;
        pcbTable[i].ppid = -2;
        pcbTable[i].state = REAPED;
        pcbTable[i].exit_status = -42;
        pcbTable[i].child_num = 0;
        for (int j = 0; j < 100; j++) {
            pcbTable[i].child_process[j] = -2;
        }
    }
}

int pm_fork(int parent_pid){
    pthread_mutex_lock(&lock);
    struct PCB p1;
    p1.ppid = parent_pid;
    p1.state = RUNNING;
    p1.exit_status = -42;
    p1.child_num = 0;
    for (int j = 0; j < 100; j++) {
        p1.child_process[j] = -2;
    }
    
    for (int i = 0; i < 64; i++){
        if (pcbTable[i].pid == -2){
            p1.pid = i+1;
            pcbTable[i] = p1;
            break;
        } 
    }
    for (int i = 0; i < 64; i++){
        if (pcbTable[i].pid == parent_pid){
            pcbTable[i].child_num += 1;
            pcbTable[i].child_process[pcbTable[i].child_num - 1] = p1.pid;    
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    sem_post(&sem);
    return p1.pid;
}

int pm_exit(int pid, int exitStatus){
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 64; i++){
        if (pcbTable[i].pid == pid){
            pcbTable[i].state = ZOMBIE;
            pcbTable[i].exit_status = exitStatus;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    sem_post(&sem);
    return 0;
}

void *thr(void *child_pid){
    int pid = *(int *)child_pid;
    while (1){
        pthread_mutex_lock(&lock);
        if (pcbTable[pid-1].state == ZOMBIE){
            printf("exit status: %d\n", pcbTable[pid-1].exit_status);
            pcbTable[pid-1].pid = -2;
            pcbTable[pid-1].state = REAPED;
            pcbTable[pid-1].ppid = -2;
            pcbTable[pid-1].child_num = 0;
            for (int i = 0; i < 100; i++) {
                pcbTable[pid-1].child_process[i] = -2;
            }
            pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);
        usleep(100);
    }
    pthread_exit(NULL);
}

void *thr2(void *arg) {
    int parent_pid = *(int *)arg;
    int found_child_pid = -1;
    int exit_status = -1;

    while (1) {
        pthread_mutex_lock(&lock); 
        
      
        for (int i = 0; i < 64; i++) { 
            if (pcbTable[i].ppid == parent_pid && pcbTable[i].state == ZOMBIE) { 
                found_child_pid = pcbTable[i].pid;
                exit_status = pcbTable[i].exit_status; 
                
                
                pcbTable[i].state = REAPED;
                pcbTable[i].pid = -2; 
                break; 
            }
        }

        if (found_child_pid != -1) {
      
            for (int j = 0; j < 64; j++) {
                if (pcbTable[j].pid == parent_pid) {
                    pcbTable[j].state = RUNNING;
                    
                    break;
                }
            }
            
            printf("Process %d reaped child %d with status: %d\n", parent_pid, found_child_pid, exit_status);
            pthread_mutex_unlock(&lock);
            sem_post(&sem); 
            break;
        }

        pthread_mutex_unlock(&lock);
        usleep(10000); 
    }
    return NULL;
}

int pm_wait(int parent_pid, int child_pid){
    pthread_mutex_lock(&lock);
    if (pcbTable[parent_pid - 1].child_num == 0){
        pthread_mutex_unlock(&lock);
        return -1;
    }

    pthread_t t1;
    for (int i = 0; i < 100; i++){
        if (child_pid == -1 && pcbTable[parent_pid - 1].child_num > 0){
            pcbTable[parent_pid - 1].state = BLOCKED;
            pthread_create(&t1, NULL, thr2, &parent_pid);
            break;
        }
        if (child_pid == pcbTable[parent_pid - 1].child_process[i]){
            pcbTable[parent_pid - 1].state = BLOCKED;
            pthread_create(&t1, NULL, thr, &child_pid);
            break;
        }
    }
    
    pthread_mutex_unlock(&lock);
    pthread_join(t1, NULL);
    pthread_mutex_lock(&lock);
    pcbTable[parent_pid - 1].child_num -= 1;
    for (int i = 0; i < 100; i++){
        if (pcbTable[parent_pid - 1].child_process[i] == child_pid){
            pcbTable[parent_pid - 1].child_process[i] = -2;
        }
    }
    pcbTable[parent_pid - 1].state = RUNNING;
    pthread_mutex_unlock(&lock);
    sem_post(&sem);    
    return 0;
}

int pm_kill(int pid){
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 64; i++){
        if (pcbTable[i].pid == pid){
            int temp = pcbTable[i].pid;
            pcbTable[i].pid = -2;
            pcbTable[i].state = REAPED;
            for (int j = 0; j < 64; j++){
                if (pcbTable[j].pid == pcbTable[i].ppid){
                    pcbTable[j].child_num -= 1;
                    for (int k = 0; k < 100; k++){
                        if (pcbTable[j].child_process[k] == temp){
                            pcbTable[j].child_process[k] = -2;
                            break;
                        }
                    }
                    break;    
                }
            }
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    sem_post(&sem);
    return 0;
}

int pm_ps() {
    FILE *fp = fopen("snapshots.txt", "a");
    if (fp == NULL) return -1;

    printf("PID     PPID     STATE     EXIT_STATUS\n");
    printf("_________________________________________\n");
    fprintf(fp, "PID     PPID     STATE     EXIT_STATUS\n");
    fprintf(fp, "_________________________________________\n");

    for (int i = 0; i < 64; i++) {
        if (pcbTable[i].state != REAPED) {
            printf("%d       %d        ", pcbTable[i].pid, pcbTable[i].ppid);
            fprintf(fp, "%d       %d        ", pcbTable[i].pid, pcbTable[i].ppid);
            
            if (pcbTable[i].state == RUNNING) {
                printf("RUNNING       ");
                fprintf(fp, "RUNNING       ");
            }
            if (pcbTable[i].state == BLOCKED) {
                printf("BLOCKED       ");
                fprintf(fp, "BLOCKED       ");
            }
            if (pcbTable[i].state == ZOMBIE) {
                printf("ZOMBIE        ");
                fprintf(fp, "ZOMBIE        ");
            }

            if (pcbTable[i].exit_status == -42) {
                printf("-\n");
                fprintf(fp, "-\n");
            } else {
                printf("%d\n", pcbTable[i].exit_status);
                fprintf(fp, "%d\n", pcbTable[i].exit_status);
            }
        }
    }
    
    printf("\n");
    fprintf(fp, "\n");
    fclose(fp);
    return 0;
}

void *worker(void *arg){
    char *filename = (char *)arg;
    FILE *fp = fopen(filename, "r");

    if (!fp) {
        perror("Failed to open script file");
        return NULL;
    }

    char line[100];
    char command[10];
    int arg1, arg2;

    while (fgets(line, sizeof(line), fp)) {

        char n = ((char *)arg)[6];
        int num_args = sscanf(line, "%s %d %d", command, &arg1, &arg2);
        if (strcmp(command, "fork") == 0){
            pm_fork(arg1);
            printf("Thread %c calls pm_fork %d\n",n, arg1);
            
            
        }
        if (strcmp(command, "exit") == 0){           
            pm_exit(arg1, arg2);
            printf("Thread %c calls pm_exit %d %d\n", n, arg1, arg2);
            
            
        }
        if (strcmp(command, "wait") == 0){
            pm_wait(arg1, arg2);
            printf("Thread %c calls pm_wait %d %d\n", n, arg1, arg2);
        
        }
        if (strcmp(command, "kill") == 0){
            pm_kill(arg1);
            printf("Thread %c calls pm_kill %d\n",n, arg1);
            
        }
        if (strcmp(command, "sleep") == 0){
            usleep(arg1*1000);
        }
    }
    fclose(fp);
    pthread_exit(NULL);
}

void *monitor(){
    //FILE *mon = fopen("snapshot.txt", "w");
    while (1){
        sem_wait(&sem);
        pthread_mutex_lock(&lock);
        pm_ps();
        pthread_mutex_unlock(&lock);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]){
    fopen ("snapshots.txt", "w");
    initialize_pcb();
    struct PCB p;
    p.pid = 1;
    p.ppid = 0;
    p.state = RUNNING;
    p.exit_status = -42;
    p.child_num = 0;
    for (int i = 0; i < 100; i++) {
        p.child_process[i] = -2;
    }
    pcbTable[0] = p;
    printf("Initial Process Table\n");
    pm_ps();
    pthread_t t[argc-1];
    pthread_t t1;
    
    sem_init(&sem, 0, 0);
    pthread_mutex_init(&lock, NULL);
    pthread_create(&t1, NULL, monitor, NULL);
    
    for (int i = 1; i < argc; i++){
        pthread_create(&t[i-1], NULL, worker, argv[i]);
    }
    for (int i = 1; i < argc; i++){
        pthread_join(t[i-1], NULL);
    }
    sleep(1);
    printf("\n");
    return 0;
}
