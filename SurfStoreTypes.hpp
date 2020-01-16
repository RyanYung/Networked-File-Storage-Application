#ifndef SURFSTORETYPES_HPP
#define SURFSTORETYPES_HPP

#include <tuple>
#include <map>
#include <list>
#include <string>

typedef tuple<int, list<string>> FileInfo;
typedef map<string, FileInfo> FileInfoMap;

#endif // SURFSTORETYPES_HPP
