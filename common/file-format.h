#ifndef FILE_FORMAT_H
#define FILE_FORMAT_H

#include "common/fs.h"
#include "common/file.h"
#include "common/str.h"
#include "common/array.h"

namespace Common {
class FileFormat {
public:
	virtual const Common::Array<String> getFileExtensions() {};
};

}; // namespace Common

#endif
