#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
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

const double match_threshold_default = .1;

enum optionIndex { UNKNOWN, HELP, THRESHOLD };
const option::Descriptor usage[] = {
{
  UNKNOWN, 0, "", "", Arg::None,
  "Usage: hypothesizer [options] input.csv\n\nOptions:"
}, {
  HELP, 0, "h", "help", Arg::None, "--help  \tPrint usage and exit"
}, {
  THRESHOLD, 0, "t", "threshold", Arg::Required,
  "-t/--threshold n  \tSet hypothesis match threshold to n% (default: 10.0)"
}, {0,0,0,0,0,0}
};

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
	bool contains_locks(std::vector<myid_t>& l)
	{
		auto it_theirlock = l.begin();
		for (auto mylock : locks_held_sorted) {
			if (mylock == *it_theirlock) {
				++it_theirlock;
				if (it_theirlock == l.end()) {
					return true;
				}
			}
		}
		return false;
	}
	// Returns actual order of locks presented in l.
	std::vector<myid_t> lock_order(std::vector<myid_t>& l)
	{
		std::vector<myid_t> ret;
		for (auto mylock : locks_held) {
			if (find(l.begin(), l.end(), mylock) != l.end()) {
				ret.push_back(mylock);
			}
		}
		return ret;
	}
};

struct LockingHypothesisMatches {
	LockingHypothesisMatches() : occurrences(0) { }
	uint64_t occurrences;
	std::vector<myid_t> sorted_hypothesis;
	std::map<std::vector<myid_t>, uint64_t> matches;
};

struct Member {
	Member(std::string name) : name(name), occurrences(0), occurrences_with_locks(0) { }
	std::string name;
	uint64_t occurrences; // counts all accesses to this member
	uint64_t occurrences_with_locks; // counts accesses to this member with at least one lock held
	std::map<std::vector<myid_t>, LockCombination> combinations;
	std::map<std::vector<myid_t>, LockingHypothesisMatches> hypotheses;
};

// All members seen in the input.  The index of each element is used as a key
// in other data structures.
std::vector<Member> members;

// All lock names seen in the input.  The index of each element is used as a
// key in other data structures.
std::vector<std::string> locks;

template <typename M, typename V> 
void map2vec(const M& m, V& v) {
	for (auto&& elem : m) {
		v.push_back(elem.second);
	}
}

std::string locks2string(const std::vector<myid_t>& l, const std::string separator = " -> ")
{
	std::stringstream ss;
	for (auto it = l.cbegin(); it != l.cend(); ++it) {
		ss << locks[*it];
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
	for (auto&& lc : member.combinations) {
		if (lc.second.contains_locks(hypothesis)) {
			matches.occurrences += lc.second.occurrences;
			matches.sorted_hypothesis = hypothesis;
			matches.matches[lc.second.lock_order(hypothesis)] += lc.second.occurrences;
		}
	}
	//std::cout << ", matches: " << matches.occurrences << std::endl;
}

void find_hypotheses_rek(Member& member, LockCombination& lc, unsigned next_lockpos, std::vector<myid_t>& cur, unsigned depth)
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

		for (auto&& lc : member.combinations) {
			//std::cout << locks2string(obs.locks_held_sorted, " + ") << std::endl;
			std::vector<myid_t> cur;
			cur.reserve(depth);
			find_hypotheses_rek(member, lc.second, 0, cur, depth);
		}

		// if no additional hypotheses with requested depth found, we're done
		if (prev_hypothesis_count == member.hypotheses.size()) {
			break;
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

	double match_threshold = match_threshold_default;
	if (options[THRESHOLD]) {
		try {
			match_threshold = std::stod(options[THRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse threshold value " << options[THRESHOLD].last()->arg << std::endl;
			return 1;
		}
		match_threshold /= 100.0;
	}

	const char *filename = parse.nonOption(0);

	// === Load input CSV into memory ===
	// input format example:
	// r:i_mode,r:i_opflags,r:i_uid,r:i_flags,r:i_sb,r:i_rdev  EMB:5705(i_mutex),16(rcu)       1
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
	// member/lock to ID mapping, only needed temporarily during CSV parsing
	std::map<std::string, myid_t> member_to_id;
	std::map<std::string, myid_t> lock_to_id;
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
		if (lineElems.size() != 3) {
			std::cerr << "Warning: Line " << lineCounter << " does not have the required number of columns." << std::endl;
			continue;
		}

		// Read #occurrences
		unsigned long long occurrences = std::stoull(lineElems[2]);
		accesscount += occurrences;

		// Split lock list along commas
		ss.clear();
		ss.str("");
		ss << lineElems[1];
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
		ss << lineElems[0];
		while (getline(ss, inputElement, ',')) {
			myid_t member_id;
			auto it = member_to_id.find(inputElement);
			if (it == member_to_id.end()) {
				members.push_back(Member(inputElement));
				if (members.size() - 1 > std::numeric_limits<decltype(member_id)>::max()) {
					std::cerr << "Error: More members than myid_t can hold("
						<< (uint64_t) std::numeric_limits<decltype(member_id)>::max()
						<< "), increase datatype bits." << std::endl;
					return 1;
				}
				member_id = members.size() - 1;
				member_to_id[inputElement] = member_id;
			} else {
				member_id = it->second;
			}

			// add lock combination (or increase its occurrence counter)
			members[member_id].occurrences += occurrences;
			if (locks_held.size() > 0) {
				members[member_id].occurrences_with_locks += occurrences;
				auto ret = members[member_id].combinations.emplace(std::piecewise_construct,
					std::forward_as_tuple(locks_held), std::forward_as_tuple(occurrences, locks_held));
				if (ret.second == false) {
					ret.first->second.occurrences += occurrences;
				}
			}
		}
	}

	std::cerr << "Input file read ("
		<< members.size() << " distinct members, "
		<< locks.size() << " distinct locks, "
		<< accesscount << " memory accesses in total)"
		<< std::endl;

	std::cerr << "Synthesizing lock hypotheses ..." << std::endl;

	for (auto&& member : members) {
		std::cout << "member: " << member.name << " (" << member.combinations.size() << " lock combinations)" << std::endl;

		find_hypotheses(member);
		std::cout << "  hypotheses: " << member.hypotheses.size() << std::endl;
//		for (auto it = member.hypotheses.cbegin(); it != member.hypotheses.cend(); ++it) {
//			std::cout << locks2string(it->first) << ", ";
//		}
//		std::cout << std::endl;

		std::vector<LockingHypothesisMatches> sorted_hypotheses;
		map2vec(member.hypotheses, sorted_hypotheses);
		sort(sorted_hypotheses.begin(), sorted_hypotheses.end(),
			[](const LockingHypothesisMatches& a, const LockingHypothesisMatches& b)
			{ return a.occurrences > b.occurrences; }); // reverse order

		int printed = 0;
		std::cout.precision(3);
		for (auto&& h : sorted_hypotheses) {
			double match_fraction = (double) h.occurrences / (double) member.occurrences_with_locks;
			if (match_fraction < match_threshold) {
				break;
			}
			if (h.matches.size() > 1) {
				std::cout << "    " << std::setw(5) << match_fraction * 100 << "% ("
					<< h.occurrences << " out of " << member.occurrences << " mem accesses under locks): "
					<< locks2string(h.sorted_hypothesis, " + ") << std::endl;

				// show locking-order distribution
				for (auto&& match : h.matches) {
					std::cout << "       " << std::setw(5) << ((double) match.second / (double) h.occurrences * 100) << "% "
						<< locks2string(match.first) << std::endl;
				}
			} else {
				// only one locking order observed, show this one right away
				std::cout << "    " << std::setw(5) << match_fraction * 100 << "% ("
					<< h.occurrences << " out of " << member.occurrences << " mem accesses under locks): "
					<< locks2string(h.matches.begin()->first) << std::endl;
			}
			printed++;
		}

		if (printed == 0) {
			std::cout << "    (No hypothesis exceeds match threshold of " << match_threshold * 100 << "%.)" << std::endl;
		}

		if (member.occurrences != member.occurrences_with_locks) {
			std::cout << "    (Possibly accessible without locks, "
				<< (member.occurrences - member.occurrences_with_locks)
				<< " accesses without locks ["
				<< (double) (member.occurrences - member.occurrences_with_locks) /
					(double) member.occurrences * 100
				<< "%] out of a total of " << member.occurrences << " observed.)" << std::endl;
		}
	}
}
