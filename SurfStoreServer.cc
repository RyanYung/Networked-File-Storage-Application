#include <sysexits.h>
#include <string>

#include "rpc/server.h"

#include "logger.hpp"
#include "SurfStoreTypes.hpp"
#include "SurfStoreServer.hpp"

SurfStoreServer::SurfStoreServer(INIReader& t_config, int t_servernum)
    : config(t_config), servernum(t_servernum)
{
    auto log = logger();

	// pull our address and port
	string serverid = "server" + std::to_string(servernum);
	string servconf = config.Get("ssd", serverid, "");
	if (servconf == "") {
		log->error("{} not found in config file", serverid);
		exit(EX_CONFIG);
	}
	size_t idx = servconf.find(":");
	if (idx == string::npos) {
		log->error("Config line {} is invalid", servconf);
		exit(EX_CONFIG);
	}
	port = (int) strtol(servconf.substr(idx+1).c_str(), nullptr, 0);
	if (port <= 0 || port > 65535) {
		log->error("The port provided is invalid: {}", servconf);
		exit(EX_CONFIG);
	}
}

void SurfStoreServer::launch()
{
    auto log = logger();

    log->info("Launching SurfStore server");
    log->info("My ID is: {}", servernum);
    log->info("Port: {}", port);

    rpc::server srv(port);

    srv.bind("ping", []() {
            auto log = logger();
            log->info("ping()");
            return;
            });

    //TODO: get a block for a specific hash
    srv.bind("get_block", [&](string hash) {

            auto log = logger();
            log->info("get_block()");

            // if key does not exist in map
            if (blockStore.count(hash) <= 0)
            {
            string empty = "";
            log->error("Block doesn't exist");
            return empty;
            }
            return blockStore.at(hash);
            });

    // get all blockStore
    srv.bind("get_all_blocks", [&]() {
          auto log = logger();
          log->info("get_all_blocks()");
          return blockStore;
          });

    //TODO: store a block
    srv.bind("store_block", [&](string hash, string data) {

            auto log = logger();
            log->info("store_block()");

            blockStore[hash] = data;

            return;
            });

    //TODO: download a FileInfo Map from the server
    srv.bind("get_fileinfo_map", [&]() {
            auto log = logger();
            log->info("get_fileinfo_map()");

            return fileMap;
            });

    //TODO: update the FileInfo entry for a given file
    srv.bind("update_file", [&](string filename, FileInfo finfo) {
            log->info("updating file: {}", filename);
            // check if file exists in server (in fileMap)
            if (fileMap.count(filename) <= 0)
            {
            // if it doesn't, create new entry with version 1
            get<0>(finfo) = 1;
            fileMap[filename] = finfo;
            }

            else
            {
            FileInfo serverVer = fileMap.at(filename);
            // check if finfo version is server version + 1
            if (get<0>(serverVer) == get<0>(finfo) - 1)
            {
            fileMap[filename] = finfo;
            }
            }
    return;
    });

    // this rpc returns the FileInfo of the file filename
    srv.bind("file_version", [&](string filename) {
        return fileMap[filename];
    });

    // You may add additional RPC bindings as necessary

    srv.run();
}
