#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // Do obsługi pamięci dzielonej
#include <sys/sem.h> // Do obsługi semaforów
#include <sys/msg.h> // Do obsługi kolejki komunikatów
#include <signal.h> // do obsługi sygnałów
#include <errno.h>

#define MIN_STUDENTS 80
#define MAX_STUDENTS 160
#define PREPARE_TIME 3
#define STUDENT_TO_DEAN 3
#define SEM_DZIEKAN 1
#define SEM_STUDENT 0
#define SEM_EGZAMIN_PRAKTYCZNY 0
#define SEM_KOMISJA_A 1
#define SEM_EGZAMIN_A 2
#define SEM_ILE_STUDENTOW_1 3
#define SEM_ILE_STUDENTOW_2 4
#define STUDENT_TO_COMMISSION_A 1
#define SEM_EGZAMIN_TEORETYCZNY 0
#define SEM_KOMISJA_B 1
#define SEM_EGZAMIN_B 2
#define STUDENT_TO_COMMISSION_B 2
#define SEM_ILE_STUDENTOW 2

int sem_id, sem_komisja_A_id, sem_komisja_B_id, shm_id, shm_komisja_id, msgid, msg_dziekan, w, x;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;
int *liczba_studentow = NULL; // Wskaźnik do tablicy z liczbami studentów na każdym kierunku

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

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
    int komisja_B_koniec;
} Student_info;

Student_info *shared_info;

// Argument procesu studenta przekazywany w strukturze przy tworzeniu procesu
typedef struct {
    int kierunek;
    pid_t pid;     // PID procesu studenta jako identyfikator studenta
} Student;

void handle_signal(int sig);

void symuluj_przybycie(Student* dane) {
    //printf("Student o PID %d z kierunku %d przybył do kolejki.\n", dane->pid, dane->kierunek);

    int ile_studentow = 0;

    // Oczekiwanie na ogłoszenie od dziekana
    while (1) {
        sem_p(sem_id, SEM_STUDENT); // Oczekiwanie na dostęp do pamięci dzielonej
        ogloszony_kierunek = *shared_mem;
        if (ogloszony_kierunek != 0) {
            sem_v(sem_id, SEM_STUDENT); // Zwolnienie semafora
            break;
        }
        sem_v(sem_id, SEM_STUDENT); // Zwolnienie semafora
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem ogłoszenia
    }

    // Reakcja na ogłoszenie dziekana
    if (dane->kierunek == ogloszony_kierunek) {
        signal(SIGUSR1, handle_signal);

        ile_studentow = liczba_studentow[ogloszony_kierunek - 1];
        shared_info->ile_kierunek = ile_studentow;

        sem_v(sem_id, SEM_ILE_STUDENTOW);

        struct message msg;
        msg.msg_type = STUDENT_TO_DEAN;
        msg.pid = dane->pid; // PID studenta

        // Wysyłanie PIDu studenta
        if (msgsnd(msg_dziekan, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(1);
        }

        sem_p(sem_komisja_A_id, SEM_ILE_STUDENTOW_1);
        shared_info->ile_kierunek = ile_studentow;
        sem_v(sem_komisja_A_id, SEM_ILE_STUDENTOW_2);

        sem_p(sem_komisja_A_id, SEM_EGZAMIN_PRAKTYCZNY); // Czekanie na dostępne miejsce do egzaminu
        ///printf("Student %d z kierunku %d wchodzi na egzamin praktyczny.\n", dane->pid, dane->kierunek);

        sem_p(sem_komisja_A_id, SEM_EGZAMIN_A);  // Sprawdzenie czy można podejść do komisji

        msg.msg_type = STUDENT_TO_COMMISSION_A;
        msg.pid = dane->pid; // PID studenta

        // Wysyłanie PIDu studenta
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(1);
        }

        sem_v(sem_komisja_A_id, SEM_KOMISJA_A); // Uruchomienie komisji (symulacja oczekiwania na pytania po stronie komisji)
        // Tutaj ewentualnie można dodać semafor który będzie odbierał informacje o gotowych pytaniach 
        sem_v(sem_komisja_A_id, SEM_EGZAMIN_A);  // Zwolnienie miejsca przy komisji
        sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

        sem_p(sem_komisja_A_id, SEM_EGZAMIN_A);  // Oczekiwanie na zwolnienie się komisji po otrzymanie oceny za odpowiedź

        // Odebranie oceny od komisji
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), dane->pid, 0) == -1) {
            perror("msgrcv");
            exit(1);
        } else {
            ///printf("Student %d otrzymał ocenę A: %.1f\n", msg.pid, msg.ocena_A);
        }

        sem_v(sem_komisja_A_id, SEM_EGZAMIN_A); // Zwolnienie miejsca do komisji po zakończeniu egzaminu

        ///printf("Student %d zwalnia miejsce na egzamin praktyczny.\n", dane->pid);
        sem_v(sem_komisja_A_id, SEM_EGZAMIN_PRAKTYCZNY);  // Zwolnienie miejsca do egzaminu po zakończeniu egzaminu

        if (msg.ocena_A >= 3.0) {
            ///printf("Student %d kwalifikuje się do Komisji B z oceną: %.1f\n", msg.pid, msg.ocena_A);

            sem_p(sem_komisja_B_id, SEM_EGZAMIN_TEORETYCZNY); // Czekanie na dostępne miejsce do egzaminu
            ///printf("Student %d z kierunku %d wchodzi na egzamin teoretyczny.\n", dane->pid, dane->kierunek);

            sem_p(sem_komisja_B_id, SEM_EGZAMIN_B);  // Sprawdzenie czy można podejść do komisji

            struct message msg;
            msg.msg_type = STUDENT_TO_COMMISSION_B;
            msg.pid = dane->pid; // PID studenta

            // Wysyłanie PIDu studenta
            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                exit(1);
            }

            sem_v(sem_komisja_B_id, SEM_KOMISJA_B); // Uruchomienie komisji (symulacja oczekiwania na pytania po stronie komisji)
            // Tutaj ewentualnie można dodać semafor który będzie odbierał informacje o gotowych pytaniach 
            sem_v(sem_komisja_B_id, SEM_EGZAMIN_B);  // Zwolnienie miejsca przy komisji
            sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

            sem_p(sem_komisja_B_id, SEM_EGZAMIN_B);  // Oczekiwanie na zwolnienie się komisji po otrzymanie oceny za odpowiedź

            // Odebranie oceny od komisji
            if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), dane->pid, 0) == -1) {
                perror("msgrcv");
                exit(1);
            } else {
                ///printf("Student %d otrzymał ocenę B: %.1f\n", msg.pid, msg.ocena_B);
            }

            sem_v(sem_komisja_B_id, SEM_EGZAMIN_B); // Zwolnienie miejsca do komisji po zakończeniu egzaminu

            ///printf("Student %d zwalnia miejsce na egzamin teoretyczny.\n", dane->pid);
            sem_v(sem_komisja_B_id, SEM_EGZAMIN_TEORETYCZNY);  // Zwolnienie miejsca do egzaminu po zakończeniu egzaminu
        } else {
            ///printf("Student o PID %d nie zdał egzaminu praktycznego i wraca do domu.\n", dane->pid);
        }
        
    } else {
        //printf("Student o PID %d z kierunku %d wraca do domu.\n", dane->pid, dane->kierunek);
    }
}

void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
    key_t key_komisja_A = ftok(".", 'A');
    key_t key_komisja_B = ftok(".", 'B');
    key_t key_msg = ftok(".", 'M');
    key_t key_dziekan = ftok(".", 'D');
    if (key == -1 || key_komisja_A == -1 || key_komisja_B == -1 || key_msg == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(key, sizeof(int), IPC_CREAT | 0666);
    shm_komisja_id = shmget(key_komisja_A, sizeof(Student_info), IPC_CREAT | 0666);
    if (shm_id == -1 || shm_komisja_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    shared_info = (Student_info *)shmat(shm_komisja_id, NULL, 0);
    if (shared_mem == (int *)(-1) || shared_info == (Student_info *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msgid = msgget(key_msg, 0666 | IPC_CREAT);
    msg_dziekan = msgget(key_dziekan, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("Błąd tworzenia kolejki komunikatów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 3, IPC_CREAT | 0666);
    sem_komisja_A_id = semget(key_komisja_A, 5, IPC_CREAT | 0666);
    sem_komisja_B_id = semget(key_komisja_B, 4, IPC_CREAT | 0666);
    if (sem_id == -1 || sem_komisja_A_id == -1 || sem_komisja_B_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    int liczba_kierunkow = rand() % 11 + 5; // Losowanie liczby kierunków
    printf("Liczba kierunków: %d\n", liczba_kierunkow);

    liczba_studentow = (int *)malloc(liczba_kierunkow * sizeof(int)); // Tworzenie tablicy przechowującej ilość studentów na danym kierunku

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        liczba_studentow[i] = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, liczba_studentow[i]);
    }

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie procesów dla studentów
    for (int i = 0; i < liczba_kierunkow; i++) {
        for (int j = 1; j <= liczba_studentow[i]; j++) {
            //sleep(1);
            //usleep(rand() % 50000);
            pid_t pid = fork();  // Tworzenie procesu dla studenta
            if (pid == 0) {  // Jeśli to proces studenta
                Student* student = (Student*)malloc(sizeof(Student)); // Alokacja pamięci dla studenta
                student->kierunek = i + 1;
                student->pid = getpid(); // Ustawienie PID jako ID studenta

                symuluj_przybycie(student);

                // Sprawdzenie, czy proces jest procesem wybranego kierunku
                if (i + 1 != ogloszony_kierunek) {
                    free(student); // Zwolnienie pamięci po zakończeniu pracy studenta
                    exit(0);  // Zakończenie procesu studenta, jeśli nie jest to wybrany kierunek
                } else {
                    /* 
                        Miejsce na przyszły kod dla procesu wybranego kierunku
                    */
                    sleep(3);
                    free(student); // Zwolnienie pamięci po zakończeniu pracy studenta
                    exit(0);
                }
            }
        }
    }

    // Czekanie na zakończenie wszystkich procesów potomnych
    while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!");
            exit(EXIT_FAILURE);
        }
        //printf("Zakonczyl sie proces potomny o PID=%d i statusie = %d\n", w, x);
    }

    //cleanup();

    //printf("\nSymulacja zakończona.\n");

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
        perror("(Student) Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    printf("Została wywołana funkcja czyszcząca!\n");
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

void handle_signal(int sig) {
    printf("Otrzymano sygnał %d. Student kończy pracę.\n", sig);
    exit(0);  // Zakończenie procesu studenta
}
