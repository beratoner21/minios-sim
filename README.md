# MiniOS-Sim

MiniOS-Sim is a C11 command-line operating systems simulator for CEN 302.
Milestone 1 implements CPU scheduling with a simulated clock, independent I/O
waits, and five scheduling policies.

## Build and test

On Ubuntu 24.04, run the following from the repository root. The simulator
builds with GCC and GNU Make. The test runner uses Python 3's standard library.

```bash
make
make test
```

The suite compares the simulator's output against the reference files in
`tests/`. It prints PASS or FAIL for each case and exits with an error if a case fails.

## Run a workload

Each non-comment row contains a PID, arrival time, priority, and an alternating
list of CPU and I/O durations. The first and last durations are CPU bursts.
Lines beginning with `#` are comments. Smaller priority values rank higher.

```bash
./minios sched -a fcfs tests/sched-io.txt
./minios sched -a sjf tests/sched-sjf.txt
./minios sched -a srtf tests/sched-srtf.txt
./minios sched -a prio --aging 2 tests/sched-aging.txt
./minios sched -a rr -q 2 tests/sched-rr-worked.txt
```

The default output includes the Gantt chart, a per-process timing table, and
summary statistics. Round Robin defaults to a four-tick quantum.

## Inspect and compare

```bash
./minios sched -a fcfs --trace tests/sched-io.txt
./minios sched -a fcfs --csv tests/sched-io.txt
./minios sched -a fcfs -c 1 tests/sched-switch.txt
./minios sched --compare -q 2 tests/sched-rr-worked.txt
```

Trace output records process state transitions. CSV output contains a header
and PID-sorted rows. A switch cost of one adds one idle tick on a direct
change between processes. Comparison mode runs all five policies on the same
workload and prints their aggregate metrics side by side.
