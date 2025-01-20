#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h> 
#include <sys/msg.h> 
#include <signal.h> 
#include <errno.h>

#define MIN_STUDENTS 80
#define MAX_STUDENTS 160
#define PREPARE_TIME 1

#define SEM_STUDENT 0
#define SEM_ILE_STUDENTOW 1

#define SEM_EGZAMIN_PRAKTYCZNY 2
#define SEM_KOMISJA_A 3
#define SEM_EGZAMIN_A 4
#define SEM_ILE_STUDENTOW_1 5
#define SEM_ILE_STUDENTOW_2 6

#define SEM_EGZAMIN_TEORETYCZNY 7
#define SEM_KOMISJA_B 8
#define SEM_EGZAMIN_B 9

#define STUDENT_TO_COMMISSION_A 1
#define STUDENT_TO_COMMISSION_B 2
#define STUDENT_TO_DEAN 3

int shm_id, shm_info_id, msg_id, msg_dean_id, sem_id, w, x;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;
int *liczba_studentow = NULL;

struct message {
    long msg_type;  
    int pid;        
    float ocena_A;      
    float ocena_B;
    int zaliczenie;  // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
    int komisja_B_koniec;   // Flaga informująca o końcu pracy komisji B
} Student_info;

Student_info *shared_info;

typedef struct {
    int kierunek;
    pid_t pid;
    int praktyka_zaliczona;
} Student;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void handle_signal(int sig);
void cleanup();

void symuluj_przybycie(Student* dane) {
    printf("Student o PID %d z kierunku %d przybył do kolejki.\n", dane->pid, dane->kierunek);

    int ile_studentow = 0;

    // Oczekiwanie na ogłoszenie kierunku przez dziekana
    while (1) {
        sem_p(sem_id, SEM_STUDENT);
        ogloszony_kierunek = *shared_mem;
        if (ogloszony_kierunek != 0) {
            sem_v(sem_id, SEM_STUDENT);
            break;
        }
        sem_v(sem_id, SEM_STUDENT); 
        sleep(1);
    }

    // Reakcja na ogłoszenie dziekana = wejście do budynku
    if (dane->kierunek == ogloszony_kierunek) {
        signal(SIGUSR1, handle_signal);

        ile_studentow = liczba_studentow[ogloszony_kierunek - 1]; // Liczba studentów na ogłoszonym kierunku
        shared_info->ile_kierunek = ile_studentow;

        sem_v(sem_id, SEM_ILE_STUDENTOW); // Dziekan zaczyna odbierać PIDy studentów, którzy są w budynku

        struct message msg;

        // Wysyłanie PIDu studenta do dziekana
        msg.msg_type = STUDENT_TO_DEAN;
        msg.pid = dane->pid;
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        }

        // Informacja dla komisji A z ilością studentów na wybranym kierunku
        sem_p(sem_id, SEM_ILE_STUDENTOW_1);
        shared_info->ile_kierunek = ile_studentow;
        sem_v(sem_id, SEM_ILE_STUDENTOW_2);

        sem_p(sem_id, SEM_EGZAMIN_PRAKTYCZNY); // Zajęcie miejsca w komisji (3 miejsca)

        sem_p(sem_id, SEM_EGZAMIN_A);  // Podejście studenta do komisji

        // Wysyłanie PIDu studenta wraz z informacją o zaliczonym egzaminie praktycznym
        msg.msg_type = STUDENT_TO_COMMISSION_A;
        msg.pid = dane->pid; 
        msg.zaliczenie = dane->praktyka_zaliczona;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        }

        sem_v(sem_id, SEM_KOMISJA_A); // Uruchomienie komisji (symulacja oczekiwania na pytania)

        if(dane->praktyka_zaliczona == 1) {
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), dane->pid, 0) == -1) {
                perror("msgrcv Student A");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EGZAMIN_A); // Odejście studenta od komisji
            sem_v(sem_id, SEM_EGZAMIN_PRAKTYCZNY); // Zwolnienie miejsca w komisji po przekazaniu informacji o zaliczonym egzaminie praktycznym
        } else { 
            sem_v(sem_id, SEM_EGZAMIN_A);  // Odejście studenta od komisji

            sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

            sem_p(sem_id, SEM_EGZAMIN_A);  // Oczekiwanie na zwolnienie się komisji, aby odpowiedzieć na pytania

            // Odebranie oceny od komisji
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), dane->pid, 0) == -1) {
                perror("msgrcv Student A");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EGZAMIN_A); // Odejście studenta od komisji po zakończeniu egzaminu

            sem_v(sem_id, SEM_EGZAMIN_PRAKTYCZNY);  // Zwolnienie miejsca w komisji po zakończeniu egzaminu
        }

        // Egzamin teoretyczny dla studentów z pozytywną oceną z części praktycznej
        if (msg.ocena_A >= 3.0) {

            sem_p(sem_id, SEM_EGZAMIN_TEORETYCZNY); // Zajęcie miejsca w komisji (3 miejsca)

            sem_p(sem_id, SEM_EGZAMIN_B);  // Podejście studenta do komisji

            struct message msg;

            // Wysyłanie PIDu studenta
            msg.msg_type = STUDENT_TO_COMMISSION_B;
            msg.pid = dane->pid;
            if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_KOMISJA_B); // Uruchomienie komisji (symulacja oczekiwania na pytania)

            sem_v(sem_id, SEM_EGZAMIN_B);  // Odejście studenta od komisji

            sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

            sem_p(sem_id, SEM_EGZAMIN_B);  // Oczekiwanie na zwolnienie się komisji, aby odpowiedzieć na pytania

            // Odebranie oceny od komisji
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), dane->pid, 0) == -1) {
                perror("msgrcv Student B");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EGZAMIN_B); // Odejście studenta od komisji po zakończeniu egzaminu

            sem_v(sem_id, SEM_EGZAMIN_TEORETYCZNY);  // Zwolnienie miejsca w komisji po zakończeniu egzaminu
        }  
    } else {
        printf("Student o PID %d z kierunku %d wraca do domu.\n", dane->pid, dane->kierunek);
    }
}

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

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

    msg_id = msgget(key_msg, 0666 | IPC_CREAT);
    msg_dean_id = msgget(key_dean_msg, 0666 | IPC_CREAT);
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

    int liczba_kierunkow = rand() % 11 + 5; // Losowanie liczby kierunków 1-15

    *shared_mem = liczba_kierunkow;

    liczba_studentow = (int *)malloc(liczba_kierunkow * sizeof(int)); // Tworzenie tablicy przechowującej liczbe studentów na danym kierunku

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        liczba_studentow[i] = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, liczba_studentow[i]);
    }   

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie procesów dla każdego studenta
    for (int i = 0; i < liczba_kierunkow; i++) {
        for (int j = 1; j <= liczba_studentow[i]; j++) {
            //sleep(1);
            usleep(rand() % 50000);
            pid_t pid = fork();  
            if (pid == 0) {
                srand(time(NULL) ^ getpid());

                Student* student = (Student*)malloc(sizeof(Student));
                student->kierunek = i + 1;
                student->pid = getpid(); // Ustawienie PID jako ID studenta

                student->praktyka_zaliczona = (rand() % 100 < 5) ? 1 : 0; // Losowanie 5% studentów z zaliczonym egzaminem praktycznym

                symuluj_przybycie(student);

                // Sprawdzenie czy proces jest procesem wybranego kierunku
                if (i + 1 != ogloszony_kierunek) {
                    free(student); 
                    exit(0);  
                } else {
                    free(student);
                    exit(0);
                }
            }
        }
    }

    // Czekanie na zakończenie wszystkich procesów potomnych studentów
    while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!");
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    free(liczba_studentow);
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

void handle_signal(int sig) {
    exit(0);
}