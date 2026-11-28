"""Run the M1 reference cases from the repository root."""
from pathlib import Path
import subprocess
import sys

cases = [
    ("FCFS I/O", "-a fcfs", "io", "io"),
    ("FCFS CSV", "-a fcfs --csv", "io", "io.csv"),
    ("FCFS transitions", "-a fcfs --trace", "io", "io.trace"),
    ("SJF selection", "-a sjf", "sjf", "sjf"),
    ("SRTF preemption", "-a srtf", "srtf", "srtf"),
    ("Priority preemption", "-a prio", "prio", "prio"),
    ("Priority aging", "-a prio --aging 2", "aging", "aging"),
    ("RR quantum boundary", "-a rr -q 2", "rr", "rr"),
    ("RR worked example", "-a rr -q 2", "rr-worked", "rr-worked"),
    ("Context-switch cost", "-a fcfs -c 1", "switch", "switch"),
    ("Five-policy comparison", "--compare -q 2", "rr-worked", "compare"),
]

failed = 0
for label, options, workload, reference in cases:
    command = ["./minios", "sched", *options.split(), f"tests/sched-{workload}.txt"]
    expected = Path(f"tests/sched-{reference}.expected").read_text(encoding="utf-8")
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        passed = result.returncode == 0 and result.stdout == expected and not result.stderr
        if not passed:
            print(f"Exit {result.returncode}\nExpected:\n{expected}Actual:\n{result.stdout}{result.stderr}")
    except (OSError, subprocess.TimeoutExpired) as error:
        print(error)
        passed = False
    print(f"{'PASS' if passed else 'FAIL'}: {label}")
    failed += not passed

print(f"{len(cases) - failed}/{len(cases)} passed")
sys.exit(1 if failed else 0)
