
CXX=g++
CXXFLAGS=-std=c++11 -ggdb -Wall -Wextra -pedantic -Werror -Wnon-virtual-dtor -I../dependencies/include
SERVEROBJS= server-main.o logger.o SurfStoreServer.o
UPLOADEROBJS= uploader-main.o logger.o Uploader.o
DOWNLOADEROBJS= downloader-main.o logger.o Downloader.o

default: ssd uploader downloader

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

uploader: $(UPLOADEROBJS) logger.hpp SurfStoreTypes.hpp Uploader.hpp
	$(CXX) $(CXXFLAGS) -o uploader $(UPLOADEROBJS) -L../dependencies/lib -pthread -lrpc

downloader: $(DOWNLOADEROBJS) logger.hpp SurfStoreTypes.hpp Downloader.hpp
	$(CXX) $(CXXFLAGS) -o downloader $(DOWNLOADEROBJS) -L../dependencies/lib -pthread -lrpc

ssd: $(SERVEROBJS) logger.hpp SurfStoreServer.hpp SurfStoreTypes.hpp
	$(CXX) $(CXXFLAGS) -o ssd $(SERVEROBJS) -L../dependencies/lib -pthread -lrpc

.c.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f uploader downloader ssd *.o
