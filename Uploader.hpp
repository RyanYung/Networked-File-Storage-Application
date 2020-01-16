#ifndef UPLOADER_HPP
#define UPLOADER_HPP

#include <string>
#include <vector>

#include "inih/INIReader.h"
#include "rpc/client.h"

#include "SurfStoreTypes.hpp"
#include "logger.hpp"

using namespace std;

class Uploader {
public:

    Uploader(INIReader& t_config, int local);

	void upload();

	const uint64_t RPC_TIMEOUT = 10000; // milliseconds

    list<string> create_fileinfo(string filename);

    void policySelector(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients);
    void policyRandom(FileInfoMap clientMap, vector<rpc::client*> clients);
    void policyTwoRandom(FileInfoMap clientMap, vector<rpc::client*> clients);
    void policyLocal(FileInfoMap clientMap, vector<rpc::client*> clients);
    void policyLocalClosest(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients);
    void policyLocalFarthest(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients);
          
protected:

    INIReader& config;

	string base_dir;
	int blocksize;
	string policy;

	int num_servers;
	vector<string> ssdhosts;
	vector<int> ssdports;

    int local; // index of local server
    unordered_map<string, string> blockStore; // store blocks
};

#endif // UPLOADER_HPP
