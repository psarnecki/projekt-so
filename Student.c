#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // do obsługi pamięci dzielonej

#define MIN_STUDENTS 80
#define MAX_STUDENTS 160
#define SEM_SIZE sizeof(int)

int sem_id, shm_id;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Argument wątku przekazywany do funkcji wątku przy rozpoczęciu jego wykonywania (struktura)
typedef struct {
    int kierunek;  // Numer kierunku
    int liczba_studentow; // Liczba studentów
} Kierunek;

// Funkcja symulująca przybycie studentów dla danego kierunku (funkcja wątku)
void* symuluj_przybycie(void* arg) {
    Kierunek* dane = (Kierunek*)arg;

    for (int i = 1; i <= dane->liczba_studentow; i++) {
        int czas_przybycia = rand() % 2 + 1; // Losowy czas przybycia (1-2 sekundy)
        sleep(czas_przybycia); // Symulacja opóźnienia
        printf("Student %d z kierunku %d przybył do kolejki.\n", i, dane->kierunek);
    }

    // Oczekiwanie na informację od dziekana
    while (1) {
        pthread_mutex_lock(&mutex); // Zablokowanie mutexu, aby uzyskać wyłączny dostęp do zmiennej
        if (ogloszony_kierunek != 0) {
            pthread_mutex_unlock(&mutex); // Odblokowanie mutexu i wyjście z pętli, jeśli dziekan ogłosił kierunek
            break;
        }
        pthread_mutex_unlock(&mutex); // Odblokowanie mutexu, jeśli dziekan nie ogłosił kierunku aby inne wątki kierunków mogły kontuunować działanie
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem ogłoszenia w pętli 
    }

    // Sprawdzenie ogłoszonego kierunku
    if (dane->kierunek == ogloszony_kierunek) {
        printf("Studenci z kierunku %d wchodzą na egzamin.\n", dane->kierunek);
    } else {
        printf("Studenci z kierunku %d wracają do domu.\n", dane->kierunek);
    }

    pthread_exit(NULL);
}

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
    
    int liczba_kierunkow = rand() % 11 + 5; // Losowanie liczby kierunków z zakresu 5-15
    printf("Liczba kierunków: %d\n", liczba_kierunkow);

    Kierunek kierunki[liczba_kierunkow]; // Tworzenie tablicy struktur kierunków
    pthread_t watki[liczba_kierunkow]; // Tworzenie tablicy wątków o ilości kierunków

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        kierunki[i].kierunek = i + 1;
        kierunki[i].liczba_studentow = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, kierunki[i].liczba_studentow);
    }

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie wątków dla każdego kierunku
    for (int i = 0; i < liczba_kierunkow; i++) {
        if (pthread_create(&watki[i], NULL, symuluj_przybycie, (void*)&kierunki[i]) != 0) {
            perror("Błąd tworzenia wątku");
            exit(EXIT_FAILURE);
        }
    }

    ogloszony_kierunek = *shared_mem;
    printf("\nOdebrany ogłoszony kierunek: %d\n", ogloszony_kierunek);

    cleanup();

    // Czekanie na zakończenie wszystkich wątków
    for (int i = 0; i < liczba_kierunkow; i++) {
        pthread_join(watki[i], NULL);
    }

    printf("\nSymulacja zakończona.\n");
    
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