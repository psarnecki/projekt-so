#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <sys/sem.h> 
#include <sys/msg.h> 
#include <signal.h> 
#include <errno.h>

#define SEM_STUDENT 0
#define SEM_ILE_STUDENTOW 1

#define STUDENT_TO_DEAN 3
#define COMMISSION_TO_DEAN 4
#define COMMISSION_PID 5

int shm_id, shm_info_id, msg_id, msg_dean_id, sem_id, ile_studentow, czy_koniec = 0;
int *shared_mem = NULL;
int student_pids[160];
int commission_pids[2];
struct student_record *students = NULL;

struct message {
    long msg_type;
    int pid;        
    float ocena_A;      
    float ocena_B;
    int zaliczenie;   // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
    int komisja_B_koniec;   // Flaga informująca o końcu pracy komisji B
} Student_info;

Student_info *shared_info;

struct student_record {
    pid_t pid;        // ID studenta
    float ocena_A;    // Ocena z komisji A (-1.0 = brak oceny)
    float ocena_B;    // Ocena z komisji B (-1.0 = brak oceny)
    float ocena_koncowa;    // Ocena końcowa wystawiona przez dziekana
};

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void cleanup();

void wyswietl_oceny() {
    printf("\nLista studentów z przypisanymi ocenami, którzy wzięli udział w egzaminie:\n");
    printf("=======================================================================================================\n");
    printf("| Nr  | PID       | Ocena A | Ocena B | Ocena Końcowa |\n");
    printf("=======================================================================================================\n");

    int num = 1;

    for (int i = 0; i < ile_studentow; i++) {
        if (students[i].ocena_A != -1.0 || students[i].ocena_B != -1.0) {
            printf("| %-3d | %-9d | %-7.1f | %-7.1f | %-13.1f |\n",
            num++,
            students[i].pid,
            (students[i].ocena_A == -1.0 ? 0.0 : students[i].ocena_A), 
            (students[i].ocena_B == -1.0 ? 0.0 : students[i].ocena_B),
            students[i].ocena_koncowa);
        }
    }
    printf("=======================================================================================================\n\n");
}

void handle_signal(int sig) {
    wyswietl_oceny();
    for(int i = 0; i < 2; i++) {
        kill(commission_pids[i], SIGUSR1);  // Wysłanie sygnału do procesu komisji
    }
    for (int i = 0; i < ile_studentow; i++) {
        if (kill(student_pids[i], 0) == 0) {  // Sprawdzenie, czy proces istnieje
            kill(student_pids[i], SIGUSR1);  // Wysłanie sygnału do procesu studenta
        }
    }
    cleanup();
    exit(0);
}

int main() {
    srand(time(NULL));

    key_t key = ftok(".", 'S');
    key_t key_info = ftok(".", 'I');
    key_t key_msg = ftok(".", 'M');
    key_t key_dean_msg = ftok(".", 'D');
    if (key == -1 || key_info == -1 || key_msg == -1 || key_dean_msg == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(key, sizeof(int), IPC_CREAT | 0666);
    shm_info_id = shmget(key_info, sizeof(Student_info), IPC_CREAT | 0666);
    if (shm_id == -1 || shm_info_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    shared_info = (Student_info *)shmat(shm_info_id, NULL, 0);
    if (shared_mem == (int *)(-1) || shared_info == (Student_info *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msg_dean_id = msgget(key_dean_msg, 0666 | IPC_CREAT);
    msg_id = msgget(key_msg, 0666 | IPC_CREAT);
    if (msg_id == -1 || msg_dean_id == -1) {
        perror("Błąd tworzenia kolejki komunikatów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 10, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

	semctl(sem_id, SEM_STUDENT, SETVAL, 1);
    semctl(sem_id, SEM_ILE_STUDENTOW, SETVAL, 0);

    signal(SIGUSR2, handle_signal);
	signal(SIGTERM, handle_signal);

    int ile_kierunkow;

    while (1) {
        ile_kierunkow = *shared_mem; // Odczytanie informacji o liczbie kierunków
        if (ile_kierunkow != 0) {
            break;
        }
        sleep(1);
    }

    int kierunek = rand() % ile_kierunkow + 1;
    printf("Dziekan ogłasza: Kierunek %d pisze egzamin.\n", kierunek);
    *shared_mem = kierunek; // Zapisanie wybranego kierunku do pamięci współdzielonej

    sem_v(sem_id, SEM_STUDENT); // Możliwość odczytania ogłoszenia kierunku przez procesy studentów 

    sem_p(sem_id, SEM_ILE_STUDENTOW); // Rozpoczęcie odbierania PIDów studentów

    int num_commissions = 0, num_students = 0;

    // Odbieranie PIDów procesów studentów i utworzenie z nich tablicy
    while (1) {
        ile_studentow = shared_info->ile_kierunek;
        struct message msg;

        if (msgrcv(msg_dean_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_DEAN, 0) == -1) {
            perror("msgrcv Dziekan 1");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            student_pids[num_students] = msg.pid;
            num_students++;
            if(num_students == ile_studentow) {
                break;
            }
        }
    }

    // Odbieranie PIDów procesów komisji i utworzenie z nich tablicy
    while (1) {
        struct message msg;
        
        if (msgrcv(msg_dean_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_PID, 0) == -1) {
            perror("msgrcv Dziekan 1");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            commission_pids[num_commissions] = msg.pid;
            num_commissions++;

            if(num_commissions == 2) {
                break;
            }
        }
    }
    
    students = malloc((ile_studentow) * sizeof(struct student_record));
    struct message msg;
    
    // Inicjalizacja struktury studentów
    for (int i = 0; i < ile_studentow; i++) {
        students[i].pid = student_pids[i]; 
        students[i].ocena_A = -1.0;  
        students[i].ocena_B = -1.0;  
    }

    while (1) {
        // Odbieranie ocen wystawionych przez komisje
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_TO_DEAN, 0) == -1) {
            if (errno == EINTR) {
                continue;  // Ignoruj przerwanie i ponów próbę
            } else {
                perror("msgrcv Dziekan 2");
                cleanup();
                exit(EXIT_FAILURE);
            }
        }

        // Wyszukiwanie studenta po odebranym PID
        for (int i = 0; i < ile_studentow; i++) {
            if (students[i].pid == msg.pid) {
                if (students[i].ocena_A == -1.0) {
                    students[i].ocena_A = msg.ocena_A;  // Przypisanie oceny A
                } else if (students[i].ocena_A != -1.0 && students[i].ocena_B == -1.0) { // Dla wystawionej oceny z egzaminu A
                    students[i].ocena_B = msg.ocena_B;  // Przypisanie oceny B
                }

                // Przypisywanie ocen i wystawianie oceny końcowej w zależności od odebranych ocen cząstkowych 
                if (students[i].ocena_A == 2.0) {
                    students[i].ocena_B = 0.0;
                    students[i].ocena_koncowa = 2.0;
                    printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].ocena_koncowa);
                    break;
                } else if (students[i].ocena_A != -1.0 && students[i].ocena_B != -1.0) {
                    if (students[i].ocena_B == 2.0) {
                        students[i].ocena_koncowa = 2.0;
                        printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].ocena_koncowa);
                        break;
                    } else {
                        students[i].ocena_koncowa = (students[i].ocena_A + students[i].ocena_B) / 2.0;
                        printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].ocena_koncowa);
                        break;
                    }
                }
            }
        }
        czy_koniec = shared_info->komisja_B_koniec; // Informacja od komisji B o zakończeniu oceniania wszystkich studentów

        if(czy_koniec == 1){
            break;
        }
    }

    wyswietl_oceny();

    printf("\nSymulacja zakończona.\n");

    free(students);
    cleanup();
    return 0;
}

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
        perror("Nie mogłem zamknąć semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

void sem_v(int sem_id, int sem_num) {
	  int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0;
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        perror("Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_mem!");
    }
    if (shared_info != NULL && shmdt(shared_info) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_info!");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_id!");
    }
    if (shmctl(shm_info_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_info_id!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów sem_id!");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_id!");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_dean_id!");
    }
}