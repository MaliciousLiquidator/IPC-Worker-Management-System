#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#define MAX_NAME_LENGTH 100
#define MAX_APPLICANTS 100
#define MAX_WORKERS_PER_DAY 10
#define MAX_WORKERS_PER_BUS 5
#define NUM_BUSES 2

typedef struct {
    long mtype;  // message type
    int num_workers;
} Message;

typedef struct {
    char name[MAX_NAME_LENGTH];
    char days[64];
} Applicant;

int num_applicants = 0;
Applicant applicants[MAX_APPLICANTS];

void add_applicant();
void modify_applicant();
void delete_applicant();
void delete_all();
void display_applicants();
void display_applicants_by_day(char *day);
void create_worker_process(int bus_id, int start_index, int num_workers, int *current_index, int pipeline[2], int msgqid);
void start_day();
void load_applicants();
void save_applicants();
void create_applicant(char *name, char *days);
void signal_handler(int signum);

int msgqid;  // Message queue ID
int pipeline[NUM_BUSES][2];  // Pipeline for each bus

void add_applicant() {
    char name[MAX_NAME_LENGTH];
    char days[64];

    printf("Enter applicant name: ");
    scanf("%s", name);

    printf("Enter available days (separated by commas): ");
    scanf("%s", days);

    create_applicant(name, days);
}

void modify_applicant() {
    char name[MAX_NAME_LENGTH];
    char days[64];

    printf("Enter applicant name to modify: ");
    scanf("%s", name);

    int found = 0;
    for (int i = 0; i < num_applicants; i++) {
        if (strcmp(applicants[i].name, name) == 0) {
            found = 1;

            printf("Enter new available days (separated by commas): ");
            scanf("%s", days);

            strncpy(applicants[i].days, days, 64);

            printf("Applicant modified successfully.\n");
            printf("Updated applicants:\n");
            display_applicants();
            save_applicants();

            break;
        }
    }

    if (!found) {
        printf("Could not find applicant with name %s.\n", name);
    }
}

void delete_applicant() {
    char name[MAX_NAME_LENGTH];

    printf("Please enter the applicant's name to delete: ");
    scanf("%s", name);

    int found = 0;
    for (int i = 0; i < num_applicants; i++) {
        if (strcmp(applicants[i].name, name) == 0) {
            found = 1;
            for (int j = i; j < num_applicants - 1; j++) {
                applicants[j] = applicants[j + 1];
            }
            num_applicants--;
            break;
        }
    }

    if (found) {
        printf("Applicant deleted successfully.\n");
        printf("Updated applicants:\n");
        display_applicants();
        save_applicants();
    } else {
        printf("Could not find applicant with name %s.\n", name);
    }
}

void delete_all() {
    num_applicants = 0;
    printf("\nAll applicants have been deleted.\n");
    save_applicants();
}

void display_applicants() {
    printf("Name          Days\n");
    for (int i = 0; i < num_applicants; i++) {
        printf("%-14s%s\n", applicants[i].name, applicants[i].days);
    }
}

void display_applicants_by_day(char *day) {
    printf("\nDisplaying applicants available on %s:\n", day);
    printf("Name          Days\n");
    for (int i = 0; i < num_applicants; i++) {
        char temp_days[64];
        strcpy(temp_days, applicants[i].days);

        char *token = strtok(temp_days, ",");
        while (token != NULL) {
            if (strcmp(token, day) == 0) {
                printf("%-14s%s\n", applicants[i].name, applicants[i].days);
                break;
            }
            token = strtok(NULL, ",");
        }
    }
}

void create_worker_process(int bus_id, int start_index, int num_workers, int *current_index, int pipeline[2], int msgqid) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process (bus)
        printf("\nBus %d: Departing with workers: ", bus_id);
        for (int i = start_index; i < start_index + num_workers; i++) {
            printf("%s ", applicants[i].name);
        }
        printf("\n");

        // Send the number of workers to the parent process via pipeline
        close(pipeline[0]);
        write(pipeline[1], &num_workers, sizeof(int));
        close(pipeline[1]);

        // Send a signal to the parent process
        kill(getppid(), SIGUSR1);

        exit(0);
    } else if (pid > 0) {
        // Parent process (vineyard)
        // Wait for the signal from the child process
        pause();

        // Receive the number of workers from the child process via pipeline
        close(pipeline[1]);
        read(pipeline[0], &num_workers, sizeof(int));
        close(pipeline[0]);

        *current_index += num_workers;
    } else {
        perror("fork");
        exit(1);
    }
}

void start_day() {
    char day[20];
    printf("\nEnter the day to choose workers from: ");
    scanf("%s", day);

    int num_workers_needed = 0;

    // Count the number of workers needed for the chosen day
    for (int i = 0; i < num_applicants; i++) {
        char temp_days[64];
        strcpy(temp_days, applicants[i].days);

        char *token = strtok(temp_days, ",");
        while (token != NULL) {
            if (strcmp(token, day) == 0) {
                num_workers_needed++;
                break;
            }
            token = strtok(NULL, ",");
        }
    }

    if (num_workers_needed > MAX_WORKERS_PER_DAY) {
        printf("Error: Maximum number of workers per day exceeded.\n");
        return;
    }

    if (num_workers_needed == 0) {
        printf("Error: No workers available for the chosen day.\n");
        return;
    }

    // Determine the number of workers to choose from the selected day
    int num_workers_choose;
    printf("Enter the number of workers to choose from the selected day (0-%d): ", num_workers_needed);
    scanf("%d", &num_workers_choose);

    if (num_workers_choose > num_workers_needed) {
        printf("Error: Invalid number of workers to choose.\n");
        return;
    }

    // Determine the number of workers for Bus 1 and Bus 2
    int num_workers_bus1 = (num_workers_choose <= MAX_WORKERS_PER_BUS) ? num_workers_choose : MAX_WORKERS_PER_BUS;
    int num_workers_bus2 = num_workers_choose - num_workers_bus1;

    // Create the message queue
    key_t key = ftok(".", 'm');
    msgqid = msgget(key, IPC_CREAT | 0666);
    if (msgqid == -1) {
        perror("msgget");
        exit(1);
    }

    // Create pipelines for each bus
    for (int i = 0; i < NUM_BUSES; i++) {
        if (pipe(pipeline[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    // Register the signal handler for SIGUSR1
    signal(SIGUSR1, signal_handler);

    // Start the day by assigning workers to buses
    int current_index = 0;

    // Start Bus 1
    create_worker_process(1, current_index, num_workers_bus1, &current_index, pipeline[0], msgqid);

    // Start Bus 2 if needed
    if (num_workers_bus2 > 0) {
        create_worker_process(2, current_index, num_workers_bus2, &current_index, pipeline[1], msgqid);
    }

    // Wait for child processes to finish
    for (int i = 0; i < NUM_BUSES; i++) {
        wait(NULL);
    }

    // Remove the message queue
    if (msgctl(msgqid, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }

    // Close the pipelines
    for (int i = 0; i < NUM_BUSES; i++) {
        close(pipeline[i][0]);
        close(pipeline[i][1]);
    }

    printf("Day started successfully.\n");
}

void load_applicants() {
    FILE *file = fopen("applicants.txt", "r");
    if (file) {
        fscanf(file, "%d", &num_applicants); // Read the number of applicants from the file

        for (int i = 0; i < num_applicants; i++) {
            fscanf(file, "%s %s", applicants[i].name, applicants[i].days);
        }

        fclose(file);
    }
}

void save_applicants() {
    FILE *file = fopen("applicants.txt", "wb");
    if (file) {
        fwrite(&num_applicants, sizeof(int), 1, file);
        fwrite(&applicants, sizeof(Applicant), num_applicants, file);
        fclose(file);
    }
}

void create_applicant(char *name, char *days) {
    if (num_applicants >= MAX_APPLICANTS) {
        printf("Error: Maximum number of applicants exceeded.\n");
        return;
    }

    strcpy(applicants[num_applicants].name, name);
    strcpy(applicants[num_applicants].days, days);
    num_applicants++;

    printf("Applicant added successfully.\n");
    printf("Updated applicants:\n");
    display_applicants();
    save_applicants();
}

void signal_handler(int signum) {
    // Empty signal handler
}

int main() {
    load_applicants();

    int choice;
    do {
        printf("\n--- Vineyard Management System ---\n");
        printf("1. Add Applicant\n");
        printf("2. Modify Applicant\n");
        printf("3. Delete Applicant\n");
        printf("4. Delete All Applicants\n");
        printf("5. Display Applicants\n");
        printf("6. Display Applicants by Day\n");
        printf("7. Start Day\n");
        printf("0. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
            switch (choice) {
        case 1:
            add_applicant();
            break;
        case 2:
            modify_applicant();
            break;
        case 3:
            delete_applicant();
            break;
        case 4:
            delete_all();
            break;
        case 5:
            display_applicants();
            break;
        case 6: {
            char day[20];
            printf("Enter the day: ");
            scanf("%s", day);
            display_applicants_by_day(day);
            break;
        }
        case 7:
            start_day();
            break;
        case 0:
            printf("Exiting...\n");
            break;
        default:
            printf("Invalid choice. Please try again.\n");
            break;
    }
} while (choice != 0);

return 0;

}