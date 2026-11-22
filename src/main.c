#include <stdio.h>
#include <string.h>

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
    if (fgets(line, sizeof line, input) == NULL) {
        fprintf(stderr, "Could not read the first input line\n");
        fclose(input);
        return 1;
    }

    fclose(input);
        char pid[32];
    int arrival, priority, cpu;
    char extra;

    if (sscanf(line, "%31s %d %d %d %c",
               pid, &arrival, &priority, &cpu, &extra) != 4) {
        fprintf(stderr, "Expected: PID ARRIVAL PRIORITY CPU\n");
        return 1;
    }

        if (arrival < 0 || cpu <= 0) {
        fprintf(stderr, "Expected a nonnegative arrival and a positive CPU burst\n");
        return 1;
    }

    long long finish = (long long)arrival + cpu;

    if (arrival > 0) {
        printf("| IDLE 0-%d ", arrival);
    }
    printf("| %s %d-%lld |\n", pid, arrival, finish);
    return 0;
}
