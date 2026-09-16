#ifndef IKURA_SCRIPT_CONTEXT_H
#define IKURA_SCRIPT_CONTEXT_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "ikura/formats/script/script.h"

namespace Ikura {
class Input;
}

namespace Ikura::Runtime {
class Presentation;
class Audio;
}

namespace Ikura::VM {

// VM state for one running script: its Format::Script::Script (bytecode,
// jump table, call/return position stack), plus the Ikura/GDL value model
// (VileVN reference: IParser, src/ikura/iparser.cpp) - growable
// variable/flag stores and the value encoding every opcode operand uses.
class Context {
public:
	// Takes ownership of script.
	explicit Context(Format::Script::Script *script) : _script(script) {}
	~Context() {
		delete _script;
		for (uint32 i = 0; i < _scriptStack.size(); i++)
			delete _scriptStack[i];
	}

	Format::Script::Script &script() { return *_script; }

	// An operand is a 32-bit field: if bit 31 is set, the low 31 bits name
	// a variable to look up instead of being the value itself. Either way
	// the result is sign-extended from bit 30 (VileVN's own "Uint31").
	int32 decodeValue(uint32 raw) const;

	int32 getValue(uint32 position) const;
	void setValue(uint32 position, uint32 raw);

	bool getFlag(uint32 position) const;
	void setFlag(uint32 position, bool value);

	// System flags: a second, separate flag bank (VileVN reference:
	// IParser::System) for engine settings (message skip, automode, BGM
	// disable, etc.) rather than story state.
	bool getSystem(uint32 position) const;
	void setSystem(uint32 position, bool value);

	// Not owned; nullptr in a headless/display-less context (e.g. tests),
	// in which case IOP_GL/IOP_GP just no-op instead of crashing.
	void setPresentation(Runtime::Presentation *presentation) { _presentation = presentation; }
	Runtime::Presentation *presentation() const { return _presentation; }

	// Same nullptr-safe contract as presentation() above.
	void setAudio(Runtime::Audio *audio) { _audio = audio; }
	Runtime::Audio *audio() const { return _audio; }

	// IOP_TIMERSET/IOP_TIMERGET: a single stopwatch start tick (VileVN
	// reference: IkuraDecoder::timerstart), in the same units as
	// OSystem::getMillis().
	void setTimerStart(uint32 ms) { _timerStart = ms; }
	uint32 timerStart() const { return _timerStart; }

	// IOP_ATIMES (VileVN reference: IkuraDecoder::delayvalue). Milliseconds
	// consumed by IOP_AWAIT to compute its own wait deadline.
	void setDelayValue(uint32 ms) { _delayValue = ms; }
	uint32 delayValue() const { return _delayValue; }

	// IOP_GO's wall-clock wait deadline (VileVN reference: IkuraDecoder::
	// timerend under IS_WAITTIMER), same units as OSystem::getMillis().
	// Read by IkuraEngine's main loop to know when a StepResult::
	// kWaitingForTimer pause is over.
	void setWaitDeadline(uint32 ms) { _waitDeadline = ms; }
	uint32 waitDeadline() const { return _waitDeadline; }

	// Same nullptr-safe contract as presentation() above. IOP_IG reads
	// advance/cancel through this.
	void setInput(Input *input) { _input = input; }
	Input *input() const { return _input; }

	// Same nullptr-safe contract as presentation() above. IOP_LS/IOP_LSBS
	// look up other named ".ISF" members through this - the same cabinet
	// loadTitleScript() got TITLE.ISF from. Not owned; must outlive this
	// Context.
	void setScriptCabinet(Common::Archive *cabinet) { _scriptCabinet = cabinet; }
	Common::Archive *scriptCabinet() const { return _scriptCabinet; }

	// IOP_LS (VileVN reference: IkuraDecoder::iop_ls / IParser::
	// JumpScript). Replaces the running script outright - no way back to
	// it (unlike callScript()). Takes ownership of script.
	void jumpScript(Format::Script::Script *script);

	// IOP_LSBS (VileVN reference: IkuraDecoder::iop_lsbs / IParser::
	// CallScript). Suspends the running script on a stack and switches to
	// script; a matching returnScript() (IOP_SRET) resumes it exactly
	// where it left off. Takes ownership of script.
	void callScript(Format::Script::Script *script);

	// IOP_SRET (VileVN reference: IkuraDecoder::iop_sret / IParser::
	// ReturnScript). Pops back to whichever script called this one via
	// callScript(). Returns false if there was nothing to pop to (this
	// wasn't inside a callScript()) - the running script is left as-is
	// either way, so the caller decides what "nothing to return to"
	// means (the reference just lets script() end normally there).
	bool returnScript();

	// IOP_CNS (VileVN reference: IkuraDecoder::iop_cns, s_names.
	// SetString). A small index->name table, read back by IOP_PM cmd
	// 0x13/0x04 (VileVN reference: s_names.GetString).
	void setCharacterName(int index, const Common::String &name) { _characterNames[index] = name; }
	bool getCharacterName(int index, Common::String &name) const {
		if (!_characterNames.contains(index))
			return false;
		name = _characterNames[index];
		return true;
	}

private:
	Format::Script::Script *_script;
	Common::Array<Format::Script::Script *> _scriptStack; // scripts suspended by callScript(), most recent last
	Common::Array<uint32> _variables;
	Common::Array<bool> _flags;
	Common::Array<bool> _system;
	Runtime::Presentation *_presentation = nullptr;
	Runtime::Audio *_audio = nullptr;
	uint32 _timerStart = 0;
	uint32 _delayValue = 0;
	uint32 _waitDeadline = 0;
	Input *_input = nullptr;
	Common::Archive *_scriptCabinet = nullptr;
	Common::HashMap<int, Common::String> _characterNames;
};

} // End of namespace Ikura::VM

#endif
