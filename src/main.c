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
    size_t current_burst;
    long long ready_at;
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

static void run_fcfs(Process *processes, size_t count)
{
    long long now = 0;
    size_t finished = 0;

    for (size_t i = 0; i < count; i++) {
        processes[i].ready_at = processes[i].arrival;
    }

    while (finished < count) {
        Process *next = NULL;

        for (size_t i = 0; i < count; i++) {
            Process *p = &processes[i];
            if (p->current_burst == p->burst_count) {
                continue;
            }

            if (next == NULL || p->ready_at < next->ready_at ||
                (p->ready_at == next->ready_at && strcmp(p->pid, next->pid) < 0)) {
                next = p;
            }
        }

        /* If even the earliest process is not ready yet, the CPU is idle. */
        if (now < next->ready_at) {
            printf("| IDLE %lld-%lld ", now, next->ready_at);
            now = next->ready_at;
        }

        long long end = now + next->bursts[next->current_burst];
        printf("| %s %lld-%lld ", next->pid, now, end);
        now = end;
        next->current_burst++;

        if (next->current_burst == next->burst_count) {
            finished++;
        } else {
            int io_duration = next->bursts[next->current_burst];
            next->ready_at = now + io_duration;
            next->current_burst++;
        }
    }

    puts("|");
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

    run_fcfs(processes, count);
    status = 0;

cleanup:
    fclose(input);
    for (size_t i = 0; i < count; i++) {
        free(processes[i].bursts);
    }
    free(processes);
    return status;
}
