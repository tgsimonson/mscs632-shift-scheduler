# Employee Shift Scheduler: Go and C++

MSCS 632 Advanced Programming Languages. The same weekly shift scheduler
implemented twice, in Go and in C++, reading the same input file and producing
byte-identical output.

## Scheduling rules

- Seven days, three shifts per day (morning, afternoon, evening)
- No employee works more than one shift per day
- No employee works more than five days per week
- Every shift needs at least two employees
- A shift holds at most three employees, which is what makes a shift "full"

The prompt sets a minimum of two per shift but no maximum. A maximum is
required for "the preferred shift is full" to mean anything, so `MaxPerShift`
is set to 3 and named as a constant in both implementations.

## Algorithm

Four passes, in this order:

1. **Preferences, capped at the minimum.** Assign employees to their highest
   ranked available shift, but stop each shift at two. Filling shifts to
   capacity in a single pass exhausts the five-day cap early in the week and
   leaves the weekend unstaffed.
2. **Minimum staffing.** Any shift still below two gets a randomly chosen
   fill-in, drawn from the employees with the fewest days worked so far.
3. **Preferences, capped at the maximum.** Spend the remaining capacity on
   preferred shifts, up to three per shift.
4. **Conflict resolution.** An employee whose preferred shifts were all full
   is placed on another shift the same day, or failing that the next day. If
   both days are fully staffed the employee is off, which is reported as a
   no-vacancy outcome rather than a failure.

Within each pass, employees are taken least-loaded first. Taking them in file
order starves the back half of the week.

## Randomness

The prompt calls for random fill-in assignment. A standard library generator
would produce different schedules in each language and make the two outputs
impossible to compare, so both implementations use the same hand-written
linear congruential generator with the same constants and the same seed. The
assignment is random in the sense the prompt requires, and reproducible.

## Input format

`data/employees.txt`, one employee per line:

```
name,mon,tue,wed,thu,fri,sat,sun
```

Each day is a ranked preference list separated by `>`, best first. Use `-` for
a day the employee is unavailable.

```
Alice Nguyen,morning,morning>afternoon,morning,afternoon,morning,-,evening
```

Ranked preferences are the optional bonus requirement.

## Running

Both implementations, with an automatic output comparison:

```bash
./run_both.sh
```

Individually:

```bash
cd go  && go run . ../data/employees.txt
cd cpp && make run
```

## Layout

```
data/employees.txt   shared input, 12 employees
go/main.go           Go implementation
cpp/scheduler.cpp    C++ implementation
cpp/Makefile
run_both.sh          builds both, runs both, diffs the output
```

## Language features demonstrated

| | Go | C++ |
|---|---|---|
| Data structures | slices, maps, struct with array fields | `std::vector`, `std::array`, classes |
| Concurrency | three goroutines with `sync.WaitGroup` | three `std::thread` with `std::lock_guard` |
| Control flow | `for`/`range`, `switch` with no fallthrough | indexed and range-based `for`, `switch` |
| Error handling | multiple return values, explicit `err` checks | exceptions with `try`/`catch` |
| Memory | garbage collected | RAII, automatic storage, no manual `new` |

## Verified output

Both implementations produce 78 lines and 3,556 bytes, verified with `diff`.
