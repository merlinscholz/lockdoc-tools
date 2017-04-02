#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <tuple>
#include <iomanip>

#include "optionparser.h"
#include "optionparser_ext.h"

// hypothesizer: Creates hypotheses on locking rules, and tests them against a
// set of observations/lock combinations.
//
// Input: The output of txns_members_locks.sql in the form of a CSV file.

const double accept_threshold_default = .9, cutoff_threshold_default = .1, nolock_threshold_default = .05;

enum optionIndex { UNKNOWN, HELP,
	ACCEPTTHRESHOLD, CUTOFFTHRESHOLD, NOLOCKTHRESHOLD,
	DATATYPE, MEMBER, SORT, REPORT, BUGSQL };
const option::Descriptor usage[] = {
{
  UNKNOWN, 0, "", "", Arg::None,
  "Usage: hypothesizer [options] input.csv\n\nOptions:"
}, {
  HELP, 0, "h", "help", Arg::None, "--help  \tPrint usage and exit"
}, {
  ACCEPTTHRESHOLD, 0, "a", "accept-threshold", Arg::Required,
  "-a/--accept-threshold n  \tSet hypothesis accept threshold to n% (default: 90.0)"
}, {
  CUTOFFTHRESHOLD, 0, "t", "cutoff-threshold", Arg::Required,
  "-t/--cutoff-threshold n  \tSet hypothesis cutoff threshold to n% (default: 10.0)"
}, {
  NOLOCKTHRESHOLD, 0, "n", "nolock-threshold", Arg::Required,
  "-n/--nolock-threshold n  \tSet threshold for assuming that no lock is required to n% (default: 5.0)"
}, {
  DATATYPE, 0, "d", "datatype", Arg::Required,
  "-d/--datatype typename  \tOnly create/test hypotheses for a specific data structure; may be used more than once"
}, {
  MEMBER, 0, "m", "member", Arg::Required,
  "-m/--member member  \tOnly create/test hypotheses for specific data-structure member; may be used more than once"
}, {
  SORT, 0, "s", "sort", Arg::Required,
  "-s/--sort criterion  \tSort output by criterion "
  "\"member\" = datatype/member name, "
  "\"combinations\" = number of lock combinations, or "
  "\"hypotheses\" = number of hypotheses"
}, {
  REPORT, 0, "r", "report", Arg::Required,
  "-r/--report mode  \tGenerate report (mode: "
  "normal = human-readable, "
  "csv = machine-readable CSV, "
  "csvwinner = like csv but only winning hypothesis, "
  "doc = C source-code comment"
  ")"
}, {
  BUGSQL, 0, "b", "bugsql", Arg::None,
  "-b/--bugsql  \tGenerate parameters for counterexamples.sql.sh, "
  "which helps locating counterexamples in the kernel source code; "
  "only effective in combination with --report normal"
}, {0,0,0,0,0,0}
};

enum class SortCriterion { NONE, MEMBER, COMBINATIONS, HYPOTHESES };
enum class ReportMode { NORMAL, CSV, CSVWINNER, DOC };

// ID type
typedef uint16_t myid_t;

struct LockCombination {
	uint64_t occurrences;
	std::vector<myid_t> locks_held;	// order matters
	std::vector<myid_t> locks_held_sorted;	// sorted copy of locks_held for comparison purposes

	LockCombination(uint64_t occurrences, std::vector<myid_t>& locks_held)
		: occurrences(occurrences), locks_held(locks_held) {
		update_sorted_locks();
	}
	void set_locks_held(std::vector<myid_t>& l)
	{
		locks_held = l;
		update_sorted_locks();
	}
	void update_sorted_locks()
	{
		locks_held_sorted = locks_held;
		sort(locks_held_sorted.begin(), locks_held_sorted.end());
	}
	// Checks whether all locks in l are contained in locks_held.  Assumes l to
	// be sorted.
	bool contains_locks(const std::vector<myid_t>& l) const
	{
		auto it_theirlock = l.cbegin();
		for (const auto mylock : locks_held_sorted) {
			if (mylock == *it_theirlock) {
				++it_theirlock;
				if (it_theirlock == l.cend()) {
					return true;
				}
			}
		}
		return false;
	}
	// Returns actual order of locks presented in l.
	std::vector<myid_t> lock_order(const std::vector<myid_t>& l) const
	{
		std::vector<myid_t> ret;
		for (const auto mylock : locks_held) {
			if (find(l.cbegin(), l.cend(), mylock) != l.cend()) {
				ret.push_back(mylock);
			}
		}
		return ret;
	}
};

struct LockingHypothesisMatches {
	uint64_t occurrences = 0;
	std::vector<myid_t> sorted_hypothesis;
	std::map<std::vector<myid_t>, uint64_t> matches;
};

struct Member {
	Member(std::string datatype, std::string combined_name) : datatype(datatype)
	{
		parse_name(combined_name);
	}
	void clear()
	{
		name.clear();
		combinations.clear();
		hypotheses.clear();
	}
	void parse_name(std::string combined_name)
	{
		accesstype = combined_name[0];
		name = combined_name.c_str() + 2;
	}
	std::string combined_name() const
	{
		return std::string(1, accesstype) + ":" + name;
	}
	std::string datatype;
	std::string name; // without r: / w: prefix (this is kept in the accesstype member)
	uint64_t occurrences = 0; // counts all accesses to this member
	uint64_t occurrences_with_locks = 0; // counts accesses to this member with at least one lock held
	std::vector<LockCombination> combinations;
	std::map<std::vector<myid_t>, LockingHypothesisMatches> hypotheses;
	bool show = true; // set to false if filtered out by user parameters
	char accesstype; // r / w
};

// All members seen in the input.  The index of each element is used as a key
// in other data structures.
std::vector<Member> members;

// All lock names seen in the input.  The index of each element is used as a
// key in other data structures.
std::vector<std::string> locks;

// Per data-type list of lock combinations with list of protected members
// for ReportMode::DOC
// data-type -> lock combination -> vector of protected members
std::map<std::string, std::map<std::vector<myid_t>, std::vector<std::string>>> doc_map;

template <typename M, typename V>
void map2vec(const M& m, V& v) {
	for (const auto& elem : m) {
		v.push_back(elem.second);
	}
}

std::string locks2string(const std::vector<myid_t>& l, const std::string separator = " -> ", const std::string quote = "")
{
	std::stringstream ss;
	for (auto it = l.cbegin(); it != l.cend(); ++it) {
		ss << quote << locks[*it] << quote;
		if (it + 1 != l.cend()) {
			ss << separator;
		}
	}
	return ss.str();
}

void evaluate_hypothesis(Member& member, std::vector<myid_t>& hypothesis)
{
	auto ret = member.hypotheses.emplace(std::piecewise_construct,
		std::forward_as_tuple(hypothesis), std::forward_as_tuple());
	if (ret.second == false) {
		//std::cout << "ALREADY TESTED hypothesis: " << locks2string(hypothesis) << std::endl;
		return;
	}

	auto&& matches = ret.first->second;
	//std::cout << "new hypothesis: " << locks2string(hypothesis, " + ");
	for (const auto& lc : member.combinations) {
		if (lc.contains_locks(hypothesis)) {
			matches.occurrences += lc.occurrences;
			matches.sorted_hypothesis = hypothesis;
			matches.matches[lc.lock_order(hypothesis)] += lc.occurrences;
		}
	}
	//std::cout << ", matches: " << matches.occurrences << std::endl;
}

void find_hypotheses_rek(Member& member, const LockCombination& lc, unsigned next_lockpos, std::vector<myid_t>& cur, unsigned depth)
{
	for (unsigned lockpos = next_lockpos; lockpos < lc.locks_held_sorted.size(); ++lockpos) {
		cur.push_back(lc.locks_held_sorted[lockpos]);
		if (depth == 1) {
			evaluate_hypothesis(member, cur);
		} else {
			find_hypotheses_rek(member, lc, lockpos + 1, cur, depth - 1);
		}
		cur.pop_back();
	}
}

void find_hypotheses(Member& member)
{
	for (unsigned depth = 1; ; ++depth) {
		size_t prev_hypothesis_count = member.hypotheses.size();

		//std::cerr << "depth " << depth << std::endl;

		std::vector<myid_t> cur;
		for (const auto& lc : member.combinations) {
			//std::cout << locks2string(obs.locks_held_sorted, " + ") << std::endl;
			cur.clear();
			find_hypotheses_rek(member, lc, 0, cur, depth);
		}

		// if no additional hypotheses with requested depth found, we're done
		if (prev_hypothesis_count == member.hypotheses.size()) {
			break;
		}
	}
}

void print_bugsql(char const *prefix, const Member& member, const std::vector<myid_t>& l, bool order_matters)
{
	std::cout << prefix << "counterexample.sql.sh "
		<< member.datatype << " "
		<< member.combined_name() << " "
		<< "CEX "
		<< (order_matters ? "SEQ " : "ANY ")
		<< locks2string(l, " ", "'") << std::endl;
}

void print_hypotheses(const Member& member,
	double accept_threshold, double cutoff_threshold, double nolock_threshold,
	ReportMode reportmode, bool bugsql)
{
	if (reportmode == ReportMode::NORMAL) {
		std::cout << member.datatype << " member: "
			<< member.name << " [" << member.accesstype << "] ("
			<< member.combinations.size() << " lock combinations)" << std::endl;
		std::cout << "  hypotheses: " << member.hypotheses.size() << std::endl;
	}

	// handle accesses w/o locks
	double nolock_fraction =
		(double) (member.occurrences - member.occurrences_with_locks) /
		(double) member.occurrences;
	bool nolock_is_winner = nolock_fraction >= nolock_threshold;
	if (reportmode == ReportMode::NORMAL) {
		std::cout << (nolock_is_winner ? '!' : ' ') << "   ";
		if (nolock_fraction == 0) {
			std::cout << "Unlikely to be";
		} else if (!nolock_is_winner) {
			std::cout << "Possibly";
		} else {
			std::cout << "Seemingly";
		}
		std::cout << " accessible without locks, "
			<< (member.occurrences - member.occurrences_with_locks)
			<< " accesses without locks ["
			<< nolock_fraction * 100
			<< "%] out of a total of " << member.occurrences << " observed.)" << std::endl;
	} else if (reportmode == ReportMode::CSV ||
		(reportmode == ReportMode::CSVWINNER && nolock_is_winner)) {
		std::cout << member.datatype << ";" << member.name << ";" << member.accesstype << ";nolock;"
			<< (member.occurrences - member.occurrences_with_locks) << ";"
			<< member.occurrences << ";"
			<< std::setprecision(5)
			<< (double) (member.occurrences - member.occurrences_with_locks) /
				(double) member.occurrences * 100 << ";"
			<< nolock_is_winner << ";"
			<< "TODO\n";
		// are we done already?
		if (reportmode == ReportMode::CSVWINNER) {
			return;
		}
	} else if (reportmode == ReportMode::DOC && nolock_is_winner) {
		// TODO properly group member r/w accesses
		doc_map[member.datatype][std::vector<myid_t>()].push_back(member.combined_name());
		return;
	}

	// sort lock hypotheses by the number of memory accesses where each *set*
	// of locks is held (for now disregarding the lock order, this happens
	// within the output loop)
	std::vector<LockingHypothesisMatches> sorted_hypotheses;
	map2vec(member.hypotheses, sorted_hypotheses);
	sort(sorted_hypotheses.begin(), sorted_hypotheses.end(),
		[](const LockingHypothesisMatches& a, const LockingHypothesisMatches& b)
		{ return a.occurrences > b.occurrences || // reverse order
			(a.occurrences == b.occurrences &&
			a.sorted_hypothesis.size() > b.sorted_hypothesis.size()); // more locks first
		});

	// collect all lock orders for ReportMode CSVWINNER and DOC
	std::vector<std::pair<std::vector<myid_t>, uint64_t>> all_lock_orders;

	// iterate over hypotheses from best to worst
	bool found_winner = nolock_is_winner; // have we found a winner already?
	int printed = 0;
	std::cout.precision(3);
	for (const auto& h : sorted_hypotheses) {
		// skip hypotheses below cutoff_threshold
		double match_fraction = (double) h.occurrences / (double) member.occurrences_with_locks;
		if (match_fraction < cutoff_threshold) {
			break;
		}

		// more than one locking order within this lock set?
		// -> show lock set, and then (indented) all observed locking orders
		// (for the other report modes, this branch is even taken with only one
		// locking order)
		if (h.matches.size() > 1 ||
			reportmode == ReportMode::CSV ||
			reportmode == ReportMode::CSVWINNER ||
			reportmode == ReportMode::DOC) {
			if (reportmode == ReportMode::NORMAL) {
				std::cout << "    " << std::setw(5) << match_fraction * 100 << "% ("
					<< h.occurrences << " out of " << member.occurrences_with_locks << " mem accesses under locks): "
					<< locks2string(h.sorted_hypothesis, " + ") << std::endl;
				if (bugsql) {
					print_bugsql("    ", member, h.sorted_hypothesis, false);
				}
			}

			// sort matches
			std::vector<std::pair<std::vector<myid_t>, uint64_t>> sorted_matches;
			for (const auto& match : h.matches) {
				sorted_matches.push_back(std::pair<std::vector<myid_t>, uint64_t>(match.first, match.second));
			}
			sort(sorted_matches.begin(), sorted_matches.end(),
				[](const std::pair<std::vector<myid_t>, uint64_t>& a,
					const std::pair<std::vector<myid_t>, uint64_t>& b)
					{
						return a.second > b.second;
					});

			// show locking-order distribution
			for (const auto& match : sorted_matches) {
				// fraction within this lock set
				double local_fraction = match_fraction = (double) match.second / (double) h.occurrences;
				// fraction within all memory accesses under locks
				match_fraction = (double) match.second / (double) member.occurrences_with_locks;

				bool this_is_the_winner = !found_winner && match_fraction >= accept_threshold;
				found_winner = found_winner || this_is_the_winner;

				if (reportmode == ReportMode::NORMAL) {
					std::cout << (this_is_the_winner ? '!' : ' ')
						<< "      " << std::setw(5) << local_fraction * 100 << "% "
						<< locks2string(match.first) << std::endl;
					if (bugsql && this_is_the_winner) {
						print_bugsql("!      ", member, match.first, true);
					} else if (bugsql) {
						print_bugsql("       ", member, match.first, true);
					}
				} else if (reportmode == ReportMode::CSV) {
					std::cout << member.datatype << ";"
						<< member.name << ";"
						<< member.accesstype << ";"
						<< locks2string(match.first) << ";"
						<< match.second << ";"
						<< member.occurrences_with_locks << ";"
						<< std::setprecision(5)
						<< match_fraction * 100 << ";"
						<< this_is_the_winner << ";"
						<< "TODO\n";
				} else if (reportmode == ReportMode::CSVWINNER || reportmode == ReportMode::DOC) {
					all_lock_orders.push_back(match);
				}
			}
		} else {
			// only one locking order observed, show this one right away

			bool this_is_the_winner = !found_winner && match_fraction >= accept_threshold;
			found_winner = found_winner || this_is_the_winner;

			std::cout << (this_is_the_winner ? '!' : ' ')
				<< "   " << std::setw(5) << match_fraction * 100 << "% ("
				<< h.occurrences << " out of " << member.occurrences_with_locks << " mem accesses under locks): "
				<< locks2string(h.matches.begin()->first) << std::endl;
			if (bugsql && this_is_the_winner) {
				print_bugsql("!   ", member, h.matches.begin()->first, true);
			} else if (bugsql) {
				print_bugsql("    ", member, h.matches.begin()->first, true);
			}
		}
		printed++;
	}

	if (printed == 0 && reportmode == ReportMode::NORMAL) {
		std::cout << "    (No hypothesis with locks exceeds cutoff threshold of " << cutoff_threshold * 100 << "%.)" << std::endl;
	} else if (printed > 0 && reportmode == ReportMode::NORMAL && !found_winner) {
		std::cout << "    (No hypothesis with locks exceeds accept threshold of " << accept_threshold * 100 << "%.)" << std::endl;
	} else if (printed == 0 && reportmode == ReportMode::CSV) {
		std::cout << member.datatype << ";"
			<< member.name << ";"
			<< member.accesstype << ";"
			<< "no hypothesis with locks exceeds cutoff threshold;0;0;0;0;TODO\n";
	}

	if (reportmode == ReportMode::CSVWINNER || reportmode == ReportMode::DOC) {
		sort(all_lock_orders.begin(), all_lock_orders.end(),
			[](const std::pair<std::vector<myid_t>, uint64_t>& a,
				const std::pair<std::vector<myid_t>, uint64_t>& b)
				{
					return a.second > b.second ||
						(a.second == b.second && a.first.size() > b.first.size());
				});
		if (all_lock_orders.size() == 0) {
			if (reportmode == ReportMode::CSVWINNER) {
				std::cout << member.datatype << ";"
					<< member.name << ";"
					<< member.accesstype << ";"
					<< "no hypothesis with locks exceeds cutoff threshold;0;0;0;0;TODO"
					<< std::endl;
			} else {
				std::cerr << "Cannot generate documentation for "
					<< member.datatype << "::" << member.name
					<< " [" << member.accesstype << "]" << std::endl;
			}
		} else {
			auto& lo = all_lock_orders[0];
			double match_fraction = (double) lo.second / (double) member.occurrences_with_locks;
			bool this_is_the_winner = match_fraction >= accept_threshold;
			if (reportmode == ReportMode::CSVWINNER) {
				std::cout << member.datatype << ";"
					<< member.name << ";"
					<< member.accesstype << ";"
					<< locks2string(lo.first) << ";"
					<< lo.second << ";"
					<< member.occurrences_with_locks << ";"
					<< std::setprecision(5)
					<< ((double) lo.second / (double) member.occurrences_with_locks * 100) << ";"
					<< this_is_the_winner << ";"
					<< "TODO" << std::endl;
			} else if (this_is_the_winner) {
				// TODO properly group member r/w accesses
				doc_map[member.datatype][lo.first].push_back(member.combined_name());
			} else {
				std::cerr << "Cannot generate documentation for "
					<< member.datatype << "::" << member.name
					<< " [" << member.accesstype << "] "
					<< "(best locking rule does not exceed accept threshold)" << std::endl;
			}
		}
	}
}

void print_doc(void)
{
	const char *prefix = " * ";
	for (const auto& type : doc_map) {
		std::cout << prefix
			<< type.first << " locking rules:\n" << prefix << "\n";
		for (const auto& locks : type.second) {
			if (locks.first.size() == 0) {
				std::cout << prefix
					<< "No locks needed for:\n";
			} else {
				std::cout << prefix
					<< locks2string(locks.first) << " protects:\n";
			}

			// access to element 0 is OK, we know there's at least one entry
			std::cout << prefix << "  " << locks.second[0];
			for (auto it = locks.second.cbegin() + 1; it != locks.second.cend(); ++it) {
				std::cout << ", " << *it;
			}
			std::cout << "\n" << prefix << "\n";
		}
	}
}

int main(int argc, char **argv)
{
	// === Command-line parsing ===
	// skip program name argv[0] if present
	argc -= (argc > 0);
	argv += (argc > 0);

	option::Stats stats(usage, argc, argv);
	std::vector<option::Option> options(stats.options_max);
	std::vector<option::Option> buffer(stats.buffer_max);
	option::Parser parse(usage, argc, argv, &options[0], &buffer[0]);

	if (parse.error()) {
		return 1;
	}

	for (option::Option *opt = options[UNKNOWN]; opt; opt = opt->next()) {
		std::cerr << "Unknown option: " << std::string(opt->name, opt->namelen) << "\n";
	}

	if (options[HELP] || options[UNKNOWN] ||
		argc == 0 || parse.nonOptionsCount() != 1) {
		option::printUsage(std::cout, usage);
		return options[HELP] ? 0 : 1;
	}

	double accept_threshold = accept_threshold_default;
	if (options[ACCEPTTHRESHOLD]) {
		try {
			accept_threshold = std::stod(options[ACCEPTTHRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse accept threshold value " << options[ACCEPTTHRESHOLD].last()->arg << std::endl;
			return 1;
		}
		accept_threshold /= 100.0;
	}

	double cutoff_threshold = cutoff_threshold_default;
	if (options[CUTOFFTHRESHOLD]) {
		try {
			cutoff_threshold = std::stod(options[CUTOFFTHRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse cutoff threshold value " << options[CUTOFFTHRESHOLD].last()->arg << std::endl;
			return 1;
		}
		cutoff_threshold /= 100.0;
	}

	double nolock_threshold = nolock_threshold_default;
	if (options[NOLOCKTHRESHOLD]) {
		try {
			nolock_threshold = std::stod(options[NOLOCKTHRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse no-lock threshold value " << options[NOLOCKTHRESHOLD].last()->arg << std::endl;
			return 1;
		}
		nolock_threshold /= 100.0;
	}

	std::set<std::string> accepted_datatypes;
	for (option::Option *o = options[DATATYPE]; o; o = o->next()) {
		accepted_datatypes.insert(o->arg);
	}

	std::set<std::string> accepted_members;
	for (option::Option *o = options[MEMBER]; o; o = o->next()) {
		accepted_members.insert(o->arg);
	}

	SortCriterion sortby = SortCriterion::NONE;
	if (options[SORT]) {
		std::string criterion = options[SORT].last()->arg;
		if (criterion == "member") {
			sortby = SortCriterion::MEMBER;
		} else if (criterion == "combinations") {
			sortby = SortCriterion::COMBINATIONS;
		} else if (criterion == "hypotheses") {
			sortby = SortCriterion::HYPOTHESES;
		} else {
			std::cerr << "Unknown sort criterion " << criterion << std::endl;
			return 1;
		}
	}

	ReportMode reportmode = ReportMode::NORMAL;
	if (options[REPORT]) {
		std::string mode = options[REPORT].last()->arg;
		if (mode == "normal") {
			reportmode = ReportMode::NORMAL;
		} else if (mode == "csv") {
			reportmode = ReportMode::CSV;
		} else if (mode == "csvwinner") {
			reportmode = ReportMode::CSVWINNER;
		} else if (mode == "doc") {
			reportmode = ReportMode::DOC;
		} else {
			std::cerr << "Unknown report mode " << mode << std::endl;
			return 1;
		}
	}

	bool bugsql = options[BUGSQL];

	const char *filename = parse.nonOption(0);

	// === Load input CSV into memory ===
	// input format example:
	// inode	r:i_mode,r:i_opflags,r:i_uid,r:i_flags,r:i_sb,r:i_rdev  EMB:5705(i_mutex),16(rcu)       1
	std::ifstream infile(filename);
	if (!infile.is_open()) {
		std::cerr << "Cannot open file: " << filename << std::endl;
		return 1;
	}

	// CSV parsing helper variables
	std::stringstream ss;
	std::vector<std::string> lineElems;
	std::string inputLine, inputColumn, inputElement;
	// total memory-access count
	uint64_t accesscount = 0;
	// member+datatype/lock to ID mapping, only needed temporarily during CSV parsing
	std::map<std::pair<std::string, std::string>, myid_t> member_to_id;
	std::map<std::string, myid_t> lock_to_id;
	// per-member lock combination to LockCombination mapping (the latter
	// contains, among other things, the occurrence count), only needed
	// temporarily during CSV parsing
	std::vector<std::map<std::vector<myid_t>, LockCombination> > members_combinations;
	for (unsigned lineCounter = 0; getline(infile, inputLine); lineCounter++) {

		// Skip the header if there is one.  This check exploits the fact that
		// any valid input line must end with a decimal digit.
		if (lineCounter == 0) {
			if (inputLine.length() == 0 || !isdigit(inputLine[inputLine.length() - 1])) {
				continue;
			} else {
				std::cerr << "Warning: Input data does not start with a CSV header." << std::endl;
			}
		}

		// Split along tab characters
		ss.clear();
		ss.str("");
		ss << inputLine;
		lineElems.clear();
		while (getline(ss, inputColumn, '\t')) {
			lineElems.push_back(inputColumn);
		}

		// Sanity check
		if (lineElems.size() != 4) {
			std::cerr << "Warning: Line " << lineCounter << " does not have the required number of columns." << std::endl;
			continue;
		}

		std::string& datatype = lineElems[0];

		// Read #occurrences
		unsigned long long occurrences = std::stoull(lineElems[3]);
		accesscount += occurrences;

		// Split lock list along commas
		ss.clear();
		ss.str("");
		ss << lineElems[2];
		std::vector<myid_t> locks_held;
		while (getline(ss, inputElement, ',')) {
			myid_t lock_id;
			if (inputElement == "") {
				// no lock held, memory accesses outside of TXNs
				continue;
			}
			auto it = lock_to_id.find(inputElement);
			if (it == lock_to_id.end()) {
				locks.push_back(inputElement);
				if (locks.size() - 1 > std::numeric_limits<decltype(lock_id)>::max()) {
					std::cerr << "Error: More locks than myid_t can hold ("
						<< (uint64_t) std::numeric_limits<decltype(lock_id)>::max()
						<< "), increase datatype bits." << std::endl;
					return 1;
				}
				lock_id = locks.size() - 1;
				lock_to_id[inputElement] = lock_id;
			} else {
				lock_id = it->second;
			}
			locks_held.push_back(lock_id);
		}

		// Split member list along commas
		ss.clear();
		ss.str("");
		ss << lineElems[1];
		while (getline(ss, inputElement, ',')) {
			myid_t member_id;
			auto it = member_to_id.find(std::pair<std::string, std::string>(datatype, inputElement));
			if (it == member_to_id.end()) {
				members.push_back(Member(datatype, inputElement));
				members_combinations.resize(members_combinations.size() + 1);
				if (members.size() - 1 > std::numeric_limits<decltype(member_id)>::max()) {
					std::cerr << "Error: More members than myid_t can hold("
						<< (uint64_t) std::numeric_limits<decltype(member_id)>::max()
						<< "), increase datatype bits." << std::endl;
					return 1;
				}
				member_id = members.size() - 1;
				member_to_id[std::pair<std::string, std::string>(datatype, inputElement)] = member_id;
			} else {
				member_id = it->second;
			}

			// add lock combination (or increase its occurrence counter)
			members[member_id].occurrences += occurrences;
			if (locks_held.size() > 0) {
				members[member_id].occurrences_with_locks += occurrences;
				auto& combinations = members_combinations[member_id];
				auto ret = combinations.emplace(std::piecewise_construct,
					std::forward_as_tuple(locks_held), std::forward_as_tuple(occurrences, locks_held));
				if (ret.second == false) {
					ret.first->second.occurrences += occurrences;
				}
			}
		}
	}

	// clear unneeded data structures
	member_to_id.clear();
	lock_to_id.clear();

	// copy lock combinations to members for fast access (and clear the former)
	for (myid_t i = 0; i < members.size(); ++i) {
		map2vec(members_combinations[i], members[i].combinations);
	}
	members_combinations.clear();

	std::cerr << "Input file read ("
		<< members.size() << " distinct members, "
		<< locks.size() << " distinct locks, "
		<< accesscount << " memory accesses in total)"
		<< std::endl;

	std::cerr << "Synthesizing lock hypotheses ..." << std::endl;

	if (reportmode == ReportMode::CSV || reportmode == ReportMode::CSVWINNER) {
		std::cout << "type;member;accesstype;locks;occurrences;total;percentage;accepted;confidence\n";
	}

#pragma omp parallel for
//	for (auto&& member : members) {
	for (auto it = members.begin(); it < members.end(); ++it) {
		Member& member = *it;

		// Skip if user has specified datatypes + this one is not in the list
		if (accepted_datatypes.size() > 0 &&
			accepted_datatypes.find(member.datatype) == accepted_datatypes.end()) {
			member.clear();
			member.show = false;
			continue;
		}

		// Skip if user has specified members + this one is not in the list
		if (accepted_members.size() > 0 &&
			accepted_members.find(member.name) == accepted_members.end() &&
			accepted_members.find(member.combined_name()) == accepted_members.end()) {
			member.clear();
			member.show = false;
			continue;
		}

		find_hypotheses(member);

#pragma omp critical
{
		if (sortby == SortCriterion::NONE) {
			print_hypotheses(member, accept_threshold, cutoff_threshold, nolock_threshold, reportmode, bugsql);

			member.clear();
		} else {
			std::cerr << ".";
		}
}
	}

	if (sortby != SortCriterion::NONE) {
		std::cerr << std::endl;
		switch (sortby) {
		case SortCriterion::MEMBER:
			sort(members.begin(), members.end(),
				[](const Member& a, const Member& b)
				{ return a.datatype < b.datatype
					|| (a.datatype == b.datatype && a.name < b.name)
					|| (a.datatype == b.datatype && a.name == b.name && a.accesstype < b.accesstype); });
			break;
		case SortCriterion::COMBINATIONS:
			sort(members.begin(), members.end(),
				[](const Member& a, const Member& b)
				{ return a.combinations.size() < b.combinations.size(); });
			break;
		case SortCriterion::HYPOTHESES:
			sort(members.begin(), members.end(),
				[](const Member& a, const Member& b)
				{ return a.hypotheses.size() < b.hypotheses.size(); });
			break;
		default: ;
		}

		for (const auto& member : members) {
			if (member.show) {
				print_hypotheses(member, accept_threshold, cutoff_threshold, nolock_threshold, reportmode, bugsql);
			}
		}
	}

	if (reportmode == ReportMode::DOC) {
		std::cout << "/*\n";
		print_doc();
		std::cout << "*/\n";
	}
}
