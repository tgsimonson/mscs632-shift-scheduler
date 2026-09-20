// employee shift scheduler
// demonstrates go control structures: for/range loops, switch, labeled break,
// slices and maps, and goroutines with a waitgroup for the validation pass
package main

import (
	"bufio"
	"fmt"
	"os"
	"sort"
	"strings"
	"sync"
)

const (
	daysPerWeek  = 7
	shiftsPerDay = 3
	minPerShift  = 2 // company requirement: at least 2 employees per shift
	maxPerShift  = 3 // capacity that makes a shift "full"
	maxDaysAweek = 5 // no employee works more than 5 days
)

var dayNames = [daysPerWeek]string{
	"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday",
}

var shiftNames = [shiftsPerDay]string{"Morning", "Afternoon", "Evening"}

// shiftIndex converts a preference word into a shift slot
func shiftIndex(s string) int {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "morning":
		return 0
	case "afternoon":
		return 1
	case "evening":
		return 2
	default:
		return -1
	}
}

type employee struct {
	name string
	// prefs[day] holds ranked shift indices, best first; empty means unavailable
	prefs      [daysPerWeek][]int
	daysWorked int
	assignedOn [daysPerWeek]bool
}

// deterministic linear congruential generator
// hand-written so the c++ build produces an identical random sequence
type lcg struct{ state uint64 }

func newLCG(seed uint64) *lcg { return &lcg{state: seed} }

func (r *lcg) next() uint32 {
	r.state = r.state*6364136223846793005 + 1442695040888963407
	return uint32(r.state >> 33)
}

// intn returns a value in [0, n)
func (r *lcg) intn(n int) int {
	if n <= 0 {
		return 0
	}
	return int(r.next() % uint32(n))
}

type scheduler struct {
	employees []*employee
	// schedule[day][shift] holds employee indices
	schedule  [daysPerWeek][shiftsPerDay][]int
	conflicts []string
	warnings  []string
	rng       *lcg
}

func loadEmployees(path string) ([]*employee, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var list []*employee
	scanner := bufio.NewScanner(file)
	lineNo := 0

	for scanner.Scan() {
		lineNo++
		line := strings.TrimSpace(scanner.Text())
		// skip blanks and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		fields := strings.Split(line, ",")
		if len(fields) != daysPerWeek+1 {
			return nil, fmt.Errorf("line %d: expected %d fields, found %d",
				lineNo, daysPerWeek+1, len(fields))
		}

		emp := &employee{name: strings.TrimSpace(fields[0])}
		if emp.name == "" {
			return nil, fmt.Errorf("line %d: employee name is empty", lineNo)
		}

		for day := 0; day < daysPerWeek; day++ {
			entry := strings.TrimSpace(fields[day+1])
			if entry == "" || entry == "-" {
				continue // unavailable that day
			}
			for _, token := range strings.Split(entry, ">") {
				idx := shiftIndex(token)
				if idx < 0 {
					return nil, fmt.Errorf("line %d: unknown shift %q", lineNo, token)
				}
				emp.prefs[day] = append(emp.prefs[day], idx)
			}
		}
		list = append(list, emp)
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}
	if len(list) == 0 {
		return nil, fmt.Errorf("no employees found in %s", path)
	}
	return list, nil
}

// byLoad returns the indices of employees who can work the given day,
// ordered by days already worked. assigning the least-loaded employee first
// keeps weekly capacity in reserve for the back half of the week; taking
// employees in list order instead exhausts the early names by Friday and
// leaves the weekend unstaffed.
func (s *scheduler) byLoad(day int) []int {
	var idx []int
	for i, e := range s.employees {
		if s.canWork(e, day) {
			idx = append(idx, i)
		}
	}
	sort.SliceStable(idx, func(a, b int) bool {
		return s.employees[idx[a]].daysWorked < s.employees[idx[b]].daysWorked
	})
	return idx
}

func (s *scheduler) canWork(e *employee, day int) bool {
	return e.daysWorked < maxDaysAweek && !e.assignedOn[day]
}

func (s *scheduler) assign(empIdx, day, shift int) {
	e := s.employees[empIdx]
	s.schedule[day][shift] = append(s.schedule[day][shift], empIdx)
	e.assignedOn[day] = true
	e.daysWorked++
}

// assignPreferred honors ranked preferences up to the given capacity.
// it runs twice: first with the staffing minimum so coverage spreads across
// all seven days, then with the full capacity once every shift is covered.
// filling to capacity in a single pass exhausts the weekly day cap early in
// the week and leaves the last days unstaffed.
func (s *scheduler) assignPreferred(capacity int) {
	for day := 0; day < daysPerWeek; day++ {
		for _, i := range s.byLoad(day) {
			e := s.employees[i]
			if len(e.prefs[day]) == 0 {
				continue
			}
			for _, shift := range e.prefs[day] {
				if len(s.schedule[day][shift]) < capacity {
					s.assign(i, day, shift)
					break
				}
			}
		}
	}
}

// pass 2: place conflicted employees on any open shift that day, else the next day
func (s *scheduler) resolveConflicts() {
	for day := 0; day < daysPerWeek; day++ {
		for i, e := range s.employees {
			if !s.canWork(e, day) || len(e.prefs[day]) == 0 {
				continue
			}
			// employees placed on a preferred shift are already done
			if e.assignedOn[day] {
				continue
			}

			s.conflicts = append(s.conflicts, fmt.Sprintf(
				"%s: preferred shift full on %s", e.name, dayNames[day]))

			// try any shift on the same day
			for shift := 0; shift < shiftsPerDay; shift++ {
				if len(s.schedule[day][shift]) < maxPerShift {
					s.assign(i, day, shift)
					s.conflicts = append(s.conflicts, fmt.Sprintf(
						"  resolved: %s moved to %s %s",
						e.name, dayNames[day], shiftNames[shift]))
					break
				}
			}
			if e.assignedOn[day] {
				continue
			}

			// spill to the next day if one exists
			if day+1 < daysPerWeek && s.canWork(e, day+1) {
				for shift := 0; shift < shiftsPerDay; shift++ {
					if len(s.schedule[day+1][shift]) < maxPerShift {
						s.assign(i, day+1, shift)
						s.conflicts = append(s.conflicts, fmt.Sprintf(
							"  resolved: %s deferred to %s %s",
							e.name, dayNames[day+1], shiftNames[shift]))
						break
					}
				}
			}

			// every shift on both days is at capacity: the company needs no
			// more coverage, so the employee is simply off that day
			if !e.assignedOn[day] {
				s.conflicts = append(s.conflicts, fmt.Sprintf(
					"  no vacancy: %s and the following day are fully staffed, %s is off",
					dayNames[day], e.name))
			}
		}
	}
}

// pass 3: bring every shift up to the company minimum with random fill-ins
func (s *scheduler) enforceMinimum() {
	for day := 0; day < daysPerWeek; day++ {
		for shift := 0; shift < shiftsPerDay; shift++ {
			for len(s.schedule[day][shift]) < minPerShift {
				// random choice, drawn from the least-loaded tier so the
				// pick cannot starve a later day of coverage
				ordered := s.byLoad(day)
				var eligible []int
				if len(ordered) > 0 {
					lowest := s.employees[ordered[0]].daysWorked
					for _, i := range ordered {
						if s.employees[i].daysWorked == lowest {
							eligible = append(eligible, i)
						}
					}
				}
				if len(eligible) == 0 {
					s.warnings = append(s.warnings, fmt.Sprintf(
						"%s %s understaffed: %d of %d, no eligible employees remain",
						dayNames[day], shiftNames[shift],
						len(s.schedule[day][shift]), minPerShift))
					break
				}
				pick := eligible[s.rng.intn(len(eligible))]
				s.assign(pick, day, shift)
				s.conflicts = append(s.conflicts, fmt.Sprintf(
					"  filled: %s added to %s %s to meet the minimum",
					s.employees[pick].name, dayNames[day], shiftNames[shift]))
			}
		}
	}
}

// validate runs the three rule checks concurrently, one goroutine each
func (s *scheduler) validate() []string {
	var (
		mu      sync.Mutex
		wg      sync.WaitGroup
		results []string
	)

	report := func(msg string) {
		mu.Lock()
		results = append(results, msg)
		mu.Unlock()
	}

	wg.Add(3)

	// rule 1: no employee works more than one shift per day
	go func() {
		defer wg.Done()
		for day := 0; day < daysPerWeek; day++ {
			seen := make(map[int]bool)
			for shift := 0; shift < shiftsPerDay; shift++ {
				for _, idx := range s.schedule[day][shift] {
					if seen[idx] {
						report(fmt.Sprintf("VIOLATION: %s works twice on %s",
							s.employees[idx].name, dayNames[day]))
					}
					seen[idx] = true
				}
			}
		}
	}()

	// rule 2: no employee exceeds the weekly day cap
	go func() {
		defer wg.Done()
		for _, e := range s.employees {
			if e.daysWorked > maxDaysAweek {
				report(fmt.Sprintf("VIOLATION: %s works %d days, cap is %d",
					e.name, e.daysWorked, maxDaysAweek))
			}
		}
	}()

	// rule 3: every shift meets the staffing minimum
	go func() {
		defer wg.Done()
		for day := 0; day < daysPerWeek; day++ {
			for shift := 0; shift < shiftsPerDay; shift++ {
				if n := len(s.schedule[day][shift]); n < minPerShift {
					report(fmt.Sprintf("VIOLATION: %s %s has %d, minimum is %d",
						dayNames[day], shiftNames[shift], n, minPerShift))
				}
			}
		}
	}()

	wg.Wait()
	sort.Strings(results)
	return results
}

func (s *scheduler) printSchedule() {
	fmt.Println("========================================================================")
	fmt.Println("                      WEEKLY SHIFT SCHEDULE")
	fmt.Println("========================================================================")

	for day := 0; day < daysPerWeek; day++ {
		fmt.Printf("\n%s\n", dayNames[day])
		fmt.Println("------------------------------------------------------------------------")
		for shift := 0; shift < shiftsPerDay; shift++ {
			names := make([]string, 0, len(s.schedule[day][shift]))
			for _, idx := range s.schedule[day][shift] {
				names = append(names, s.employees[idx].name)
			}
			sort.Strings(names)
			joined := "(unstaffed)"
			if len(names) > 0 {
				joined = strings.Join(names, ", ")
			}
			fmt.Printf("  %-10s (%d) %s\n", shiftNames[shift], len(names), joined)
		}
	}

	fmt.Println("\n========================================================================")
	fmt.Println("                      DAYS WORKED PER EMPLOYEE")
	fmt.Println("========================================================================")

	sorted := make([]*employee, len(s.employees))
	copy(sorted, s.employees)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].name < sorted[j].name })

	for _, e := range sorted {
		bar := strings.Repeat("#", e.daysWorked)
		note := ""
		if e.daysWorked == maxDaysAweek {
			note = " (at weekly cap)"
		}
		fmt.Printf("  %-16s %d/%d %-5s%s\n", e.name, e.daysWorked, maxDaysAweek, bar, note)
	}
}

func (s *scheduler) printAdjustments() {
	fmt.Println("\n========================================================================")
	fmt.Println("                   CONFLICTS AND ADJUSTMENTS")
	fmt.Println("========================================================================")
	if len(s.conflicts) == 0 {
		fmt.Println("  none: every employee received a preferred shift")
		return
	}
	for _, c := range s.conflicts {
		fmt.Printf("  %s\n", c)
	}
}

func main() {
	path := "../data/employees.txt"
	if len(os.Args) > 1 {
		path = os.Args[1]
	}

	employees, err := loadEmployees(path)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}

	s := &scheduler{employees: employees, rng: newLCG(20260913)}

	// coverage first, then preferences, then conflicts
	s.assignPreferred(minPerShift)
	s.enforceMinimum()
	s.assignPreferred(maxPerShift)
	s.resolveConflicts()

	s.printSchedule()
	s.printAdjustments()

	fmt.Println("\n========================================================================")
	fmt.Println("                      RULE VALIDATION")
	fmt.Println("========================================================================")
	violations := s.validate()
	if len(violations) == 0 {
		fmt.Println("  all rules satisfied: 1 shift per day, 5 day cap, 2 per shift minimum")
	} else {
		for _, v := range violations {
			fmt.Printf("  %s\n", v)
		}
	}
	for _, w := range s.warnings {
		fmt.Printf("  WARNING: %s\n", w)
	}
}
