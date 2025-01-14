#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // do obsługi pamięci dzielonej
#include <sys/sem.h> // do obsługi semaforów
#include <sys/msg.h> // Do obsługi kolejki komunikatów
#include <signal.h> // do obsługi sygnałów
#include <errno.h>

#define SEM_SIZE sizeof(int)
#define SEM_DZIEKAN 1
#define SEM_STUDENT 0
#define STUDENT_TO_DEAN 3

int sem_id, shm_id, msgid, msg_dziekan, shm_komisja_id, ile_studentow = 0;
int *shared_mem = NULL;
int num_students = 0;  // Liczba aktywnych studentów
int max_students = 80;  // Początkowy rozmiar tablicy
int student_pids[160];  // Wskaźnik na dynamiczną tablicę przechowującą PID-y studentów

struct message {
    long msg_type;  // Typ komunikatu
    int pid;        // PID studenta
    float ocena_A;      // Ocena (dla odpowiedzi)
    float ocena_B;
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
} Student_info;

Student_info *shared_info;

void handle_signal(int sig) {
    for (int i = 0; i < num_students; i++) {
        if (kill(student_pids[i], 0) == 0) {  // Sprawdzenie, czy proces istnieje
            kill(student_pids[i], SIGUSR1);  // Wysyłanie sygnału do studenta
        }
    }
}

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
    key_t key_msg = ftok(".", 'M');
    key_t key_dziekan = ftok(".", 'D');
    key_t key_komisja_A = ftok(".", 'A');
    if (key == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
    shm_komisja_id = shmget(key_komisja_A, sizeof(Student_info), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    shared_info = (Student_info *)shmat(shm_komisja_id, NULL, 0);
    if (shared_mem == (int *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msg_dziekan = msgget(key_dziekan, 0666 | IPC_CREAT);
    if (msg_dziekan == -1) {
        perror("Błąd tworzenia kolejki komunikatów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 2, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    semctl(sem_id, SEM_DZIEKAN, SETVAL, 1);
	semctl(sem_id, SEM_STUDENT, SETVAL, 0);

    signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

    int kierunek = rand() % 10 + 1; // Kierunki od 1 do 10
    printf("Dziekan ogłasza: Kierunek %d pisze egzamin.\n", kierunek);

    sem_p(sem_id, SEM_DZIEKAN);
    *shared_mem = kierunek; // Zapisanie numeru kierunku do pamięci współdzielonej
    sem_v(sem_id, SEM_STUDENT);

    while (1) {
        // trzeba tutaj dodac semafor
        ile_studentow = shared_info->ile_kierunek;
        struct message msg;  
        if (msgrcv(msg_dziekan, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_DEAN, 0) == -1) {
            perror("Przerwano");
            exit(1);
        } else {
            student_pids[num_students] = msg.pid;
            num_students++;
            if(num_students == ile_studentow && num_students != 0){ // w przyszlosci zmienic waurnek gdy poprawi sie studenta
                student_pids[num_students] = msg.pid;
                break;
            }
        }
    }

    printf("Dziekan zakończył działanie.\n");

    //cleanup();
    
    return 0;
}

// Zmniejszenie wartości semafora - zamknięcie
void sem_p(int sem_id, int sem_num) {
    int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = -1;
    bufor_sem.sem_flg = 0;
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        } else {
        perror("(Student) Nie mogłem zamknąć semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

// Zwiększenie wartości semafora - otwarcie
void sem_v(int sem_id, int sem_num) {
	  int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0; // flaga 0 (zamiast SEM_UNDO) żeby po wyczerpaniu short inta nie wyrzuciło błędu
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        perror("Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup()
{
    //printf("Została wywołana funkcja czyszcząca!\n");
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Dziekan!");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Dziekan!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Dziekan!");
    }
}