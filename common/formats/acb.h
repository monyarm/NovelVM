#ifndef FORMAT_ACB_H
#define FORMAT_ACB_H

#include "common/archive.h"
#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {

class AWB;

/**
 * Archive reader for CRI Middleware's ACB (Atom Cue Sheet Binary) format:
 * a single @UTF table named "Header" with one row, where each column is a
 * vldata pointing to a nested @UTF sub-table describing the cue structure.
 *
 * Resolves cue names to audio waveforms stored in either an embedded AWB
 * (inside the AcbFile column) or an external companion .awb file. The cue
 * resolution chain supports direct (type 0x01) and synth (type 0x02) cues;
 * sequence (type 0x03) and block-sequence (type 0x08) are not yet supported.
 *
 * Real-data note: ACB files are found in every post-P4 CRI-middleware title
 * (P4G, P5, P5R, Catherine Full Body, Metaphor). The ACB is always paired
 * with either an embedded or external AWB containing the actual audio.
 */
class ACB : public Common::Archive {
public:
	ACB(const Common::String &filename);
	// For ACB data read out of another archive (e.g. a CPK). `awbStream`, if
	// given, is the paired companion AWB (external AWBs aren't autodetected
	// in this mode since there is no filename to derive one from).
	ACB(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeStream,
	    Common::SeekableReadStream *awbStream = nullptr, DisposeAfterUse::Flag disposeAWB = DisposeAfterUse::NO);
	~ACB() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

	struct LoopInfo {
		uint32 start = 0;
		uint32 end = 0;
		bool valid = false;
	};

	LoopInfo getLoopInfo(const Common::String &cueName) const;

	// Debug/test accessors.
	int numCues() const { return (int)_cues.size(); }
	int numWaveforms() const { return (int)_waveforms.size(); }
	bool hasAWB() const { return _awb != nullptr; }

private:
	struct CueInfo {
		Common::String name;
		uint16 waveformIndex = 0;
		bool hasLoop = false;
		uint32 loopStart = 0;
		uint32 loopEnd = 0;
	};

	struct WaveformInfo {
		uint16 id = 0;
		uint8 encodeType = 0;
		uint8 numChannels = 0;
		uint32 numSamples = 0;
		uint8 loopFlag = 0;
		int16 extensionDataIndex = -1;
		bool hasLoop = false;
		uint32 loopStart = 0;
		uint32 loopEnd = 0;
	};

	Common::String _filename;
	Common::Array<CueInfo> _cues;
	Common::HashMap<Common::String, uint32, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _cueIndex;
	Common::Array<WaveformInfo> _waveforms;
	Common::ScopedPtr<AWB> _awb;

	bool load();
	bool loadFromStream(Common::SeekableReadStream &f, Common::SeekableReadStream *awbStream, DisposeAfterUse::Flag disposeAWB);
};

ACB *makeACBArchive(const Common::String &name);

} // namespace Common

#endif
