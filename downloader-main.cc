#include <iostream>
#include <thread>
#include <sysexits.h>
#include <stdlib.h>

#include "inih/INIReader.h"
#include "rpc/server.h"

#include "logger.hpp"
#include "Downloader.hpp"

using namespace std;

int main(int argc, char** argv) {
	initLogging();
    spdlog::set_level(spdlog::level::err);
	auto log = logger();

	// Handle the command-line argument, config and what server is running it
	if (argc < 3) {
		cerr << "Usage: " << argv[0] << " [config_file]" << " [localserver]" << endl;
		return EX_USAGE;
	}

	// Read in the configuration file
	INIReader config(argv[1]);

	if (config.ParseError() < 0) {
		cerr << "Error parsing config file " << argv[1] << endl;
		return EX_CONFIG;
	}

	// pass in the config and what server is downloading

	Downloader c(config, stoi(argv[2]));
	c.download();

	return 0;
}
