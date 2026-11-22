# MiniOS-Sim

MiniOS-Sim is a C11 command-line operating systems simulator for CEN 302.
The first milestone focuses on CPU scheduling. Later milestones cover thread
synchronization, deadlock handling, and memory management.

## Build and run

From the project directory on Ubuntu 24.04, run:

```bash
make
./minios
```

The current executable prints `MiniOS-Sim ready`.

## First scheduling example

The input file `tests/sched-one.txt` contains one process:

```text
P1 0 3 2
```

Process P1 arrives at tick 0 with priority 3 and needs 2 ticks of CPU time.
Under first-come, first-served (FCFS) scheduling, it should run from tick 0
to tick 2 with no waiting time. This is the first expected result to check
when the scheduler is implemented.

## M1 scope

- Implement FCFS, SJF, SRTF, preemptive priority with aging, and round robin.
- Produce Gantt charts, scheduling metrics, transition traces, and CSV output.
- Compare all five policies on the same workload and verify at least four tests.
