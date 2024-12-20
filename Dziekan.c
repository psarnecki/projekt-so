#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // do obsługi pamięci dzielonej

#define SEM_SIZE sizeof(int)

int sem_id, shm_id;
int *shared_mem = NULL;

void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

   key_t key = ftok(".", 'S');
	if (key == -1){
		perror("Blad tworzenia klucza!");
		exit(-1);
	} else
		//printf("key: %d\n", key);

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
	if(shm_id == -1){
		perror("Blad tworzenia segmentu pamieci dzielonej!");
		exit(-1);
	} else
		//printf("shm_id: %d\n", shm_id);

    shared_mem = (int *) shmat(shm_id, NULL, 0);
	if(shared_mem == (int *)(-1)){
		perror("Blad przylaczenia pamieci dzielonej!");
		exit(-1);
	} else {
    	//printf("Pamiec dzielona przypisana poprawnie!\n");
    }

    int kierunek = rand() % 10 + 1; // Kierunki od 1 do 10
    printf("Dziekan ogłasza: Kierunek %d pisze egzamin.\n", kierunek);

    *shared_mem = kierunek; // Zapisanie numeru kierunku do pamięci współdzielonej

    printf("Dziekan zakończył działanie.\n");

    //cleanup();
    return 0;
}

void cleanup()
{
	if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Blad odlaczania pamieci dzielonej");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Blad usuwania segmentu pamieci dzielonej");
    }
}