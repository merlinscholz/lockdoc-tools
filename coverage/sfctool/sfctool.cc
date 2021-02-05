#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <memory>

#include <cstring>
#include <cmath>

#include <png++/png.hpp>

#include "optionparser.h"
#include "optionparser_ext.h"

using namespace std::string_literals;

enum optionIndex { UNKNOWN, HELP, CURVE_TYPE, OFF_MAP, ON_MAP, COLOR, DEFAULT_COLOR };
const option::Descriptor usage[] = {
{
  UNKNOWN, 0, "", "", Arg::None,
  "Usage: sfctool [options] domain.map input.map output.png\n\n"
  "sfctool iterates over all strings s at line i in domain.map "
  "and picks the color for img[curve[i]] as follows:\n"
  " -  If string s is contained in input.map (e.g. the basic block is \"covered\"),\n"
  "    pick the color specified by on_map[s].\n"
  " -  If string s is NOT contained in input.map, pick the color specified by\n"
  "    off_map[s].\n"
  " -  If on_map/off_map does not specify a color for s, pick the --default-color.\n"
  "\nOptions:"
}, {
  HELP, 0, "h", "help", Arg::None, "--help  \tPrint usage and exit"
}, {
  CURVE_TYPE, 0, "t", "curve-type", Arg::Required,
  "-t/--curve-type type  \tSet SFC type: lines|spiral|hilbert|z; defaults to lines."
}, {
  OFF_MAP, 0, "0", "off-map", Arg::Required,
  "-0/--off-map file  \tList of strings that get assigned the subsequent --color if they are NOT present in the main input file; may be used more than once."
}, {
  ON_MAP, 0, "1", "on-map", Arg::Required,
  "-1/--on-map file  \tList of strings that get assigned the subsequent --color if they are present in the main input file; may be used more than once."
}, {
  COLOR, 0, "c", "color", Arg::Required,
  "-c/--color RRGGBB  \tSet color for the preceding --off/on-map."
}, {
  DEFAULT_COLOR, 0, "d", "default-color", Arg::Required,
  "-d/--default-color RRGGBB  \tSet default color, defaults to FFFFFF."
}, {0,0,0,0,0,0}
};

struct Color {
	uint8_t r = 0xff, g = 0, b = 0xff;

	Color() {}

	Color(char const *s)
	{
		set_color(s);
	}

	void set_color(char const *s)
	{
		if (s[0] == '#') {
			++s;
		}
		if (std::strlen(s) != 6) {
			throw std::invalid_argument("color must be in the format RRGGBB");
		}
		std::string rs(s  , 2);
		std::string gs(s+2, 2);
		std::string bs(s+4, 2);
		r = std::stoul(rs, 0, 16);
		g = std::stoul(gs, 0, 16);
		b = std::stoul(bs, 0, 16);
	}

	void get_components(uint8_t& r, uint8_t& g, uint8_t& b)
	{
		r = this->r;
		g = this->g;
		b = this->b;
	}
};

class SFC {
protected:
	unsigned dim;
public:
	SFC(unsigned dim) : dim(dim) {}
	virtual ~SFC() {}
	virtual void reset() = 0;
	virtual void get_coords(unsigned& x, unsigned& y) = 0;
	virtual void next() = 0;
};

class SFC_Lines : public SFC {
	unsigned pos = 0;
public:
	SFC_Lines(unsigned dim) : SFC(dim) { }
	void reset() override
	{
		pos = 0;
	}
	void get_coords(unsigned& x, unsigned& y) override
	{
		x = pos % dim;
		y = pos / dim;
	}
	void next() override
	{
		++pos;
	}
};

class SFC_Spiral : public SFC {
	unsigned x, y;
	unsigned state, nsteps, step;
public:
	SFC_Spiral(unsigned dim) : SFC(dim) { reset(); }
	void reset() override
	{
		state = 0;
		nsteps = 1;
		step = 0;
		x = dim / 2;
		y = dim / 2;
	}
	void get_coords(unsigned& x, unsigned& y) override
	{
		x = this->x;
		y = this->y;
	}
	void next() override
	{
		switch (state) {
		case 0:
			--y;
			break;
		case 1:
			--x;
			break;
		case 2:
			++y;
			break;
		case 3:
			++x;
			break;
		}
		++step;
		if (step == nsteps) {
			++state;
			step = 0;
			if (state == 2) {
				++nsteps;
			} else if (state == 4) {
				++nsteps;
				state = 0;
			}
		}
	}
};

class SFC_Hilbert : public SFC {
	unsigned pos = 0;
public:
	SFC_Hilbert(unsigned dim) : SFC(dim) { }
	void reset() override
	{
		pos = 0;
	}

	void get_coords(unsigned& x, unsigned& y) override
	{
		// https://en.wikipedia.org/wiki/Hilbert_curve
		unsigned rx, ry, s, t = pos;
		x = y = 0;
		for (s = 1; s < dim; s *= 2) {
			rx = 1 & (t / 2);
			ry = 1 & (t ^ rx);
			rot(s, x, y, rx, ry);
			x += s * rx;
			y += s * ry;
			t /= 4;
		}
    }

	void next() override
	{
		++pos;
	}

private:
	//rotate/flip a quadrant appropriately
	void rot(unsigned n, unsigned& x, unsigned& y, unsigned rx, unsigned ry)
	{
		if (ry == 0) {
			if (rx == 1) {
				x = n-1 - x;
				y = n-1 - y;
			}

			//Swap x and y
			unsigned t = x;
			x = y;
			y = t;
		}
	}
};

Color default_color("ffffff");
std::vector<std::string> domain_strings;
std::map<std::string, Color> off_map, on_map;
std::set<std::string> input;

void loadmap(char const *filename, std::map<std::string, Color>& m, Color color)
{
	std::ifstream f(filename);
	if (!f) {
		throw std::ios_base::failure("cannot open file");
	}

	std::string buf;
	while (std::getline(f, buf)) {
		m[buf] = color;
	}
}

int main(int argc, char **argv)
{
	// skip program name in argv[0]
	if (argc) {
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
			argc == 0 || parse.nonOptionsCount() != 3) {
		option::printUsage(std::cout, usage);
		return options[HELP] ? 0 : 1;
	}

	if (options[DEFAULT_COLOR]) {
		default_color.set_color(options[DEFAULT_COLOR].arg);
	}

	const char *filename_domain = parse.nonOption(0);
	const char *filename_input = parse.nonOption(1);
	const char *filename_pngoutput = parse.nonOption(2);

	{
	std::ifstream f_domain(filename_domain);
	if (!f_domain) {
		std::cerr << "failed to open " << filename_domain << "\n";
		return 1;
	}
	std::string buf;
	while (std::getline(f_domain, buf)) {
		domain_strings.push_back(buf);
	}
	std::cout << "domain size: " << domain_strings.size() << "\n";
	}

	{
	std::ifstream f_input(filename_input);
	if (!f_input) {
		std::cerr << "failed to open " << filename_input << "\n";
		return 1;
	}
	std::string buf;
	while (std::getline(f_input, buf)) {
		input.insert(buf);
	}
	std::cout << "input size: " << input.size() << "\n";
	}

	std::unique_ptr<SFC> sfc;
	unsigned image_dim = std::ceil(std::sqrt(domain_strings.size()));
	if (options[CURVE_TYPE]) {
		char const *s = options[CURVE_TYPE].arg;
		if (s == "lines"s) {
			sfc = std::make_unique<SFC_Lines>(image_dim);
		} else if (s == "spiral"s) {
			sfc = std::make_unique<SFC_Spiral>(image_dim);
		} else if (s == "hilbert"s) {
			sfc = std::make_unique<SFC_Hilbert>(image_dim);
			// Hilbert curve needs image_dim to be a power of 2
			unsigned x = 1;
			while (x < image_dim) {
				x *= 2;
			}
			image_dim = x;
		} else {
			std::cerr << "curve type " << s << " not implemented\n";
			return 1;
		}
	} else {
		sfc = std::make_unique<SFC_Lines>(image_dim);
	}

	std::cout << "image size: " << image_dim << "x" << image_dim << "\n";
	png::image<png::rgb_pixel> image(image_dim, image_dim);
	for (unsigned y = 0; y < image_dim; ++y) {
		for (unsigned x = 0; x < image_dim; ++x) {
			image[y][x] = png::rgb_pixel(default_color.r, default_color.b, default_color.g);
		}
	}

	option::Option *coloropt = options[COLOR];
	for (option::Option *mapopt = options[OFF_MAP]; mapopt; mapopt = mapopt->next()) {
		if (!coloropt) {
			std::cerr << "more --off/on-map than --color parameters\n";
			return 1;
		}
		Color color(coloropt->arg);
		coloropt = coloropt->next();
		
		loadmap(mapopt->arg, off_map, color);
	}

	for (option::Option *mapopt = options[ON_MAP]; mapopt; mapopt = mapopt->next()) {
		if (!coloropt) {
			std::cerr << "more --off/on-map than --color parameters\n";
			return 1;
		}
		Color color(coloropt->arg);
		coloropt = coloropt->next();
		
		loadmap(mapopt->arg, on_map, color);
	}

	if (coloropt) {
		std::cerr << "more --color than --off/on-map parameters\n";
		return 1;
	}

	for (unsigned i = 0; i < domain_strings.size(); ++i) {
		unsigned x, y;
		sfc->get_coords(x, y);
		sfc->next();

		auto& s = domain_strings[i];
		if (input.find(s) == input.end()) {
			auto it = off_map.find(s);
			if (it != off_map.end()) {
				image[y][x] = png::rgb_pixel(it->second.r, it->second.g, it->second.b);
			} // else: nothing to do, image is already filled with default_color
		} else {
			auto it = on_map.find(s);
			if (it != on_map.end()) {
				image[y][x] = png::rgb_pixel(it->second.r, it->second.g, it->second.b);
			} else {
				// nothing to do, image is already filled with default_color
				std::cerr << "warning: string '" << s << "' occurs in input but has no color assigned\n";
			}
		}
	}

	image.write(filename_pngoutput);
}
