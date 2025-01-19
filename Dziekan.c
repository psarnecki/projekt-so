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

#define SEM_STUDENT 0
#define SEM_ILE_STUDENTOW 1

#define STUDENT_TO_DEAN 3
#define COMMISSION_TO_DEAN 4
#define COMMISSION_PID 5

int shm_id, shm_info_id, msg_id, msg_dean_id, sem_id, ile_studentow = 0, czy_koniec = 0;
int *shared_mem = NULL;
int num_students = 0;  // Liczba aktywnych studentów
int student_pids[160];  // Wskaźnik na dynamiczną tablicę przechowującą PID-y studentów
int commission_pids[2];
struct student_record *students = NULL;

struct message {
    long msg_type;  // Typ komunikatu
    int pid;        // PID studenta
    float ocena_A;      // Ocena (dla odpowiedzi)
    float ocena_B;
    int zaliczenie;
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
    int komisja_B_koniec;
} Student_info;

Student_info *shared_info;

struct student_record {
    pid_t pid;        // PID studenta
    float ocena_A;    // Ocena z komisji A (-1.0 oznacza brak oceny)
    float ocena_B;    // Ocena z komisji B (-1.0 oznacza brak oceny)
    float podsumowanie;
};

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void cleanup();

void wyswietl_oceny() {
    printf("\nLista studentów z przypisanymi ocenami, którzy wzięli udział w egzaminie:\n");
    printf("=======================================================================================================\n");
    printf("| Nr  | PID       | Ocena A | Ocena B | Ocena Końcowa |\n");
    printf("=======================================================================================================\n");

    int numer = 1; // Licznik numeracji
    for (int i = 0; i < ile_studentow; i++) {
        // Sprawdzenie czy student ma przypisaną przynajmniej jedną ocenę
        if (students[i].ocena_A != -1.0 || students[i].ocena_B != -1.0) {
            printf("| %-3d | %-9d | %-7.1f | %-7.1f | %-13.1f |\n",
            numer++,
            students[i].pid,
            (students[i].ocena_A == -1.0 ? 0.0 : students[i].ocena_A),
            (students[i].ocena_B == -1.0 ? 0.0 : students[i].ocena_B),
            students[i].podsumowanie);
        }
    }
    printf("=======================================================================================================\n\n");
}

void handle_signal(int sig) {
    wyswietl_oceny();
    for(int i = 0; i < 2; i++) {
        kill(commission_pids[i], SIGUSR1);
    }
    for (int i = 0; i < num_students; i++) {
        if (kill(student_pids[i], 0) == 0) {  // Sprawdzenie, czy proces istnieje
            kill(student_pids[i], SIGUSR1);  // Wysyłanie sygnału do studenta
        }
    }
    cleanup();
    exit(0);
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
        ile_kierunkow = *shared_mem; // Odczytanie liczby kierunków
        if (ile_kierunkow != 0) {
            //printf("Dziekan odczytuje ile jest kierunków: %d\n", ile_kierunkow);
            break;  // Wyjście z pętli, jeśli liczba kierunków jest różna od 0
        }
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem
    }

    int kierunek = rand() % ile_kierunkow + 1; // Kierunki od 1 do 10
    printf("Dziekan ogłasza: Kierunek %d pisze egzamin.\n", kierunek);
    *shared_mem = kierunek; // Zapisanie numeru kierunku do pamięci współdzielonej

    sem_v(sem_id, SEM_STUDENT);

    sem_p(sem_id, SEM_ILE_STUDENTOW); // Odbieranie PIDów studentów

    int licznik = 0;

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
            //printf("[%d] Student PID: %d. Na kierunku jest %d studentow.\n", num_students, msg.pid, ile_studentow);
            if(num_students == ile_studentow){
                break;
            }
        }
    }

    while (1) {
        struct message msg;
        if (msgrcv(msg_dean_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_PID, 0) == -1) {
            perror("msgrcv Dziekan 1");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            commission_pids[licznik] = msg.pid;
            licznik++;
            //printf("[%d] Komisja PID: %d.\n", licznik, msg.pid, ile_studentow);
            if(licznik == 2){
                break;
            }
        }
    }
    
    students = malloc((ile_studentow) * sizeof(struct student_record));
    struct message msg;
    
    // Inicjalizacja struktury studentów (przykład)
    for (int i = 0; i < ile_studentow; i++) {
        students[i].pid = student_pids[i];  // Student PID
        students[i].ocena_A = -1.0;  // Brak oceny A
        students[i].ocena_B = -1.0;  // Brak oceny B
    }

    while (1) {
        // Odbieranie wiadomości z kolejki komunikatów
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_TO_DEAN, 0) == -1) {
            if (errno == EINTR) {
                continue;  // Ignoruj przerwanie i ponów próbę
            } else {
                perror("msgrcv Dziekan 2");
                cleanup();
                exit(EXIT_FAILURE);
            }
        } else {
            //printf("Odebrałem wiadomość z PID %d i oceną %.1f, %.1f.\n", msg.pid, msg.ocena_A, msg.ocena_B);
        }

        // Wyszukiwanie studenta po pid
        for (int i = 0; i < ile_studentow; i++) {
            if (students[i].pid == msg.pid) {
                if (students[i].ocena_A == -1.0) {
                    students[i].ocena_A = msg.ocena_A;  // Przypisanie oceny A
                    //printf("Dziekan: Ocena A dla studenta PID %d: %.1f\n", students[i].pid, students[i].ocena_A);
                } else if (students[i].ocena_A != -1.0 && students[i].ocena_B == -1.0) {
                    students[i].ocena_B = msg.ocena_B;  // Przypisanie oceny B
                    //printf("Dziekan: Ocena B dla studenta PID %d: %.1f\n", students[i].pid, students[i].ocena_B);
                }

                if (students[i].ocena_A == 2.0) {
                    students[i].ocena_B = 0.0;
                    students[i].podsumowanie = 2.0;
                    printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].podsumowanie);
                    break;
                } else if (students[i].ocena_A != -1.0 && students[i].ocena_B != -1.0) {
                    if (students[i].ocena_B == 2.0) {
                        students[i].podsumowanie = 2.0;
                        printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].podsumowanie);
                        break;
                    } else {
                        students[i].podsumowanie = (students[i].ocena_A + students[i].ocena_B) / 2.0;
                        printf("Student PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\n", students[i].pid, students[i].ocena_A, students[i].ocena_B, students[i].podsumowanie);
                        break;
                    }
                }
            }
        }

        czy_koniec = shared_info->komisja_B_koniec;

        if(czy_koniec == 1){
            break;
        }
    }

    wyswietl_oceny();

    //printf("Dziekan zakończył działanie.\n");

    printf("\nSymulacja zakończona.\n");

    free(students);
    cleanup();
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

void cleanup() {
    //printf("Została wywołana funkcja czyszcząca!\n");
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Dziekan!");
    }
    if (shared_info != NULL && shmdt(shared_info) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Komisja!");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Komisja!");
    }
    if (shmctl(shm_info_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Komisja!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Komisja A!");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów!");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów!");
    }
}