// fast sort | uniq -c replacement
// g++ -O2 sortuniq.cc -o sortuniq

#include <iostream>
#include <string>
#include <set>
#include <cstdint>

int main()
{
	std::set<std::string> st;
	std::string s;
	while (getline(std::cin, s)) {
		st.insert(s);
	}
	for (auto elem : st) {
		std::cout << elem << '\n';
	}
}
