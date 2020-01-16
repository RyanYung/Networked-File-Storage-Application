
#ifndef SURFSTORESERVER_HPP
#define SURFSTORESERVER_HPP

#include "inih/INIReader.h"
#include "logger.hpp"
#include "SurfStoreTypes.hpp"
using namespace std;

class SurfStoreServer {
public:
    SurfStoreServer(INIReader& t_config, int t_servernum);

    void launch();

	const uint64_t RPC_TIMEOUT = 10000; // milliseconds

protected:
    INIReader& config;
	const int servernum;
	int port;
    unordered_map<string, string> blockStore; // hash table to store blocks
    FileInfoMap fileMap; // map to store files
};

#endif // SURFSTORESERVER_HPP
