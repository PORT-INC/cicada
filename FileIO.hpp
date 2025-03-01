// © 2016 PORT INC.

#ifndef FILE_IO__H
#define FILE_IO__H

#include <sstream>
#include "Error.hpp"

template <class T>
void open(T& strm, const std::string& arg) {
	strm.open( arg );
	if( strm.fail() ) {
		std::stringstream ss;
		ss << "cannot open such file: " << arg;
		throw Error(ss.str());
	}
}

#endif // FILE_IO__H
