#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char pid[32];
    int arrival;
    int priority;
    int *bursts;
    size_t burst_count;
} Process;

static int read_number(const char *text, int minimum, int *result)
{
    if (text == NULL) {
        return 0;
    }

    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (end == text || *end != '\0' || errno == ERANGE ||
        value < minimum || value > INT_MAX) {
        return 0;
    }

    *result = (int)value;
    return 1;
}

static int parse_process(char *line, Process *process)
{
    const char *spaces = " \t\r\n";
    char *pid = strtok(line, spaces);

    if (pid == NULL || strlen(pid) >= sizeof process->pid) {
        return 0;
    }
    strcpy(process->pid, pid);

    if (!read_number(strtok(NULL, spaces), 0, &process->arrival) ||
        !read_number(strtok(NULL, spaces), 0, &process->priority)) {
        return 0;
    }

    for (char *token = strtok(NULL, spaces); token != NULL;
         token = strtok(NULL, spaces)) {
        int duration;
        if (!read_number(token, 1, &duration)) {
            return 0;
        }

        int *updated = realloc(process->bursts,
                               (process->burst_count + 1) * sizeof *updated);
        if (updated == NULL) {
            return 0;
        }

        process->bursts = updated;
        process->bursts[process->burst_count++] = duration;
    }

    /* CPU, I/O, CPU, ... must start and end with CPU. */
    return process->burst_count % 2 == 1;
}

int main(int argc, char *argv[])
{
    if (argc != 5 || strcmp(argv[1], "sched") != 0 ||
        strcmp(argv[2], "-a") != 0 || strcmp(argv[3], "fcfs") != 0) {
        fprintf(stderr, "Usage: ./minios sched -a fcfs FILE\n");
        return 1;
    }

    FILE *input = fopen(argv[4], "r");
    if (input == NULL) {
        perror(argv[4]);
        return 1;
    }

    char line[256];
    Process *processes = NULL;
    size_t count = 0;
    int status = 1;

    while (fgets(line, sizeof line, input) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(input)) {
            fprintf(stderr, "Input row too long\n");
            goto cleanup;
        }

        char *row = line;
        while (isspace((unsigned char)*row)) {
            row++;
        }
        if (*row == '#' || *row == '\0') {
            continue;
        }

        Process process = {0};
        if (!parse_process(row, &process)) {
            free(process.bursts);
            fprintf(stderr, "Invalid process row\n");
            goto cleanup;
        }

        Process *updated = realloc(processes, (count + 1) * sizeof *updated);
        if (updated == NULL) {
            free(process.bursts);
            fprintf(stderr, "Could not allocate process list\n");
            goto cleanup;
        }
        processes = updated;
        processes[count++] = process;
    }

    if (ferror(input) || count == 0) {
        fprintf(stderr, "Could not read workload\n");
        goto cleanup;
    }

    if (count == 1 && processes[0].burst_count == 1) {
        const Process *p = &processes[0];
        long long finish = (long long)p->arrival + p->bursts[0];
        if (p->arrival > 0) {
            printf("| IDLE 0-%d ", p->arrival);
        }
        printf("| %s %d-%lld |\n", p->pid, p->arrival, finish);
    } else {
        /* Show the parsed workload until the FCFS simulation is connected. */
        for (size_t i = 0; i < count; i++) {
            const Process *p = &processes[i];
            printf("%s bursts:", p->pid);
            for (size_t j = 0; j < p->burst_count; j++) {
                printf(" %d", p->bursts[j]);
            }
            putchar('\n');
        }
    }
    status = 0;

cleanup:
    fclose(input);
    for (size_t i = 0; i < count; i++) {
        free(processes[i].bursts);
    }
    free(processes);
    return status;
}
