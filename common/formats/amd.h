#ifndef COMMON_FORMATS_AMD_H
#define COMMON_FORMATS_AMD_H

#include "common/array.h"
#include "common/formats/gmo.h"
#include "common/stream.h"

namespace Common {

/** Persona 4 Golden CHNK container carrying a GMO model. */
class AMD {
public:
	AMD(SeekableReadStream *stream);
	~AMD();

	bool ok() const { return _ok; }
	uint32 chunkCount() const { return _chunkCount; }
	GMO *getModel() { return _model; }

private:
	Common::Array<byte> _data;
	GMO *_model = nullptr;
	bool _ok = false;
	uint32 _chunkCount = 0;

	uint32 read32(const byte *p) const;
};

} // namespace Common

#endif
