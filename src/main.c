#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { NEW, READY, RUNNING, WAITING, TERMINATED } ProcessState;

typedef struct {
    ProcessState state;
    char pid[32];
    int arrival;
    int priority;
    int *bursts;
    size_t burst_count;
    size_t current_burst;
    long long ready_at;
    int remaining;
    int quantum_expired;
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

typedef enum { FCFS, SJF, SRTF, PRIO, RR } Policy;

static int effective_priority(const Process *p, long long waited, int aging)
{
    long long levels = aging ? waited / aging : 0;
    return levels >= p->priority ? 0 : p->priority - (int)levels;
}

static int comes_first(const Process *a, const Process *b,
                       long long now, Policy policy, int aging)
{
    int a_ready = a->ready_at <= now;
    int b_ready = b->ready_at <= now;

    if (a_ready != b_ready) {
        return a_ready;
    }

    if (a_ready && policy == PRIO) {
        int pa = effective_priority(a, a->waiting + (now - a->ready_at), aging);
        int pb = effective_priority(b, b->waiting + (now - b->ready_at), aging);
        if (pa != pb) {
            return pa < pb;
        }
    }

    if (a_ready && (policy == SJF || policy == SRTF)) {
        int a_cpu = a->remaining;
        int b_cpu = b->remaining;
        if (a_cpu != b_cpu) {
            return a_cpu < b_cpu;
        }
    }

    if (a->ready_at != b->ready_at) {
        return a->ready_at < b->ready_at;
    }
    /* Same-tick arrivals and I/O returns precede the expired runner. */
    if (policy == RR && a->quantum_expired != b->quantum_expired) {
        return !a->quantum_expired;
    }
    return strcmp(a->pid, b->pid) < 0;
}

static long long preemption_end(const Process *processes, size_t count,
                                const Process *running, long long now,
                                Policy policy, int aging)
{
    long long finish = now + running->remaining;
    long long end = finish;
    int rank = effective_priority(running, running->waiting, aging);

    for (size_t i = 0; i < count; i++) {
        const Process *p = &processes[i];
        if (p == running || p->current_burst == p->burst_count || p->ready_at >= end) {
            continue;
        }

        long long at = p->ready_at;
        if (policy == SRTF) {
            if (at <= now || p->remaining >= finish - at) {
                continue;
            }
        } else if (effective_priority(p, p->waiting, aging) >= rank) {
            if (!aging || rank == 0) {
                continue;
            }
            /* First tick at which this process strictly outranks the runner. */
            long long required_wait = ((long long)p->priority - rank + 1) * aging;
            long long delay = required_wait - p->waiting;
            if (delay >= end - at) {
                continue;
            }
            at += delay;
        }
        if (at > now && at < end) {
            end = at;
        }
    }
    return end;
}

static void transition(FILE *trace, Process *p, ProcessState state, long long at)
{
    static const char *const names[] = {"NEW", "READY", "RUNNING", "WAITING", "TERMINATED"};
    if (trace) {
        fprintf(trace, "t=%lld %s %s -> %s\n", at, p->pid, names[p->state], names[state]);
    }
    p->state = state;
}

/* The scheduler jumps between CPU events. Report intervening arrivals and
   I/O completions in timestamp order, with PID breaking simultaneous ties. */
static void admit_until(FILE *trace, Process *processes, size_t count, long long until)
{
    for (;;) {
        Process *first = NULL;
        for (size_t i = 0; i < count; i++) {
            Process *p = &processes[i];
            if ((p->state != NEW && p->state != WAITING) || p->ready_at > until) {
                continue;
            }
            if (!first || p->ready_at < first->ready_at ||
                (p->ready_at == first->ready_at && strcmp(p->pid, first->pid) < 0)) {
                first = p;
            }
        }
        if (!first) {
            return;
        }
        transition(trace, first, READY, first->ready_at);
    }
}

static void run_schedule(Process *processes, size_t count, int csv, Policy policy, int aging, int quantum, int switch_cost, FILE *trace)
{
    long long now = 0;
    size_t finished = 0;
    const Process *previous = NULL;
    long long switches = 0;
    long long busy = 0;

    for (size_t i = 0; i < count; i++) {
        processes[i].state = NEW;
        processes[i].current_burst = 0;
        processes[i].waiting = 0;
        processes[i].quantum_expired = 0;
        processes[i].ready_at = processes[i].arrival;
        processes[i].remaining = processes[i].bursts[0];
        processes[i].response = -1;
    }

    while (finished < count) {
        admit_until(trace, processes, count, now);
        Process *next = NULL;

        for (size_t i = 0; i < count; i++) {
            Process *p = &processes[i];
            if (p->current_burst == p->burst_count) {
                continue;
            }

            if (next == NULL || comes_first(p, next, now, policy, aging)) {
                next = p;
            }
        }

        /* If even the earliest process is not ready yet, the CPU is idle. */
        if (now < next->ready_at) {
            if (!csv && !trace) {
                printf("| IDLE %lld-%lld ", now, next->ready_at);
            }
            previous = NULL;
            now = next->ready_at;
            continue;
        }

        if (previous && previous != next) {
            switches++;
            if (switch_cost > 0) {
                if (!csv && !trace) {
                    printf("| IDLE %lld-%lld ", now, now + switch_cost);
                }
                now += switch_cost;
                /* The dispatch decision uses all arrivals during the delay. */
                previous = NULL;
                continue;
            }
        }
        previous = next;
        transition(trace, next, RUNNING, now);
        next->quantum_expired = 0;
        next->waiting += now - next->ready_at;
        if (next->response < 0) {
            next->response = now - next->arrival;
        }

        long long end = now + next->remaining;
        if (policy == RR && next->remaining > quantum) {
            end = now + quantum;
            next->quantum_expired = 1;
        } else if (policy == SRTF || policy == PRIO) {
            end = preemption_end(processes, count, next, now, policy, aging);
        }
        if (!csv && !trace) {
            printf("| %s %lld-%lld ", next->pid, now, end);
        }
        busy += end - now;
        next->remaining -= (int)(end - now);
        now = end;
        admit_until(trace, processes, count, now);
        if (next->remaining > 0) {
            transition(trace, next, READY, now);
            next->ready_at = now;
            continue;
        }
        next->current_burst++;

        if (next->current_burst == next->burst_count) {
            transition(trace, next, TERMINATED, now);
            next->completion = now;
            finished++;
        } else {
            transition(trace, next, WAITING, now);
            int io_duration = next->bursts[next->current_burst];
            next->ready_at = now + io_duration;
            next->current_burst++;
            next->remaining = next->bursts[next->current_burst];
        }
    }

    if (trace) {
        return;
    }
    if (!csv) {
        puts("|");
    }
    qsort(processes, count, sizeof *processes, compare_pid);
    puts(csv ? "pid,arrival,completion,turnaround,waiting,response"
             : "PID Arrival Completion Turnaround Waiting Response");
    double turnaround = 0, waiting = 0, response = 0;
    for (size_t i = 0; i < count; i++) {
        const Process *p = &processes[i];
        long long elapsed = p->completion - p->arrival;
        printf(csv ? "%s,%d,%lld,%lld,%lld,%lld\n" : "%s %d %lld %lld %lld %lld\n",
               p->pid, p->arrival, p->completion, elapsed, p->waiting, p->response);
        turnaround += (double)elapsed;
        waiting += (double)p->waiting;
        response += (double)p->response;
    }
    if (!csv) {
        printf("Average turnaround: %.2f\nAverage waiting: %.2f\nAverage response: %.2f\n",
               turnaround / count, waiting / count, response / count);
        printf("CPU utilization: %.2f%%\n", 100.0 * busy / now);
        printf("Throughput (processes per 100 ticks): %.2f\n", 100.0 * count / now);
        printf("Context switches: %lld\n", switches);
    }
}

int main(int argc, char *argv[])
{
    int csv = 0;
    int show_trace = 0;
    int aging = 0;
    int quantum = 0;
    int switch_cost = -1;
    if (argc < 5 || strcmp(argv[1], "sched") != 0 || strcmp(argv[2], "-a") != 0 ||
        (strcmp(argv[3], "fcfs") && strcmp(argv[3], "sjf") &&
         strcmp(argv[3], "srtf") && strcmp(argv[3], "prio") && strcmp(argv[3], "rr"))) {
        fprintf(stderr, "Usage: ./minios sched -a POLICY [-q QUANTUM] [-c COST] [--aging N] [--trace] [--csv] FILE\n");
        return 1;
    }
    for (int i = 4; i < argc - 1; i++) {
        if (strcmp(argv[i], "--csv") == 0 && !csv) {
            csv = 1;
        } else if (strcmp(argv[i], "-c") == 0 && switch_cost < 0 &&
                   i + 1 < argc - 1 && read_number(argv[i + 1], 0, &switch_cost)) {
            i++;
        } else if (strcmp(argv[i], "--trace") == 0 && !show_trace) {
            show_trace = 1;
        } else if (strcmp(argv[i], "-q") == 0 && !quantum &&
                   strcmp(argv[3], "rr") == 0 && i + 1 < argc - 1 &&
                   read_number(argv[i + 1], 1, &quantum)) {
            i++;
        } else if (strcmp(argv[i], "--aging") == 0 && !aging &&
                   strcmp(argv[3], "prio") == 0 && i + 1 < argc - 1 &&
                   read_number(argv[i + 1], 1, &aging)) {
            i++;
        } else {
            fprintf(stderr, "Invalid scheduler option\n");
            return 1;
        }
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
    } else if (strcmp(argv[3], "prio") == 0) {
        policy = PRIO;
    } else if (strcmp(argv[3], "rr") == 0) {
        policy = RR;
    }
    if (show_trace && !csv) {
        run_schedule(processes, count, 0, policy, aging, quantum ? quantum : 4, switch_cost, stdout);
    }
    run_schedule(processes, count, csv, policy, aging, quantum ? quantum : 4, switch_cost, NULL);
    status = 0;

cleanup:
    fclose(input);
    for (size_t i = 0; i < count; i++) {
        free(processes[i].bursts);
    }
    free(processes);
    return status;
}
