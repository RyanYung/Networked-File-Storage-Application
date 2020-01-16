#include <sysexits.h>
#include <string>
#include <vector>
#include <iostream>
#include <assert.h>
#include <errno.h>
#include <chrono>
#include <dirent.h>

#include "rpc/server.h"
#include "rpc/rpc_error.h"
#include "picosha2/picosha2.h"

#include "logger.hpp"
#include "Uploader.hpp"

using namespace std;

    Uploader::Uploader(INIReader& t_config, int localIndex)
: config(t_config)
{
    auto log = logger();

    // set local server
    local = localIndex;
    // Read in the uploader's base directory
    base_dir = config.Get("uploader", "base_dir", "");
    if (base_dir == "") {
        log->error("Invalid base directory: {}", base_dir);
        exit(EX_CONFIG);
    }
    log->info("Using base_dir {}", base_dir);

    // Read in the block size
    blocksize = (int) config.GetInteger("uploader", "blocksize", -1);
    if (blocksize <= 0) {
        log->error("Invalid block size: {}", blocksize);
        exit(EX_CONFIG);
    }
    log->info("Using a block size of {}", blocksize);

    // Read in the uploader's block placement policy
    policy = config.Get("uploader", "policy", "");
    if (policy == "") {
        log->error("Invalid placement policy: {}", policy);
        exit(EX_CONFIG);
    }
    log->info("Using a block placement policy of {}", policy);

    num_servers = (int) config.GetInteger("ssd", "num_servers", -1);
    if (num_servers <= 0) {
        log->error("num_servers {} is invalid", num_servers);
        exit(EX_CONFIG);
    }
    log->info("Number of servers: {}", num_servers);

    for (int i = 0; i < num_servers; ++i) {
        string servconf = config.Get("ssd", "server"+std::to_string(i), "");
        if (servconf == "") {
            log->error("Server {} not found in config file", i);
            exit(EX_CONFIG);
        }
        size_t idx = servconf.find(":");
        if (idx == string::npos) {
            log->error("Config line {} is invalid", servconf);
            exit(EX_CONFIG);
        }
        string host = servconf.substr(0, idx);
        int port = (int) strtol(servconf.substr(idx+1).c_str(), nullptr, 0);
        if (port <= 0 || port > 65535) {
            log->error("Invalid port number: {}", servconf);
            exit(EX_CONFIG);
        }

        log->info("  Server {}= {}:{}", i, host, port);
        ssdhosts.push_back(host);
        ssdports.push_back(port);
    }

    log->info("Uploader initalized");
}

void Uploader::upload()
{
    auto log = logger();

    vector<rpc::client*> clients;

    // Connect to all of the servers
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Connecting to server {}", i);
        try {
            clients.push_back(new rpc::client(ssdhosts[i], ssdports[i]));
            clients[i]->set_timeout(RPC_TIMEOUT);
        } catch (rpc::timeout &t) {
            log->error("Unable to connect to server {}: {}", i, t.what());
            exit(-1);
        }
    }

    chrono::time_point<chrono::system_clock> start, end;
    chrono::duration<double> elapsed_seconds;
    double totalTime = 0;
    double rtt [4];

    // Issue a ping to each server 8 times
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Pinging server {}", i);
        totalTime = 0;
        for (int j = 0; j < 8; j++)
        {
            try {
                start = chrono::system_clock::now();
                clients[i]->call("ping");
                end = chrono::system_clock::now();
                elapsed_seconds = (end - start);
                log->info("ping time: {}", elapsed_seconds.count());
                totalTime += elapsed_seconds.count();
                log->info("  success");
            } catch (rpc::timeout &t) {
                log->error("Error pinging server {}: {}", i, t.what());
                exit(-1);
            }
        }
        rtt[i] = (totalTime / 8);
    }

    for (int i = 0; i < num_servers; ++i)
    {
        log->info("average ping time for server {}: {}", i, rtt[i]);
    }

    // create FileInfoMap for files in base directory
    FileInfoMap clientMap;

    // iterate through directory to get list of filenames
    DIR* dirp = opendir(base_dir.c_str());
    struct dirent * dp;
    while ((dp = readdir(dirp)) != NULL) {
        // make sure file exists
        string str(dp->d_name);
        if(str.compare(".") != 0 && str.compare("..") != 0 && str.compare("index.txt")){
            // create fileinfo for file, add to clientMap
            clientMap[dp->d_name] = make_tuple(1, create_fileinfo(dp->d_name));
        }
    }
    closedir(dirp);

    // upload files and blocks using specified policy
    policySelector(clientMap, rtt, clients);

    // Delete the clients
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Tearing down client {}", i);
        delete clients[i];
    }
}

// returns list of hash values of blocks for given filename
list<string> Uploader::create_fileinfo(string filename) {
    ifstream file;
    int size;
    int filePos = 0;
    char * data = new char[blocksize];
    list<string> str_list;
    string filepath = base_dir + "/" + filename;

    file.open(filepath, ios::in|ios::binary);
    if (file.is_open()) {
        file.seekg(0, ios::end);
        size = file.tellg();
        file.seekg(0,file.beg);

        //Entire file is less than 4096 bytes so it fits into one block
        if (size < blocksize){
            file.read(data, size);
            string s = string(data, size);
            string key = picosha2::hash256_hex_string(s);
            str_list.push_back(key);
            // store block in blockStore map
            blockStore[key] = s;
            return str_list;
        }
        //Entire file larger than 4096 bytes so it needs multiple blocks
        else {
            while(filePos != size){
                // Less than 4096 bytes left to process
                if(size - filePos < blocksize) {
                    file.read(data, size - filePos);
                    string s = string(data, size - filePos);
                    string key = picosha2::hash256_hex_string(s);
                    str_list.push_back(key);
                    // store block in blockStore map
                    blockStore[key] = s;
                    return str_list;
                }
                // More than 4096 bytes left to process
                else {
                    file.read(data, blocksize);
                    string s = string(data, blocksize);
                    string key = picosha2::hash256_hex_string(s);
                    str_list.push_back(key);
                    // store block in blockStore map
                    blockStore[key] = s;
                    filePos += blocksize;
                }
            }
        }
    }
    else {
        return str_list;
    }
    return str_list;
}

void Uploader::policyRandom(FileInfoMap clientMap, vector<rpc::client*> clients)
{
    auto log = logger();
    // reset random seed
    srand(time(NULL));
    // loop through each file
    for (auto file: clientMap)
    {
        // loop through each block in each file
        for (auto hash: get<1>(file.second))
        {
            string data = blockStore[hash];
            int clientIndex = rand() % num_servers;
            log->info("storing block in server {}", clientIndex);
            clients[clientIndex]->call("store_block", hash, data);
        }

        // update file for every server
        for (int i = 0; i < num_servers; i++)
        {
            clients[i]->call("update_file", file.first, file.second);
        }
    }
}

void Uploader::policyTwoRandom(FileInfoMap clientMap, vector<rpc::client*> clients)
{
    auto log = logger();
    // reset random seed
    srand(time(NULL));
    // loop through each file
    for (auto file: clientMap)
    {
        // loop through each block in each file
        for (auto hash: get<1>(file.second))
        {
            string data = blockStore[hash];
            int clientIndex = rand() % num_servers;
            int clientIndex2 = rand() % num_servers;
            // make sure index is different
            while (clientIndex == clientIndex2)
            {
                clientIndex2 = rand() % num_servers;
            }
            log->info("storing block in servers {} and {}", clientIndex, clientIndex2);
            clients[clientIndex]->call("store_block", hash, data);
            clients[clientIndex2]->call("store_block", hash, data);
        }

        // update file for every server
        for (int i = 0; i < num_servers; i++)
        {
            clients[i]->call("update_file", file.first, file.second);
        }
    }
}

void Uploader::policyLocal(FileInfoMap clientMap, vector<rpc::client*> clients)
{
    auto log = logger();
    // loop through each file
    for (auto file: clientMap)
    {
        // loop through each block in each file
        for (auto hash: get<1>(file.second))
        {
            string data = blockStore[hash];
            log->info("storing block in server {}", local);
            // store in local server
            clients[local]->call("store_block", hash, data);
        }

        // update file for every server
        for (int i = 0; i < num_servers; i++)
        {
            clients[i]->call("update_file", file.first, file.second);
        }
    }
}

void Uploader::policyLocalClosest(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients)
{
    auto log = logger();
    double min = 1000;
    int index = -1; 
    for (int i = 0; i < num_servers; i++) {
        if (rtt[i] < min && i != local) {
            min = rtt[i];
            index = i;
        }
    }    

    for (auto file: clientMap) {
        for (auto hash: get<1>(file.second)) {
            string data = blockStore[hash];
            log->info("storing block in servers {} and {}", local, index);
            clients[local]->call("store_block", hash, data);
            clients[index]->call("store_block", hash, data);
        }
        for (int i = 0; i < num_servers; i++) {
            clients[i]->call("update_file", file.first, file.second);
        }
    }  
}

void Uploader::policyLocalFarthest(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients)
{
    auto log = logger();
    double max = 0;
    int index = -1; 
    for (int i = 0; i < num_servers; i++) {
        if(rtt[i] > max && i != local) {
            max = rtt[i];
            index = i;
        }
    }

    for (auto file: clientMap) {
        for (auto hash: get<1>(file.second)) {
            string data = blockStore[hash];
            log->info("storing block in servers {} and {}", local, index);
            clients[local]->call("store_block", hash, data);
            clients[index]->call("store_block", hash, data);
        }
        for (int i = 0; i < num_servers; i++) {
            clients[i]->call("update_file", file.first, file.second);
        }
    }
}

void Uploader::policySelector(FileInfoMap clientMap, double rtt[], vector<rpc::client*> clients)
{
    auto log = logger();
    if(policy.compare("random") == 0) {
        policyRandom(clientMap, clients);
        return;    
    }
    else if(policy.compare("tworandom") == 0) {
        policyTwoRandom(clientMap, clients);
        return;
    }
    else if(policy.compare("local") == 0) {
        policyLocal(clientMap, clients);
        return;
    }
    else if(policy.compare("localclosest") == 0) {
        policyLocalClosest(clientMap, rtt, clients);
        return;
    }
    else if(policy.compare("localfarthest") == 0) {
        policyLocalFarthest(clientMap, rtt, clients);
        return;
    }
    else {
        log->error("invalid policy");
        return;
    }
}
