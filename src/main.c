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
    int remaining;
    long long completion;
    long long waiting;
    long long response;
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

static int compare_pid(const void *left, const void *right)
{
    const Process *a = left;
    const Process *b = right;
    return strcmp(a->pid, b->pid);
}

typedef enum { FCFS, SJF, SRTF } Policy;

static int comes_first(const Process *a, const Process *b,
                       long long now, Policy policy)
{
    int a_ready = a->ready_at <= now;
    int b_ready = b->ready_at <= now;

    if (a_ready != b_ready) {
        return a_ready;
    }

    if (a_ready && policy != FCFS) {
        int a_cpu = a->remaining;
        int b_cpu = b->remaining;
        if (a_cpu != b_cpu) {
            return a_cpu < b_cpu;
        }
    }

    if (a->ready_at != b->ready_at) {
        return a->ready_at < b->ready_at;
    }
    return strcmp(a->pid, b->pid) < 0;
}

/* A new arrival or I/O return can preempt only with strictly less work. */
static long long srtf_end(const Process *processes, size_t count,
                          const Process *running, long long now)
{
    long long finish = now + running->remaining;
    long long end = finish;
    for (size_t i = 0; i < count; i++) {
        const Process *p = &processes[i];
        if (p->current_burst == p->burst_count ||
            p->ready_at <= now || p->ready_at >= end) {
            continue;
        }
        if (p->remaining < finish - p->ready_at) {
            end = p->ready_at;
        }
    }
    return end;
}

static void run_schedule(Process *processes, size_t count, int csv, Policy policy)
{
    long long now = 0;
    size_t finished = 0;

    for (size_t i = 0; i < count; i++) {
        processes[i].ready_at = processes[i].arrival;
        processes[i].remaining = processes[i].bursts[0];
        processes[i].response = -1;
    }

    while (finished < count) {
        Process *next = NULL;

        for (size_t i = 0; i < count; i++) {
            Process *p = &processes[i];
            if (p->current_burst == p->burst_count) {
                continue;
            }

            if (next == NULL || comes_first(p, next, now, policy)) {
                next = p;
            }
        }

        /* If even the earliest process is not ready yet, the CPU is idle. */
        if (now < next->ready_at) {
            if (!csv) {
                printf("| IDLE %lld-%lld ", now, next->ready_at);
            }
            now = next->ready_at;
            continue;
        }

        next->waiting += now - next->ready_at;
        if (next->response < 0) {
            next->response = now - next->arrival;
        }

        long long end = now + next->remaining;
        if (policy == SRTF) {
            end = srtf_end(processes, count, next, now);
        }
        if (!csv) {
            printf("| %s %lld-%lld ", next->pid, now, end);
        }
        next->remaining -= (int)(end - now);
        now = end;
        if (next->remaining > 0) {
            next->ready_at = now;
            continue;
        }
        next->current_burst++;

        if (next->current_burst == next->burst_count) {
            next->completion = now;
            finished++;
        } else {
            int io_duration = next->bursts[next->current_burst];
            next->ready_at = now + io_duration;
            next->current_burst++;
            next->remaining = next->bursts[next->current_burst];
        }
    }

    if (!csv) {
        puts("|");
        return;
    }

    qsort(processes, count, sizeof *processes, compare_pid);
    puts("pid,arrival,completion,turnaround,waiting,response");
    for (size_t i = 0; i < count; i++) {
        const Process *p = &processes[i];
        printf("%s,%d,%lld,%lld,%lld,%lld\n",
               p->pid, p->arrival, p->completion,
               p->completion - p->arrival, p->waiting, p->response);
    }
}

int main(int argc, char *argv[])
{
    int csv = argc == 6 && strcmp(argv[4], "--csv") == 0;
    if ((argc != 5 && !csv) || strcmp(argv[1], "sched") != 0 ||
        strcmp(argv[2], "-a") != 0 || (strcmp(argv[3], "fcfs") != 0 && strcmp(argv[3], "sjf") != 0 &&
                                       strcmp(argv[3], "srtf") != 0)) {
        fprintf(stderr, "Usage: ./minios sched -a fcfs|sjf|srtf [--csv] FILE\n");
        return 1;
    }

    const char *filename = argv[argc - 1];
    FILE *input = fopen(filename, "r");
    if (input == NULL) {
        perror(filename);
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

    Policy policy = strcmp(argv[3], "sjf") == 0 ? SJF : FCFS;
    if (strcmp(argv[3], "srtf") == 0) {
        policy = SRTF;
    }
    run_schedule(processes, count, csv, policy);
    status = 0;

cleanup:
    fclose(input);
    for (size_t i = 0; i < count; i++) {
        free(processes[i].bursts);
    }
    free(processes);
    return status;
}
