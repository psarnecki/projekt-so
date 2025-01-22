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
#define PREPARE_TIME 1  // Określenie czasu na przygotowanie się do odpowiedzi dla studenta

#define SEM_STUDENT 0
#define SEM_TOTAL_STUDENTS 1
#define SEM_ACTIVE_PROCESS 8

#define SEM_PRACTICAL_EXAM 2
#define SEM_COMMISSION_A 3
#define SEM_EXAM_A 4

#define SEM_THEORETICAL_EXAM 5
#define SEM_COMMISSION_B 6
#define SEM_EXAM_B 7

#define STUDENT_TO_COMMISSION_A 1
#define STUDENT_TO_COMMISSION_B 2
#define STUDENT_TO_DEAN 3

int shm_id, shm_data_id, msg_id, msg_dean_id, sem_id, wait_status, exit_status, announced_major = 0;
int *shared_mem = NULL;
int *num_students = NULL;

struct message {
    long msg_type;  
    int pid;        
    float grade_A;      
    float grade_B;
    int practical_passed;  // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int total_in_major;   // Liczba studentów na ogłoszonym kierunku
    int total_passed_practical;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int commission_A_done;   // Flaga informująca o końcu pracy komisji A
    int commission_B_done;   // Flaga informująca o końcu pracy komisji B
} Exam_data;

Exam_data *shared_data;

typedef struct {
    pid_t pid;
    int major;
    int is_exam_passed;
} Student;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void handle_signal(int sig);
void cleanup();

void simulate_student(Student* student_data) {
    //printf("Student o PID %d z kierunku %d przybył do kolejki.\n", student_data->pid, student_data->major);

    int total_students = 0;

    // Oczekiwanie na ogłoszenie kierunku przez dziekana
    while (1) {
        sem_p(sem_id, SEM_STUDENT);
        announced_major = *shared_mem;
        if (announced_major != 0) {
            sem_v(sem_id, SEM_STUDENT);
            break;
        }
        sem_v(sem_id, SEM_STUDENT); 
        sleep(1);
    }

    // Reakcja na ogłoszenie dziekana = wejście do budynku
    if (student_data->major == announced_major) {
        signal(SIGUSR1, handle_signal); // Obsługa sygnału SIGUSR1

        total_students = num_students[announced_major - 1]; // Liczba studentów na ogłoszonym kierunku
        shared_data->total_in_major = total_students;

        sem_v(sem_id, SEM_TOTAL_STUDENTS); // Dziekan zaczyna odbierać PIDy studentów, którzy są w budynku

        struct message msg;

        // Wysyłanie PIDu studenta do dziekana
        msg.msg_type = STUDENT_TO_DEAN;
        msg.pid = student_data->pid;
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }

        sem_p(sem_id, SEM_PRACTICAL_EXAM); // Zajęcie miejsca w komisji (3 miejsca)

        sem_p(sem_id, SEM_EXAM_A);  // Podejście studenta do komisji

        // Wysyłanie PIDu studenta wraz z informacją o zaliczonym egzaminie praktycznym
        msg.msg_type = STUDENT_TO_COMMISSION_A;
        msg.pid = student_data->pid; 
        msg.practical_passed = student_data->is_exam_passed;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }

        sem_v(sem_id, SEM_COMMISSION_A); // Uruchomienie komisji (symulacja oczekiwania na pytania)

        if(student_data->is_exam_passed == 1) {
            //Odebranie pozytywnej oceny dla wcześniej zaliczonego egzaminu praktycznego
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), student_data->pid, 0) == -1) {
                perror("Błąd odbierania komunikatu!\n");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EXAM_A); // Odejście studenta od komisji
            sem_v(sem_id, SEM_PRACTICAL_EXAM); // Zwolnienie miejsca w komisji po przekazaniu informacji o zaliczonym egzaminie praktycznym
        } else { 
            sem_v(sem_id, SEM_EXAM_A);  // Odejście studenta od komisji

            sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

            sem_p(sem_id, SEM_EXAM_A);  // Oczekiwanie na zwolnienie się komisji, aby odpowiedzieć na pytania

            // Odebranie oceny od komisji
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), student_data->pid, 0) == -1) {
                perror("Błąd odbierania komunikatu!\n");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EXAM_A); // Odejście studenta od komisji po zakończeniu egzaminu

            sem_v(sem_id, SEM_PRACTICAL_EXAM);  // Zwolnienie miejsca w komisji po zakończeniu egzaminu
        }

        // Egzamin teoretyczny dla studentów z pozytywną oceną z części praktycznej
        if (msg.grade_A >= 3.0) {

            sem_p(sem_id, SEM_THEORETICAL_EXAM); // Zajęcie miejsca w komisji (3 miejsca)

            sem_p(sem_id, SEM_EXAM_B);  // Podejście studenta do komisji

            struct message msg;

            // Wysyłanie PIDu studenta
            msg.msg_type = STUDENT_TO_COMMISSION_B;
            msg.pid = student_data->pid;
            if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Błąd wysyłania komunikatu!\n");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_COMMISSION_B); // Uruchomienie komisji (symulacja oczekiwania na pytania)

            sem_v(sem_id, SEM_EXAM_B);  // Odejście studenta od komisji

            sleep(PREPARE_TIME);  // Określony czas na przygotowanie się do odpowiedzi

            sem_p(sem_id, SEM_EXAM_B);  // Oczekiwanie na zwolnienie się komisji, aby odpowiedzieć na pytania

            // Odebranie oceny od komisji
            if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), student_data->pid, 0) == -1) {
                perror("Błąd odbierania komunikatu!\n");
                cleanup();
                exit(EXIT_FAILURE);
            }

            sem_v(sem_id, SEM_EXAM_B); // Odejście studenta od komisji po zakończeniu egzaminu

            sem_v(sem_id, SEM_THEORETICAL_EXAM);  // Zwolnienie miejsca w komisji po zakończeniu egzaminu
        }  
    } else {
        //printf("Student o PID %d z kierunku %d wraca do domu.\n", student_data->pid, student_data->major);
    }
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

    msg_id = msgget(key_msg, 0600 | IPC_CREAT);
    msg_dean_id = msgget(key_dean_msg, 0600 | IPC_CREAT);
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

    int num_majors = rand() % 11 + 5; // Losowanie liczby kierunków 1-15

    *shared_mem = num_majors;

    num_students = (int *)malloc(num_majors * sizeof(int)); // Tworzenie tablicy przechowującej liczbe studentów na poszczególnych kierunkach

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < num_majors; i++) {
        num_students[i] = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, num_students[i]);
    }   

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie procesów dla każdego studenta
    for (int i = 0; i < num_majors; i++) {
        for (int j = 1; j <= num_students[i]; j++) {
            //sleep(1);
            usleep(rand() % 50000);
            pid_t pid = fork();  
            if (pid == 0) {
                srand(time(NULL) ^ getpid());

                Student* student = (Student*)malloc(sizeof(Student));
                student->major = i + 1;
                student->pid = getpid(); // Ustawienie PID jako ID studenta

                student->is_exam_passed = (rand() % 100 < 5) ? 1 : 0; // Losowanie 5% studentów z zaliczonym egzaminem praktycznym

                simulate_student(student);

                // Sprawdzenie czy proces jest procesem wybranego kierunku
                if (i + 1 != announced_major) {
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
    while ((wait_status = wait(&exit_status)) > 0) {
        if (wait_status == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    sem_p(sem_id, SEM_ACTIVE_PROCESS);

    free(num_students);
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

void handle_signal(int sig) {
    exit(0);
}