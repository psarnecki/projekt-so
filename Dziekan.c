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
#define SEM_TOTAL_STUDENTS 1
#define SEM_ACTIVE_PROCESS 8

#define STUDENT_TO_DEAN 3
#define COMMISSION_TO_DEAN 4
#define COMMISSION_PID 5

int shm_id, shm_data_id, msg_id, msg_dean_id, sem_id, total_students, is_exam_end = 0;
int student_pids[160];
int commission_pids[2];
int *shared_mem = NULL;
struct student_record *student = NULL;

struct message {
    long msg_type;
    int pid;        
    float grade_A;
    float grade_B;
    int practical_passed;   // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int total_in_major;   // Liczba studentów na ogłoszonym kierunku
    int total_passed_practical;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int commission_A_done;   // Flaga informująca o końcu pracy komisji A
    int commission_B_done;   // Flaga informująca o końcu pracy komisji B
} Exam_data;

Exam_data *shared_data;

struct student_record {
    pid_t pid;        // ID studenta
    float grade_A;    // Ocena z komisji A (-1.0 = brak oceny)
    float grade_B;    // Ocena z komisji B (-1.0 = brak oceny)
    float final_grade;    // Ocena końcowa wystawiona przez dziekana
};

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void cleanup();

float round_grade(float average) {
    if (average > 4.5) {
        return 5.0;
    } else if (average > 4.0) {
        return 4.5;
    } else if (average > 3.5) {
        return 4.0;
    } else if (average > 3.0) {
        return 3.5;
    } else if (average > 2.0) {
        return 3.0;
    } else {
        return 2.0;
    }
}

void show_exam_results() {
    printf("\nLista studentów z przypisanymi ocenami, którzy wzięli udział w egzaminie:\n");
    printf("=======================================================================================================\n");
    printf("| Nr  | PID       | Ocena A | Ocena B | Ocena Końcowa |\n");
    printf("=======================================================================================================\n");

    int record_number = 1;

    for (int i = 0; i < total_students; i++) {
        if (student[i].grade_A != -1.0 || student[i].grade_B != -1.0) {
            printf("| %-3d | %-9d | %-7.1f | %-7.1f | %-13.1f |\n",
            record_number++,
            student[i].pid,
            (student[i].grade_A == -1.0 ? 0.0 : student[i].grade_A), 
            (student[i].grade_B == -1.0 ? 0.0 : student[i].grade_B),
            student[i].final_grade);
        }
    }
    printf("=======================================================================================================\n\n");
}

void handle_signal(int sig) {
    printf("\033[31m\nDziekan nadał komunikat o ewakuacji!\033[0m\n");
    show_exam_results();
    
    for(int i = 0; i < 2; i++) {
        kill(commission_pids[i], SIGUSR1);  // Wysłanie sygnału do procesu komisji
    }
    for (int i = 0; i < total_students; i++) {
        if (kill(student_pids[i], 0) == 0) {  // Sprawdzenie, czy proces istnieje
            kill(student_pids[i], SIGUSR1);  // Wysłanie sygnału do procesu studenta
        }
    }
    sem_p(sem_id, SEM_ACTIVE_PROCESS);

    //oczekiwanie na zakończenie wszystkich programów
    while (1) {
        int active_processes = semctl(sem_id, SEM_ACTIVE_PROCESS, GETVAL);
        if (active_processes == 0) {
            break;
        }
        sleep(1);
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
        perror("Błąd tworzenia klucza!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(key, sizeof(int), IPC_CREAT | 0600);
    shm_data_id = shmget(key_info, sizeof(Exam_data), IPC_CREAT | 0600);
    if (shm_id == -1 || shm_data_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    shared_data = (Exam_data *)shmat(shm_data_id, NULL, 0);
    if (shared_mem == (int *)(-1) || shared_data == (Exam_data *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msg_dean_id = msgget(key_dean_msg, 0600 | IPC_CREAT);
    msg_id = msgget(key_msg, 0600 | IPC_CREAT);
    if (msg_id == -1 || msg_dean_id == -1) {
        perror("Błąd tworzenia kolejki komunikatów!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 9, IPC_CREAT | 0600);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

	semctl(sem_id, SEM_STUDENT, SETVAL, 0);
    semctl(sem_id, SEM_TOTAL_STUDENTS, SETVAL, 0);
    semctl(sem_id, SEM_ACTIVE_PROCESS, SETVAL, 3);

    // Obsługa sygnałów SIGUSR2 i SIGTERM
    signal(SIGUSR2, handle_signal);
	signal(SIGTERM, handle_signal);

    int num_majors;

    while (1) {
        num_majors = *shared_mem; // Odczytanie informacji o liczbie kierunków
        if (num_majors != 0) {
            break;
        }
        sleep(1);
    }

    int announced_major = rand() % num_majors + 1;
    printf("\033[31m\nDziekan ogłasza: Kierunek %d pisze egzamin.\033[0m\n", announced_major);
    *shared_mem = announced_major; // Zapisanie wybranego kierunku do pamięci współdzielonej

    sem_v(sem_id, SEM_STUDENT); // Możliwość odczytania ogłoszenia kierunku przez procesy studentów 

    sem_p(sem_id, SEM_TOTAL_STUDENTS); // Rozpoczęcie odbierania PIDów studentów

    int commission_count = 0, student_count = 0;

    // Odbieranie PIDów procesów komisji i utworzenie z nich tablicy
    while (1) {
        struct message msg;

        if (msgrcv(msg_dean_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_PID, 0) == -1) {
            perror("Błąd odbierania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            commission_pids[commission_count] = msg.pid;
            commission_count++;

            if(commission_count == 2) {
                break;
            }
        }
    }

    // Odbieranie PIDów procesów studentów i utworzenie z nich tablicy
    while (1) {
        total_students = shared_data->total_in_major;
        struct message msg;

        if (msgrcv(msg_dean_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_DEAN, 0) == -1) {
            perror("Błąd odbierania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            student_pids[student_count] = msg.pid;
            student_count++;

            if(student_count == total_students) {
                break;
            }
        }
    }
    
    student = malloc((total_students) * sizeof(struct student_record));
    struct message msg;
    
    // Inicjalizacja struktury studentów
    for (int i = 0; i < total_students; i++) {
        student[i].pid = student_pids[i]; 
        student[i].grade_A = -1.0;  
        student[i].grade_B = -1.0;  
    }

    int grade_count = 0;

    while (1) {
        // Odbieranie ocen wystawionych przez komisje
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), COMMISSION_TO_DEAN, 0) == -1) {
            if (errno == EINTR) {
                continue;  // Ignoruj przerwanie i ponów próbę
            } else {
                perror("Błąd odbierania komunikatu!\n");
                cleanup();
                exit(EXIT_FAILURE);
            }
        }
        
        // Wyszukiwanie studenta po odebranym PID
        for (int i = 0; i < total_students; i++) {
            if (student[i].pid == msg.pid) {
                if (student[i].grade_A == -1.0) {
                    student[i].grade_A = msg.grade_A;  // Przypisanie oceny A
                } else if (student[i].grade_A != -1.0 && student[i].grade_B == -1.0) { // Dla wystawionej oceny z egzaminu A
                    student[i].grade_B = msg.grade_B;  // Przypisanie oceny B
                }

                // Przypisywanie ocen i wystawianie oceny końcowej w zależności od odebranych ocen cząstkowych 
                if (student[i].grade_A == 2.0) {
                    student[i].grade_B = 0.0;
                    student[i].final_grade = 2.0;
                    printf("\033[35mStudent PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\033[0m\n", student[i].pid, student[i].grade_A, student[i].grade_B, student[i].final_grade);
                    grade_count++;
                    break;
                } else if (student[i].grade_A != -1.0 && student[i].grade_B != -1.0) {
                    if (student[i].grade_B == 2.0) {
                        student[i].final_grade = 2.0;
                        printf("\033[35mStudent PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\033[0m\n", student[i].pid, student[i].grade_A, student[i].grade_B, student[i].final_grade);
                        grade_count++;
                        break;
                    } else {
                        student[i].final_grade = round_grade((student[i].grade_A + student[i].grade_B) / 2.0);
                        printf("\033[35mStudent PID: %d, Ocena A: %.1f, Ocena B: %.1f, Ocena końcowa: %.1f\033[0m\n", student[i].pid, student[i].grade_A, student[i].grade_B, student[i].final_grade);
                        grade_count++;
                        break;
                    }
                }
            }
        }

        if(grade_count == total_students) {
            break;
        }
    }
    
    // Odebranie informacji od komisji B o zakończeniu oceniania wszystkich studentów oraz wyświetlenie ocen
    while(1) {
        is_exam_end = shared_data->commission_B_done;
        if(is_exam_end == 1){
            show_exam_results();
            break;
        }
    }
    
    sem_p(sem_id, SEM_ACTIVE_PROCESS);

    // Oczekiwanie na zakończenie wszystkich programów
    while (1) {
        int active_processes = semctl(sem_id, SEM_ACTIVE_PROCESS, GETVAL);
        if (active_processes == 0) {
            break;
        }
        sleep(1);
    }
 
    printf("\nSymulacja zakończona.\n");

    free(student);
    cleanup();
    return 0;
}

void sem_p(int sem_id, int sem_num) {
    int change_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = -1;
    bufor_sem.sem_flg = 0;
    change_sem=semop(sem_id, &bufor_sem, 1);
    if (change_sem == -1){
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        } else {
        perror("Błąd zamykania semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

void sem_v(int sem_id, int sem_num) {
	int change_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0;
    change_sem=semop(sem_id, &bufor_sem, 1);
    if (change_sem == -1){
        perror("Błąd otwierania semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_mem!\n");
    }
    if (shared_data != NULL && shmdt(shared_data) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_data!\n");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_id!\n");
    }
    if (shmctl(shm_data_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_data_id!\n");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów sem_id!\n");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_id!\n");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_dean_id!\n");
    }
}