// employee shift scheduler
// demonstrates c++ control structures: range-based and indexed for loops,
// switch, STL containers and algorithms, RAII, and std::thread for the
// validation pass
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kDaysPerWeek  = 7;
constexpr int kShiftsPerDay = 3;
constexpr int kMinPerShift  = 2;  // company requirement: at least 2 per shift
constexpr int kMaxPerShift  = 3;  // capacity that makes a shift "full"
constexpr int kMaxDaysAWeek = 5;  // no employee works more than 5 days

const std::array<std::string, kDaysPerWeek> kDayNames = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

const std::array<std::string, kShiftsPerDay> kShiftNames = {
    "Morning", "Afternoon", "Evening"};

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) parts.push_back(item);
    return parts;
}

// shiftIndex converts a preference word into a shift slot
int shiftIndex(const std::string& raw) {
    const std::string s = lower(trim(raw));
    if (s == "morning")   return 0;
    if (s == "afternoon") return 1;
    if (s == "evening")   return 2;
    return -1;
}

struct Employee {
    std::string name;
    // prefs[day] holds ranked shift indices, best first; empty means unavailable
    std::array<std::vector<int>, kDaysPerWeek> prefs;
    int  daysWorked = 0;
    std::array<bool, kDaysPerWeek> assignedOn = {};
};

// deterministic linear congruential generator
// identical constants to the go build so both produce the same sequence
class Lcg {
public:
    explicit Lcg(unsigned long long seed) : state_(seed) {}

    unsigned int next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<unsigned int>(state_ >> 33);
    }

    // intn returns a value in [0, n)
    int intn(int n) {
        if (n <= 0) return 0;
        return static_cast<int>(next() % static_cast<unsigned int>(n));
    }

private:
    unsigned long long state_;
};

class Scheduler {
public:
    Scheduler(std::vector<Employee> employees, unsigned long long seed)
        : employees_(std::move(employees)), rng_(seed) {}

    void run() {
        // coverage first, then preferences, then conflicts
        assignPreferred(kMinPerShift);
        enforceMinimum();
        assignPreferred(kMaxPerShift);
        resolveConflicts();
    }

    void printSchedule() const;
    void printAdjustments() const;
    std::vector<std::string> validate() const;
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    bool canWork(const Employee& e, int day) const {
        return e.daysWorked < kMaxDaysAWeek && !e.assignedOn[day];
    }

    // byLoad returns the indices of employees who can work the given day,
    // ordered by days already worked. assigning the least-loaded employee
    // first keeps weekly capacity in reserve for the back half of the week;
    // taking employees in list order instead exhausts the early names by
    // Friday and leaves the weekend unstaffed.
    std::vector<int> byLoad(int day) const {
        std::vector<int> idx;
        for (std::size_t i = 0; i < employees_.size(); ++i) {
            if (canWork(employees_[i], day)) idx.push_back(static_cast<int>(i));
        }
        std::stable_sort(idx.begin(), idx.end(), [this](int a, int b) {
            return employees_[a].daysWorked < employees_[b].daysWorked;
        });
        return idx;
    }

    void assign(int empIdx, int day, int shift) {
        schedule_[day][shift].push_back(empIdx);
        employees_[empIdx].assignedOn[day] = true;
        employees_[empIdx].daysWorked++;
    }

    // assignPreferred honors ranked preferences up to the given capacity.
    // it runs twice: first with the staffing minimum so coverage spreads
    // across all seven days, then with the full capacity once every shift
    // is covered.
    void assignPreferred(int capacity) {
        for (int day = 0; day < kDaysPerWeek; ++day) {
            for (int i : byLoad(day)) {
                const Employee& e = employees_[i];
                if (e.prefs[day].empty()) continue;
                for (int shift : e.prefs[day]) {
                    if (static_cast<int>(schedule_[day][shift].size()) < capacity) {
                        assign(i, day, shift);
                        break;
                    }
                }
            }
        }
    }

    // place conflicted employees on any open shift that day, else the next day
    void resolveConflicts() {
        for (int day = 0; day < kDaysPerWeek; ++day) {
            for (std::size_t i = 0; i < employees_.size(); ++i) {
                Employee& e = employees_[i];
                if (!canWork(e, day) || e.prefs[day].empty()) continue;

                // employees placed on a preferred shift are already done
                if (e.assignedOn[day]) continue;

                conflicts_.push_back(e.name + ": preferred shift full on " +
                                     kDayNames[day]);

                // try any shift on the same day
                for (int shift = 0; shift < kShiftsPerDay; ++shift) {
                    if (static_cast<int>(schedule_[day][shift].size()) < kMaxPerShift) {
                        assign(static_cast<int>(i), day, shift);
                        conflicts_.push_back("  resolved: " + e.name + " moved to " +
                                             kDayNames[day] + " " + kShiftNames[shift]);
                        break;
                    }
                }
                if (e.assignedOn[day]) continue;

                // spill to the next day if one exists
                if (day + 1 < kDaysPerWeek && canWork(e, day + 1)) {
                    for (int shift = 0; shift < kShiftsPerDay; ++shift) {
                        if (static_cast<int>(schedule_[day + 1][shift].size()) < kMaxPerShift) {
                            assign(static_cast<int>(i), day + 1, shift);
                            conflicts_.push_back("  resolved: " + e.name + " deferred to " +
                                                 kDayNames[day + 1] + " " + kShiftNames[shift]);
                            break;
                        }
                    }
                }

                // every shift on both days is at capacity: the company needs
                // no more coverage, so the employee is simply off that day
                if (!e.assignedOn[day]) {
                    conflicts_.push_back("  no vacancy: " + kDayNames[day] +
                                         " and the following day are fully staffed, " +
                                         e.name + " is off");
                }
            }
        }
    }

    // bring every shift up to the company minimum with random fill-ins
    void enforceMinimum() {
        for (int day = 0; day < kDaysPerWeek; ++day) {
            for (int shift = 0; shift < kShiftsPerDay; ++shift) {
                while (static_cast<int>(schedule_[day][shift].size()) < kMinPerShift) {
                    // random choice, drawn from the least-loaded tier so the
                    // pick cannot starve a later day of coverage
                    const std::vector<int> ordered = byLoad(day);
                    std::vector<int> eligible;
                    if (!ordered.empty()) {
                        const int lowest = employees_[ordered[0]].daysWorked;
                        for (int i : ordered) {
                            if (employees_[i].daysWorked == lowest) eligible.push_back(i);
                        }
                    }
                    if (eligible.empty()) {
                        warnings_.push_back(
                            kDayNames[day] + " " + kShiftNames[shift] +
                            " understaffed: " +
                            std::to_string(schedule_[day][shift].size()) + " of " +
                            std::to_string(kMinPerShift) +
                            ", no eligible employees remain");
                        break;
                    }
                    const int pick = eligible[rng_.intn(static_cast<int>(eligible.size()))];
                    assign(pick, day, shift);
                    conflicts_.push_back("  filled: " + employees_[pick].name +
                                         " added to " + kDayNames[day] + " " +
                                         kShiftNames[shift] + " to meet the minimum");
                }
            }
        }
    }

    std::vector<Employee> employees_;
    std::array<std::array<std::vector<int>, kShiftsPerDay>, kDaysPerWeek> schedule_;
    std::vector<std::string> conflicts_;
    std::vector<std::string> warnings_;
    Lcg rng_;
};

void Scheduler::printSchedule() const {
    std::cout << "========================================================================\n";
    std::cout << "                      WEEKLY SHIFT SCHEDULE\n";
    std::cout << "========================================================================\n";

    for (int day = 0; day < kDaysPerWeek; ++day) {
        std::cout << "\n" << kDayNames[day] << "\n";
        std::cout << "------------------------------------------------------------------------\n";
        for (int shift = 0; shift < kShiftsPerDay; ++shift) {
            std::vector<std::string> names;
            for (int idx : schedule_[day][shift]) names.push_back(employees_[idx].name);
            std::sort(names.begin(), names.end());

            std::string joined = "(unstaffed)";
            if (!names.empty()) {
                joined.clear();
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (i > 0) joined += ", ";
                    joined += names[i];
                }
            }
            std::cout << "  " << std::left << std::setw(10) << kShiftNames[shift]
                      << " (" << names.size() << ") " << joined << "\n";
        }
    }

    std::cout << "\n========================================================================\n";
    std::cout << "                      DAYS WORKED PER EMPLOYEE\n";
    std::cout << "========================================================================\n";

    std::vector<const Employee*> sorted;
    for (const Employee& e : employees_) sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(),
              [](const Employee* a, const Employee* b) { return a->name < b->name; });

    for (const Employee* e : sorted) {
        const std::string bar(static_cast<std::size_t>(e->daysWorked), '#');
        const std::string note = (e->daysWorked == kMaxDaysAWeek) ? " (at weekly cap)" : "";
        std::cout << "  " << std::left << std::setw(16) << e->name << " "
                  << e->daysWorked << "/" << kMaxDaysAWeek << " "
                  << std::left << std::setw(5) << bar << note << "\n";
    }
}

void Scheduler::printAdjustments() const {
    std::cout << "\n========================================================================\n";
    std::cout << "                   CONFLICTS AND ADJUSTMENTS\n";
    std::cout << "========================================================================\n";
    if (conflicts_.empty()) {
        std::cout << "  none: every employee received a preferred shift\n";
        return;
    }
    for (const std::string& c : conflicts_) std::cout << "  " << c << "\n";
}

// validate runs the three rule checks concurrently, one thread each
std::vector<std::string> Scheduler::validate() const {
    std::mutex mu;
    std::vector<std::string> results;

    auto report = [&mu, &results](const std::string& msg) {
        std::lock_guard<std::mutex> lock(mu);
        results.push_back(msg);
    };

    // rule 1: no employee works more than one shift per day
    std::thread t1([this, &report] {
        for (int day = 0; day < kDaysPerWeek; ++day) {
            std::vector<bool> seen(employees_.size(), false);
            for (int shift = 0; shift < kShiftsPerDay; ++shift) {
                for (int idx : schedule_[day][shift]) {
                    if (seen[idx]) {
                        report("VIOLATION: " + employees_[idx].name +
                               " works twice on " + kDayNames[day]);
                    }
                    seen[idx] = true;
                }
            }
        }
    });

    // rule 2: no employee exceeds the weekly day cap
    std::thread t2([this, &report] {
        for (const Employee& e : employees_) {
            if (e.daysWorked > kMaxDaysAWeek) {
                report("VIOLATION: " + e.name + " works " +
                       std::to_string(e.daysWorked) + " days, cap is " +
                       std::to_string(kMaxDaysAWeek));
            }
        }
    });

    // rule 3: every shift meets the staffing minimum
    std::thread t3([this, &report] {
        for (int day = 0; day < kDaysPerWeek; ++day) {
            for (int shift = 0; shift < kShiftsPerDay; ++shift) {
                const int n = static_cast<int>(schedule_[day][shift].size());
                if (n < kMinPerShift) {
                    report("VIOLATION: " + kDayNames[day] + " " + kShiftNames[shift] +
                           " has " + std::to_string(n) + ", minimum is " +
                           std::to_string(kMinPerShift));
                }
            }
        }
    });

    t1.join();
    t2.join();
    t3.join();
    std::sort(results.begin(), results.end());
    return results;
}

std::vector<Employee> loadEmployees(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);

    std::vector<Employee> list;
    std::string line;
    int lineNo = 0;

    while (std::getline(file, line)) {
        ++lineNo;
        const std::string trimmed = trim(line);
        // skip blanks and comments
        if (trimmed.empty() || trimmed[0] == '#') continue;

        const std::vector<std::string> fields = split(trimmed, ',');
        if (static_cast<int>(fields.size()) != kDaysPerWeek + 1) {
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": expected " + std::to_string(kDaysPerWeek + 1) +
                                     " fields, found " + std::to_string(fields.size()));
        }

        Employee emp;
        emp.name = trim(fields[0]);
        if (emp.name.empty()) {
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": employee name is empty");
        }

        for (int day = 0; day < kDaysPerWeek; ++day) {
            const std::string entry = trim(fields[day + 1]);
            if (entry.empty() || entry == "-") continue;  // unavailable that day
            for (const std::string& token : split(entry, '>')) {
                const int idx = shiftIndex(token);
                if (idx < 0) {
                    throw std::runtime_error("line " + std::to_string(lineNo) +
                                             ": unknown shift \"" + trim(token) + "\"");
                }
                emp.prefs[day].push_back(idx);
            }
        }
        list.push_back(std::move(emp));
    }

    if (list.empty()) throw std::runtime_error("no employees found in " + path);
    return list;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = "../data/employees.txt";
    if (argc > 1) path = argv[1];

    try {
        Scheduler s(loadEmployees(path), 20260913ULL);
        s.run();

        s.printSchedule();
        s.printAdjustments();

        std::cout << "\n========================================================================\n";
        std::cout << "                      RULE VALIDATION\n";
        std::cout << "========================================================================\n";

        const std::vector<std::string> violations = s.validate();
        if (violations.empty()) {
            std::cout << "  all rules satisfied: 1 shift per day, 5 day cap, 2 per shift minimum\n";
        } else {
            for (const std::string& v : violations) std::cout << "  " << v << "\n";
        }
        for (const std::string& w : s.warnings()) {
            std::cout << "  WARNING: " << w << "\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
