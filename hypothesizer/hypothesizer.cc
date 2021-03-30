#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <string>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <tuple>
#include <iomanip>
#include <deque>
#include <assert.h>
#include <boost/container_hash/hash.hpp>

#include "optionparser.h"
#include "optionparser_ext.h"

//#define DEBUG

// hypothesizer: Creates hypotheses on locking rules, and tests them against a
// set of observations/lock combinations.
//
// Input: The output of txns_members_locks.sql in the form of a CSV file.
//
// Note: It is possible to limit the number of used threads by setting the
// OMP_NUM_THREADS environment variable.

const double accept_threshold_default = .9, nolock_threshold_default = .05, cutoff_threshold_default = .1, confidence_threshold_default = 50.0, reduction_factor_default = .05;
const unsigned max_hypo_len_default = 0;

enum SelectionStrategy { TOPDOWN, BOTTOMUP, SHARPEN, LOCKSET };
enum optionIndex { UNKNOWN, HELP,
	REDUCTIONFACTOR, SELECTIONSTRATEGY,
	NOLOCKTHRESHOLD, ACCEPTTHRESHOLD, CUTOFFTHRESHOLD,
	DATATYPE, MEMBER, SORT, REPORT, BUGSQL, CONFIDENCETHRESHOLD, MAXHYPOLEN };
const option::Descriptor usage[] = {
{
  UNKNOWN, 0, "", "", Arg::None,
  "Usage: hypothesizer [options] input.csv\n\nOptions:"
}, {
  HELP, 0, "h", "help", Arg::None, "--help  \tPrint usage and exit"
}, {
  NOLOCKTHRESHOLD, 0, "n", "nolock-threshold", Arg::Required,
  "-n/--nolock-threshold n  \tSet threshold for assuming that no lock is required to n% (default: 5.0)"
}, {
  ACCEPTTHRESHOLD, 0, "a", "accept-threshold", Arg::Required,
  "-a/--accept-threshold n  \tSet hypothesis accept threshold to n% (default: 90.0)"
}, {
  REDUCTIONFACTOR, 0, "f", "reduction-factor", Arg::Required,
  "-f/--reduction-factor n  \tSet max. allowed reduction in rel. support when selecting a more restrictive hypothesis to n% (default: 5.0)"
}, {
  SELECTIONSTRATEGY, 0, "g", "selection-strategy", Arg::Required,
  "-g/--selection-strategy n  \tSelect the strategy for determining the winning hypothesis: \"list\", or a \"graph\". "
}, {
  CUTOFFTHRESHOLD, 0, "t", "cutoff-threshold", Arg::Required,
  "-t/--cutoff-threshold n  \tSet hypothesis cutoff threshold to n% (default: 10.0)"
}, {
  DATATYPE, 0, "d", "datatype", Arg::Required,
  "-d/--datatype typename  \tOnly create/test hypotheses for a specific data structure that name starts with $datatype; may be used more than once"
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
  "effective in combination with --report normal, implicit with --report csv or csvwinner"
}, {
  CONFIDENCETHRESHOLD, 0, "c", "confidence-threshold", Arg::Required,
  "-c/--confidence-threshold n  \tSet observations threshold for assuming a hypothesis is trustworthy to n (default: 50). "
  "Values below lead to a scaled relative support of the respective hypothesis."
}, {
  MAXHYPOLEN, 0, "l", "hypothesis-length", Arg::Required,
  "-l/--hypothesis-length n  \tLimit length of derived hypothesis to n locks (default: 0). "
  "0 means no limit."
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
		// the no-lock hypothesis always matches
		if (it_theirlock == l.cend()) {
			return true;
		}
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
	std::vector<myid_t> lock_order(std::vector<myid_t> l) const
	{
		std::vector<myid_t> ret;
		// fast-path for no-lock hypothesis
		if (l.empty()) {
			return ret;
		}
		for (const auto mylock : locks_held) {
			auto it = find(l.begin(), l.end(), mylock);
			if (it != l.end()) {
				ret.push_back(mylock);

				// Make sure a specific lock is only presented once.  This
				// prevents an (ordered) input of "LOCK_X" presenting a special
				// case of "LOCK_X -> LOCK_X" in case this lock is held
				// multiple times in this combination (which can happen quite
				// often with EMBOTHER).
				l.erase(it);
				if (l.empty()) {
					break;
				}
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

struct HypothesisHasher {
	std::size_t operator()(const std::vector<myid_t>& h) const {
#if 0
		return boost::hash_range(h.begin(), h.end());
#else
		myid_t hash_value = 0;
		std::hash<myid_t> hasher;
		for (myid_t value : h) {
			hash_value ^= hasher(value);
		}
		return hash_value;
#endif
	}
};

struct Member {
	static const std::vector<myid_t> root_node;
	Member(std::string datatype, std::string combined_name) : datatype(datatype)
	{
		parse_name(combined_name);
	}
	void clear()
	{
		name.clear();
		combinations.clear();
		hypotheses.clear();
		conflict_list.clear();
		winning_hypothesis.clear();
		hypotheses.clear();
		graph.clear();
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
	std::unordered_map<std::vector<myid_t>, LockingHypothesisMatches, HypothesisHasher> hypotheses;
	std::unordered_map<std::vector<myid_t>, std::set<std::vector<myid_t>>, HypothesisHasher> graph;
	/*
	 * Store equal hypotheses
	 * Two hypotheses are considered equal if the same amount of locks
	 * are involved and both have the same relative support.
	 * The first entry is used as representative for this group.
	 * It furthermore is used as concrete winning hypothesis.
	 * Each element is a pair of hypotheses. The first hypothesis is
	 * a sorted list of locks. Whereas the second entry is a particular
	 * locking hypothesis having the locks in a correct order.
	 */
	std::deque<std::pair<std::vector<myid_t>, std::vector<myid_t>>> conflict_list;
	std::vector<myid_t> winning_hypothesis;
	bool winner_found = false;
	bool show = true; // set to false if filtered out by user parameters
	char accesstype; // r / w
};
const std::vector<myid_t> Member::root_node;

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
	if (l.size() == 0) {
		ss << "(no locks held)";
	}
	for (auto it = l.cbegin(); it != l.cend(); ++it) {
		ss << quote << locks[*it] << quote;
		if (it + 1 != l.cend()) {
			ss << separator;
		}
	}
	return ss.str();
}

std::string ids2string(const std::vector<myid_t>& l, const std::string separator = " -> ", const std::string quote = "")
{
	std::stringstream ss;
	if (l.size() == 0) {
		ss << "(no locks held)";
	}
	for (auto it = l.cbegin(); it != l.cend(); ++it) {
		ss << quote << *it << quote;
		if (it + 1 != l.cend()) {
			ss << separator;
		}
	}
	return ss.str();
}

int diff_hypotheses(const std::vector<myid_t>& lhs, const std::vector<myid_t>& rhs)
{
	assert(rhs.size() >= lhs.size());

	unsigned int diff = rhs.size() - lhs.size();

	for (unsigned int i = 0; i <= diff; i++) {
		unsigned int found = 0;
		for (unsigned int k = 0; k < lhs.size(); k++) {
			if (lhs[k] == rhs[k + i]) {
				found++;
			}
		}
		if (found == lhs.size()) {
			return diff;
		}
	}
	return -1;
}

bool in_conflict_list(const std::deque<std::pair<std::vector<myid_t>, std::vector<myid_t>>>& conflict_list, const std::vector<myid_t>& hypothesis) {
	for (auto &elem : conflict_list) {
		if (elem.second == hypothesis) {
			return true;
		}
	}
	return false;
}

void print_graph(Member& member) {
	std::deque<std::vector<myid_t>> nodes;
	nodes.push_back(Member::root_node);

	while (nodes.size() > 0) {
		std::vector<myid_t> cur_node_sorted = nodes.front();
		auto hypothesis = member.hypotheses[cur_node_sorted];
		uint64_t occurerences = 0;
		for (auto match : hypothesis.matches) {
			occurerences += match.second;
		}
		std::cout << "node: " << locks2string(cur_node_sorted, " + ") << " , occurrences: " << occurerences << std::endl;
		std::cout << "children:" << std::endl;
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
			std::cout << "\t" << locks2string(child, " + ") << std::endl;
		}
		nodes.pop_front();
	}
}

void determine_winning_hypothesis(Member& member, void* param,
	void (*eval_cb)(Member&, void*, std::deque<std::vector<myid_t>>&, std::vector<myid_t>&, std::vector<myid_t>&, double&, double&),
	void (*init_cb)(Member&, void*, std::deque<std::vector<myid_t>>&, double&)) {
#ifdef DEBUG
	std::cout << "Member: " << member.accesstype << ":" << member.name << std::endl;
#endif
	std::deque<std::vector<myid_t>> nodes;

#ifdef DEBUG
	print_graph(member);
#endif

	/*
	 * Most of our algorithms use the root node (aka nolock hypothesis) 
	 * as default hypothesis.
	 * We therefore set it as the winner upon initialization.
	 * It has a relative support of 100%.
	 */
	double rel_support_winner = 1.0;
	member.conflict_list.clear();
	member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(Member::root_node,Member::root_node));
	member.winner_found = true;
	/*
	 * The the breadth-first search below only proceeds if 
	 * a new node (aka hypothesis) is better than the current one.
	 * Since we set the root node (aka the nolock hypothesis) as
	 * the winning hypothesis at the beginning, an algorithm would
	 * compare the root node with itself, and might stop immediatley. 
	 * We therefore init the list of nodes with the children of the root node.
	 * An algorithm is free to change this behavior by providing an init callback
	 * as shown by evaluate_hypothesis_init_topdown().
	 */
	for (auto child : member.graph[Member::root_node]) {
		nodes.push_back(child);
	}
	if (init_cb != NULL) {
		init_cb(member, param, nodes, rel_support_winner);
	}

	while (nodes.size() > 0) {
		std::vector<myid_t> cur_node_sorted = nodes.front();
		std::vector<myid_t> cur_node_ordered;
		auto hypothesis = member.hypotheses[cur_node_sorted];
		uint64_t max_occurerences = 0;

		// If there is more than hypothesis with these locks,
		// consider the lock order.
		// Choose the hypothesis with the highiest amonut of occurrences as representative.
		for (auto match : hypothesis.matches) {
			if (match.second > max_occurerences) {
				max_occurerences = match.second;
				cur_node_ordered = match.first;
			}
		}

		double cur_rel_sup =  (double)hypothesis.matches[cur_node_ordered] / (double)member.occurrences;
#ifdef DEBUG
		std::cout << "Examining: " << locks2string(cur_node_ordered, " -> ") << ", " << cur_rel_sup << "%" << "(" << hypothesis.matches[cur_node_ordered] << ")" << std::endl;
#endif
		eval_cb(member, param, nodes, cur_node_sorted, cur_node_ordered, rel_support_winner, cur_rel_sup);

		nodes.pop_front();
	}

#ifdef DEBUG
	std::cout << "conflict list length: " << member.conflict_list.size() << std::endl;
#endif

	if (member.winner_found) {
		auto winner = member.conflict_list.front();
		member.winning_hypothesis = winner.second;
#ifdef DEBUG
		std::cout << "Winning hypothesis:" << locks2string(member.winning_hypothesis, " -> ") << std::endl;
#endif
	} else {
#ifdef DEBUG
		std::cout << "No winning hypothesis found" << std::endl;
#endif
	}
}

void evaluate_hypothesis_bottomup(Member& member, void* param, std::deque<std::vector<myid_t>>& nodes,
	std::vector<myid_t>& cur_node_sorted, std::vector<myid_t>& cur_node_ordered, double& rel_support_winner, double& cur_rel_sup) {
	double accept_threshold = *(double*)param;

	if (member.conflict_list.front().first.size() == cur_node_sorted.size() && cur_rel_sup == rel_support_winner) {
		// We've found an equal hypothesis. Just save it.
		if (!in_conflict_list(member.conflict_list, cur_node_ordered)) {
			member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
#ifdef DEBUG
			std::cout << "adding to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		} else {
#ifdef DEBUG
			std::cout << "already in to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		}
		// Examine the children
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
#ifdef DEBUG
			std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
		}
	} else if ((accept_threshold <= cur_rel_sup && cur_rel_sup < rel_support_winner) ||
		(cur_node_sorted.size() > member.conflict_list.front().first.size() && cur_rel_sup == rel_support_winner)) {
		/*
		 * Found a better hypothesis:
		 * (rel. support is below current winner AND above accept threshold) OR
		 * (greater number of involved locks AND same rel. support)
		 */
#ifdef DEBUG
		std::cout << "Better node found. Resetting." << std::endl;
#endif
		member.winner_found = true;
		member.conflict_list.clear();
		member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
		rel_support_winner = cur_rel_sup;
		/*
		 * Since we've found a new potential winner, we'll examine
		 * the children.
		 * A child might have the same rel. support but
		 * more locks involved, for example.
		 */
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
#ifdef DEBUG
			std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
		}
	}
}

void evaluate_hypothesis_sharpen(Member& member, void* param, std::deque<std::vector<myid_t>>& nodes,
	std::vector<myid_t>& cur_node_sorted, std::vector<myid_t>& cur_node_ordered, double& rel_support_winner, double& cur_rel_sup) {
	double reduction_factor = *(double*)param;
	double delta = (1.0 - cur_rel_sup / rel_support_winner);

	if (member.conflict_list.front().first.size() == cur_node_sorted.size() && cur_rel_sup == rel_support_winner) {
		// We've found an equal hypothesis. Just save it.
		if (!in_conflict_list(member.conflict_list, cur_node_ordered)) {
			member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
#ifdef DEBUG
			std::cout << "adding to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		} else {
#ifdef DEBUG
			std::cout << "already in to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		}
#ifdef DEBUG
		std::cout << " adding to conflict list" << std::endl;
#endif
		// Examine the children
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
		}
	} else if ((member.conflict_list.front().first.size() == cur_node_sorted.size() && cur_rel_sup > rel_support_winner) ||
		   (cur_node_sorted.size() > member.conflict_list.front().first.size() &&
			  (cur_rel_sup == rel_support_winner || delta <= reduction_factor))) {
		/*
		 * Found a better hypothesis:
		 * (Same amount of locks AND higher rel. support)
		 * OR
		 * (More locks involved AND (same rel. support OR reduktion in rel. sup less or equal to reduction factor))
		 */
		member.winner_found = true;
		member.conflict_list.clear();
		member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
		rel_support_winner = cur_rel_sup;
		/*
		 * Since we've found a new potential winner, we'll examine
		 * the children.
		 * A child might have the same rel. support but
		 * more locks involved, for example.
		 */
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
		}
#ifdef DEBUG
		std::cout << " adding children" << std::endl;
#endif
	}
}

void evaluate_hypothesis_init_topdown(Member& member, void* param, std::deque<std::vector<myid_t>>& nodes,  double& rel_support_winner) {
	rel_support_winner = 0.0;
	member.winner_found = false;
	nodes.clear();
	nodes.push_back(Member::root_node);
}

void evaluate_hypothesis_lockset(Member& member, void* param, std::deque<std::vector<myid_t>>& nodes,
	std::vector<myid_t>& cur_node_sorted, std::vector<myid_t>& cur_node_ordered, double& rel_support_winner, double& cur_rel_sup) {
	if (cur_rel_sup < 1.0) {
		return;
	}

	if (member.conflict_list.front().first.size() == cur_node_sorted.size()) {
		// We've found an equal hypothesis. Just save it.
		if (!in_conflict_list(member.conflict_list, cur_node_ordered)) {
			member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
#ifdef DEBUG
			std::cout << "adding to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		} else {
#ifdef DEBUG
			std::cout << "already in to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
		}
#ifdef DEBUG
		std::cout << " adding to conflict list" << std::endl;
#endif
		// Examine the children
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
		}
	} else if (member.conflict_list.front().first.size() <= cur_node_sorted.size()) {
		/*
		 * Found a better hypothesis:
		 * (rel. support is 100.0) AND
		 * (greater or equal amount of locks are involved)
		 */
#ifdef DEBUG
		std::cout << "Better node found. Resetting: " <<  locks2string(cur_node_sorted, " -> ") << std::endl;
#endif
		member.winner_found = true;
		member.conflict_list.clear();
		member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
		/*
		 * Since we've found a new potential winner, we'll examine
		 * the children.
		 * A child might have the same rel. support but
		 * more locks involved, for example.
		 */
		for (auto child : member.graph[cur_node_sorted]) {
			nodes.push_back(child);
#ifdef DEBUG
			std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
		}
	}
}

void evaluate_hypothesis_topdown(Member& member, void* param, std::deque<std::vector<myid_t>>& nodes,
	std::vector<myid_t>& cur_node_sorted, std::vector<myid_t>& cur_node_ordered, double& rel_support_winner, double& cur_rel_sup) {

	double accept_threshold = ((double*)param)[0];
	double nolock_threshold = ((double*)param)[1];

	// handle accesses w/o locks
	double nolock_fraction =
		(double) (member.occurrences - member.occurrences_with_locks) /
		(double) member.occurrences;
	bool nolock_is_winner = nolock_fraction >= nolock_threshold;

	if (!nolock_is_winner) {
		if (cur_node_sorted == Member::root_node) {
			for (auto child : member.graph[cur_node_sorted]) {
				nodes.push_back(child);
#ifdef DEBUG
				std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
			}
		} else if (member.conflict_list.front().first.size() == cur_node_sorted.size() && cur_rel_sup == rel_support_winner) {
			// We've found an equal hypothesis. Just save it.
			if (!in_conflict_list(member.conflict_list, cur_node_ordered)) {
				member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
#ifdef DEBUG
				std::cout << "adding to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
			} else {
#ifdef DEBUG
				std::cout << "already in to conflict list: " << locks2string(cur_node_ordered, " -> ") << std::endl;
#endif
			}
		// Examine the children
			for (auto child : member.graph[cur_node_sorted]) {
				nodes.push_back(child);
#ifdef DEBUG
				std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
			}
		} else if (accept_threshold <= cur_rel_sup && cur_rel_sup >= rel_support_winner &&
			member.conflict_list.front().first.size() <= cur_node_sorted.size()) {
			/*
			 * Found a better hypothesis:
			 * (rel. support is above accept threshold AND greater equal the current winner) AND
			 * (greater or equal amount of locks are involved)
			 */
#ifdef DEBUG
			std::cout << "Better node found. Resetting: " <<  locks2string(cur_node_sorted, " -> ") << std::endl;
#endif
			member.winner_found = true;
			member.conflict_list.clear();
			member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(cur_node_sorted, cur_node_ordered));
			rel_support_winner = cur_rel_sup;
			/*
			 * Since we've found a new potential winner, we'll examine
			 * the children.
			 * A child might have the same rel. support but
			 * more locks involved, for example.
			 */
			for (auto child : member.graph[cur_node_sorted]) {
				nodes.push_back(child);
#ifdef DEBUG
				std::cout << "adding child to list: " << locks2string(child, " + ") << std::endl;
#endif
			}
		}
	} else {
		member.winner_found = true;
		if (member.conflict_list.size() == 0) {
			member.conflict_list.push_back(std::pair<std::vector<myid_t>, std::vector<myid_t>>(Member::root_node, Member::root_node));
		}
#ifdef DEBUG
		std::cout << "Nolock is the winner. Doing nothing." << std::endl;
#endif
	}
}

void add_hypothesis_to_graph(Member& member, const std::vector<myid_t>& hypothesis)
{
	if (hypothesis.size() == 0) {
		return;
	}
	std::deque<std::vector<myid_t>> nodes;
	nodes.push_back(Member::root_node);

	while (nodes.size() > 0) {
		auto& cur_node = nodes.front();
		//std::cout << "Using node:" << locks2string(cur_node, " + ") << std::endl;
		int ret = diff_hypotheses(cur_node, hypothesis);
		assert(ret != 0);
		if (ret > 0) {
			if (ret == 1) {
				//std::cout << "Adding: " << locks2string(hypothesis, " + ") << " to " << locks2string(cur_node, " + ") << std::endl;
				member.graph[cur_node].emplace(hypothesis);
			} else {
				for (auto&& child : member.graph[cur_node]) {
					nodes.push_back(child);
				}
			}
		}
		nodes.pop_front();
	}
}

void evaluate_hypothesis(Member& member, const std::vector<myid_t>& hypothesis)
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
			//std::cout << "Added: " << locks2string(lc.lock_order(hypothesis)) << " " << lc.occurrences << " occurrences" << std::endl;
		}
	}
	//std::cout << ", matches: " << matches.occurrences << std::endl;

	add_hypothesis_to_graph(member, hypothesis);
}

void find_hypotheses_rek(Member& member, const LockCombination& lc, unsigned next_lockpos, std::vector<myid_t>& cur, unsigned depth)
{
	if (depth == 0) {
		evaluate_hypothesis(member, cur);
		return;
	}
	for (unsigned lockpos = next_lockpos; lockpos < lc.locks_held_sorted.size(); ++lockpos) {
		if (!cur.empty() && cur.back() == lc.locks_held_sorted[lockpos]) {
			continue;
		}
		cur.push_back(lc.locks_held_sorted[lockpos]);
		//std::cout << "TESTING:" << locks2string(cur, " + ") << std::endl;
		find_hypotheses_rek(member, lc, lockpos + 1, cur, depth - 1);
		cur.pop_back();
	}
}

void find_hypotheses(Member& member, unsigned max_hypo_len)
{
	std::vector<myid_t> cur;

	//std::cout << "Member " << member.accesstype << ":" << member.name << std::endl;
	// depth = 0 -> evaluate the no-lock hypothesis
	// depth = 1 -> evaluate all one-lock hypotheses
	// depth = 2 -> evaluate all two-lock hypotheses
	// ...
	for (unsigned depth = 0; ; ++depth) {
		size_t prev_hypothesis_count = member.hypotheses.size();

		//std::cout << "depth " << depth << std::endl;

		for (const auto& lc : member.combinations) {
			//std::cout << locks2string(lc.locks_held_sorted, " + ") << std::endl;
			cur.clear();
			find_hypotheses_rek(member, lc, 0, cur, depth);
		}

		// if no additional hypotheses with requested depth found, we're done
		if (prev_hypothesis_count == member.hypotheses.size() || (max_hypo_len > 0 && depth >= max_hypo_len)) {
#ifdef DEBUG
			if (max_hypo_len == 0) {
				std::cerr << member.datatype << "." << member.accesstype << ":" << member.name << ": max. len " << depth << std::endl;
			}
#endif
			break;
		}
	}
}

// taken from Ebert et al. "Texturing and Modeling: A Procedural Approach", 3rd
// ed. (Morgan Kaufmann)
// (using double instead of float)
double smoothstep(double a, double b, double x)
{
	if (x < a)
		return 0;
	if (x >= b)
		return 1;
	x = (x - a) / (b - a);
	return x * x * (3 - 2 * x);
}

void print_bugsql(const std::string& prefix, const std::string& postfix,
	const Member& member, const std::vector<myid_t>& l, bool order_matters,
	uint64_t expected_counterexamples)
{
	if (expected_counterexamples > 0) {
		std::cout << prefix << "counterexample.sql.sh "
			<< member.datatype << " "
			<< member.combined_name() << " "
			<< "CEX "
			<< (order_matters ? "SEQ " : "ANY ")
			<< locks2string(l, " ", "'") << postfix;
	} else {
		std::cout << prefix
			<< "(no counterexamples to be expected, this hypothesis has 100% support in the observation set)"
			<< postfix;
	}
}

void print_hypotheses(const Member& member,
	double cutoff_threshold, ReportMode reportmode,
	bool bugsql, double confidence_threshold)
{
	if (reportmode == ReportMode::NORMAL) {
		std::cout << member.datatype << " member: "
			<< member.name << " [" << member.accesstype << "] ("
			<< member.combinations.size() << " lock combinations)" << std::endl;
		std::cout << "  hypotheses: " << member.hypotheses.size() << std::endl;
		std::cout << "  accesses without any locks:" << (member.occurrences - member.occurrences_with_locks) << std::endl;
	}

	//std::cout << "Graph for " << member.accesstype << ":" << member.name << " :" << std::endl;
	//for (auto entry : member.graph) {
	//	std::cout << locks2string(entry.first, " + ") << std::endl;
	//	for (auto child : entry.second) {
	//		const auto h = member.hypotheses.find(child)->second;
	//		std::cout << "\t\t\t" << locks2string(child, " + ") << ": " << std::setw(5) << (double) h.occurrences / (double) member.occurrences * 100 << " %" << std::endl;
	//	}
	//}

	// sort lock hypotheses by the number of memory accesses where each *set*
	// of locks is held (for now disregarding the lock order, this happens
	// within the output loop)
	std::vector<LockingHypothesisMatches> sorted_hypotheses;
	map2vec(member.hypotheses, sorted_hypotheses);
	sort(sorted_hypotheses.begin(), sorted_hypotheses.end(),
		[](const LockingHypothesisMatches& a, const LockingHypothesisMatches& b)
		{ return a.occurrences > b.occurrences || // ascending occurrences
			(a.occurrences == b.occurrences &&
			// more locks first: as we pick the first (= lowest-support)
			// hypothesis with a relative support >= accept_threshold as the
			// winner, this makes sure we get the most specific hypothesis (the
			// one with the most locks) if several with the same support exist
			a.sorted_hypothesis.size() > b.sorted_hypothesis.size());
		});

	// iterate over hypotheses from worst to best
	bool found_winner = false; // have we found a winner already?
	std::cout.precision(3);
	for (const auto& h : sorted_hypotheses) {
		// skip hypotheses with relative support below cutoff_threshold
		double relative_support = (double) h.occurrences / (double) member.occurrences;
		if (relative_support < cutoff_threshold) {
			continue;
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
				std::cout << "    " << std::setw(5) << relative_support * 100 << "% ("
					<< h.occurrences << " out of " << member.occurrences << " mem accesses): "
					<< locks2string(h.sorted_hypothesis, " + ") << std::endl;
				if (bugsql) {
					print_bugsql("    ", "\n", member, h.sorted_hypothesis, false,
						member.occurrences - h.occurrences);
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
				double local_fraction = (double) match.second / (double) h.occurrences;
				// fraction within all memory accesses
				relative_support = (double) match.second / (double) member.occurrences;

				bool this_is_the_winner = !found_winner && member.winning_hypothesis == match.first && member.winner_found;
				bool is_conflict = !this_is_the_winner && in_conflict_list(member.conflict_list, match.first) && member.winner_found;
				found_winner = found_winner || this_is_the_winner;

				if (reportmode == ReportMode::NORMAL) {
					std::string prefix;
					if (this_is_the_winner) {
						prefix += "!";
					} else {
						if (is_conflict) {
							prefix += "?";
						} else {
							prefix += " ";
						}
					}
					prefix += "      ";
					std::cout << prefix
						<< std::setw(5) << local_fraction * 100 << "% "
						 << locks2string(match.first) << std::endl;
					if (bugsql) {
						print_bugsql(prefix, "\n", member, match.first, true,
							member.occurrences - match.second);
					}
				} else if (reportmode == ReportMode::CSV ||
							(reportmode == ReportMode::CSVWINNER && 
							this_is_the_winner)) {
					std::cout << member.datatype << ";"
						<< member.name << ";"
						<< member.accesstype << ";"
						<< locks2string(match.first) << ";"
						<< match.second << ";"
						<< member.occurrences << ";"
						<< std::setprecision(5)
						<< relative_support * 100 << ";"
						<< (this_is_the_winner  ? 1 : (is_conflict ? 2 : 0))<< ";";
						std::cout << relative_support * smoothstep(0, confidence_threshold, match.second) << ";";
					print_bugsql("", "\n", member, match.first, true,
						member.occurrences - match.second);
				} else if (reportmode == ReportMode::DOC && this_is_the_winner) {
					// TODO properly group member r/w accesses
					doc_map[member.datatype][match.first].push_back(member.combined_name());
				}
			}
		} else {
			// only one locking order observed, show this one right away

			auto winner = h.matches.begin()->first;
			bool this_is_the_winner = !found_winner && member.winning_hypothesis == winner && member.winner_found;
			bool is_conflict = !this_is_the_winner && in_conflict_list(member.conflict_list, winner) && member.winner_found;
			found_winner = found_winner || this_is_the_winner;

			std::string prefix;
			if (this_is_the_winner) {
				prefix += "!";
			} else {
				if (is_conflict) {
					prefix += "?";
				} else {
					prefix += " ";
				}
			}
			prefix += "   ";
			std::cout << prefix
				<< std::setw(5) << relative_support * 100 << "% ("
				<< h.occurrences << " out of " << member.occurrences << " mem accesses): "
				<<  locks2string(winner) << std::endl;
			if (bugsql) {
				print_bugsql(prefix, "\n", member, winner, true,
					member.occurrences - h.occurrences);
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
	if (argc > 0) {
		argc--;
		argv++;
	}

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

	double reduction_factor = reduction_factor_default;
	if (options[REDUCTIONFACTOR]) {
		try {
			reduction_factor = std::stod(options[REDUCTIONFACTOR].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse reduction factor value " << options[REDUCTIONFACTOR].last()->arg << std::endl;
			return 1;
		}
		reduction_factor /= 100.0;
	}

	enum SelectionStrategy selection_strategy = SHARPEN;
	if (options[SELECTIONSTRATEGY]) {
		std::string criterion = options[SELECTIONSTRATEGY].last()->arg;
		if (criterion == "topdown") {
			selection_strategy = SelectionStrategy::TOPDOWN;
		} else if (criterion == "bottomup") {
			selection_strategy = SelectionStrategy::BOTTOMUP;
		} else if (criterion == "sharpen") {
			selection_strategy = SelectionStrategy::SHARPEN;
		} else if (criterion == "lockset") {
			selection_strategy = SelectionStrategy::LOCKSET;
		} else {
			std::cerr << "Unknown hypothesis sort criterion " << criterion << std::endl;
			return 1;
		}
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

	double nolock_threshold = nolock_threshold_default;
	if (options[NOLOCKTHRESHOLD]) {
		try {
			nolock_threshold = std::stod(options[NOLOCKTHRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse nolock threshold value " << options[NOLOCKTHRESHOLD].last()->arg << std::endl;
			return 1;
		}
		nolock_threshold /= 100.0;
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

	double confidence_threshold = confidence_threshold_default;
	if (options[CONFIDENCETHRESHOLD]) {
		try {
			confidence_threshold = std::stod(options[CONFIDENCETHRESHOLD].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse confidence threshold value " << options[CONFIDENCETHRESHOLD].last()->arg << std::endl;
			return 1;
		}
	}

	unsigned max_hypo_len = max_hypo_len_default;
	if (options[MAXHYPOLEN]) {
		try {
			max_hypo_len = std::stod(options[MAXHYPOLEN].last()->arg);
		} catch (const std::exception& e) {
			std::cerr << "Cannot parse max hypothesis length value " << options[MAXHYPOLEN].last()->arg << std::endl;
			return 1;
		}
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

			// Add lock combination (or increase its occurrence counter).  Note
			// that the "no locks held" case is not special-cased here, it ends
			// up as a zero-element lock-ID vector in members_combinations (and
			// later members[member_id].combinations).
			members[member_id].occurrences += occurrences;
			if (locks_held.size() > 0) {
				members[member_id].occurrences_with_locks += occurrences;
			}
			auto& combinations = members_combinations[member_id];
			auto ret = combinations.emplace(std::piecewise_construct,
				std::forward_as_tuple(locks_held), std::forward_as_tuple(occurrences, locks_held));
			if (ret.second == false) {
				// combination already existed
				ret.first->second.occurrences += occurrences;
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
	std::cerr << "Using strategy '";
	if (selection_strategy == SHARPEN) {
		std::cerr << "sharpen";
	} else if (selection_strategy == BOTTOMUP) {
		std::cerr << "bottomup";
	} else if (selection_strategy == TOPDOWN) {
		std::cerr << "topdown";
	} else if (selection_strategy == LOCKSET) {
		std::cerr << "lockset";
	} else {
		std::cerr << "unknown";
	}
	std::cerr << "' with acceptance threshold " << std::fixed << std::setprecision(2) << 100 * accept_threshold << ", and reduction factor " << 100 * reduction_factor << std::endl;
	if (max_hypo_len) {
		std::cerr << "Limiting hypothesis length to max " << max_hypo_len << " locks." << std::endl;
	}

	std::cerr << "Synthesizing lock hypotheses ..." << std::endl;

	if (reportmode == ReportMode::CSV || reportmode == ReportMode::CSVWINNER) {
		std::cout << "type;member;accesstype;locks;occurrences;total;percentage;accepted;confidence;counterexample-parameters\n";
	}

#pragma omp parallel for
//	for (auto&& member : members) {
	for (auto it = members.begin(); it < members.end(); ++it) {
		Member& member = *it;

		// Skip if user has specified datatypes + this one is not in the list
		if (accepted_datatypes.size() > 0) {
			const auto dataTypeName = member.datatype;
			const auto it = find_if(accepted_datatypes.cbegin(), accepted_datatypes.cend(),
					[&dataTypeName](const std::string& curDataTypeName) { return dataTypeName.find(curDataTypeName) == 0; } );
			if (it == accepted_datatypes.cend()) {
				member.clear();
				member.show = false;
				continue;
			}
		}

		// Skip if user has specified members + this one is not in the list
		if (accepted_members.size() > 0 &&
			accepted_members.find(member.name) == accepted_members.end() &&
			accepted_members.find(member.combined_name()) == accepted_members.end()) {
			member.clear();
			member.show = false;
			continue;
		}

		find_hypotheses(member, max_hypo_len);
		switch (selection_strategy) {
			case SelectionStrategy::SHARPEN:
				determine_winning_hypothesis(member, &reduction_factor, evaluate_hypothesis_sharpen, NULL);
				break;

			case SelectionStrategy::TOPDOWN:
				double param[2];
				param[0] = accept_threshold;
				param[1] = nolock_threshold;
				determine_winning_hypothesis(member, &param, evaluate_hypothesis_topdown, evaluate_hypothesis_init_topdown);
				break;

			case SelectionStrategy::BOTTOMUP:
				determine_winning_hypothesis(member, &accept_threshold, evaluate_hypothesis_bottomup, NULL);
				break;

			case SelectionStrategy::LOCKSET:
				determine_winning_hypothesis(member, &accept_threshold, evaluate_hypothesis_lockset, NULL);
				break;
		}

#pragma omp critical
{
		if (sortby == SortCriterion::NONE) {
			print_hypotheses(member, cutoff_threshold, reportmode, bugsql, confidence_threshold);

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
				print_hypotheses(member, cutoff_threshold, reportmode, bugsql, confidence_threshold);
			}
		}
	}

	if (reportmode == ReportMode::DOC) {
		std::cout << "/*\n";
		print_doc();
		std::cout << "*/\n";
	}
}
