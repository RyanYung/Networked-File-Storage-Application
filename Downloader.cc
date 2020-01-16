#include <sysexits.h>
#include <string>
#include <vector>
#include <iostream>
#include <assert.h>
#include <errno.h>

#include "rpc/server.h"
#include "rpc/rpc_error.h"
#include "picosha2/picosha2.h"

#include "logger.hpp"
#include "Downloader.hpp"

using namespace std;

    Downloader::Downloader(INIReader& t_config, int local)
: config(t_config)
{
    auto log = logger();

    // Read in the downloader's base directory
    base_dir = config.Get("downloader", "base_dir", "");
    if (base_dir == "") {
        log->error("Invalid base directory: {}", base_dir);
        exit(EX_CONFIG);
    }
    log->info("Using base_dir {}", base_dir);

    // Read in the block size
    blocksize = (int) config.GetInteger("downloader", "blocksize", -1);
    if (blocksize <= 0) {
        log->error("Invalid block size: {}", blocksize);
        exit(EX_CONFIG);
    }
    log->info("Using a block size of {}", blocksize);

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

    // mark which server is localserver
    localserver = local;
    log->info("Downloader initalized");
}

void Downloader::download()
{
    auto log = logger();

    vector<rpc::client*> clients;
    vector<unordered_map<string, string>> blockStores;

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

    // Issue a ping to each server
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Pinging server {}", i);
        try {
            clients[i]->call("ping");
            log->info("  success");
        } catch (rpc::timeout &t) {
            log->error("Error pinging server {}: {}", i, t.what());
            exit(-1);
        }
    }

    // Get file info map from local servers
    try{
        fileInfoMap = clients[localserver]->call("get_fileinfo_map").as<FileInfoMap>();
    } catch (rpc::rpc_error) {
        log->error("Error retrieving local server file info map");
    }

    // store all the average rtt times for each server
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
                log->error("ping time: {}", elapsed_seconds.count());
                totalTime += elapsed_seconds.count();
                log->info("  success");
            } catch (rpc::timeout &t) {
                log->error("Error pinging server {}: {}", i, t.what());
                exit(-1);
            }
        }
        rtt[i] = (totalTime / 8);
    }

    // Download all file blocks from shortest RTT servers
    for(int i = 0; i < 4; i++){
        log->error("RTT[{}] : {}", i, rtt[i]);
    }

    int shortestRTT[4];

    double shortestTime = 99999;
    double lastShortestTime = -1;

    for(int i = 0; i < num_servers; i++){
        shortestTime = 99999;
        for(int j = 0; j < num_servers; j++){
            if(rtt[j] < shortestTime && rtt[j] > lastShortestTime){
                shortestRTT[i] = j;
                shortestTime = rtt[j];
            }
        }
        lastShortestTime = shortestTime;
    }

    for(int i = 0; i < 4; i++){
        log->info("shortestRTT[{}] : {}", i, shortestRTT[i]);
    }

    // Get list of blocks on num_servers
    for (int i = 0; i < num_servers; i++){
        try{
            // stores hashmap lists in order
            blockStores.push_back(clients[shortestRTT[i]]->call("get_all_blocks").as<unordered_map<string, string>>());
        } catch (rpc::rpc_error) {
            log->error("Error getting blocks from server {}", i);
        }
    }

    // store blocks downloaded from servers in this unordered_map
    unordered_map<string, string> blockStore;

    start = chrono::system_clock::now();

    // loop through all files
    for (auto file: fileInfoMap)
    {
        // loop through hash list of file
        for (auto hash: get<1>(file.second))
        {
            bool found = false;
            // go through each server (in order)
            for (int i = 0; i < 4; i++)
            {
                // loop through blocks in each server's blocks
                for (auto hashServer: blockStores.at(i))
                {
                    // same block
                    if (!hash.compare(hashServer.first))
                    {
                        log->info("downloading block from server {}", shortestRTT[i]);
                        // download block from server
                        blockStore[hash] = clients[shortestRTT[i]]->call("get_block", hash).as<string>();
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    found = false;
                    break;
                }
            }
        }
    }

    end = chrono::system_clock::now();
    elapsed_seconds = (end - start);
    log->error("download time: {}", elapsed_seconds.count());

    // loop through all files
    for (auto file: fileInfoMap)
    {
        ofstream myfile;
        string filepath = base_dir + "/" + file.first;
        // overwrite
        myfile.open(filepath, ios::trunc);

        // loop through hash list
        for (auto hash: get<1>(file.second))
        {
            // get block from blockStore, add to myfile
            myfile << blockStore[hash];
        }
        myfile.close();
    }

    // Delete the clients
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Tearing down client {}", i);
        delete clients[i];
    }
}
