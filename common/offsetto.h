#ifndef COMMON_OFFSETTO
#define COMMON_OFFSETTO

#include "scummsys.h"
namespace Common {
template<typename T>
class OffsetTo {
public:
	uint32 Offset;
	T Value;

	OffsetTo() {
		Offset = 0;
	}

	OffsetTo(uint32 offset) {
		Offset = offset;
	}

	OffsetTo(uint32 address, T value) {
		Offset = address;
		Value = value;
	}
};

} // namespace Common

#endif