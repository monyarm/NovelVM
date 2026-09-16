#include "ikura/ikura.h"

#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "ikura/formats/archive/cabinet.h"
#include "ikura/runtime/audio.h"
#include "ikura/runtime/loader.h"
#include "ikura/runtime/presentation.h"
#include "ikura/script/interpreter.h"

namespace Ikura {

IkuraEngine::IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc)
	: Engine(syst), _gameDescription(gameDesc), _console(nullptr), _rnd("ikura"), _scriptContext(nullptr),
	  _scriptCabinet(nullptr), _graphicsCabinet(nullptr), _presentation(nullptr), _musicCabinet(nullptr),
	  _seCabinet(nullptr), _voiceCabinet(nullptr), _audio(nullptr) {
}

IkuraEngine::~IkuraEngine() {
	delete _scriptContext;
	delete _scriptCabinet;
	delete _presentation;
	delete _graphicsCabinet;
	delete _audio;
	delete _musicCabinet;
	delete _seCabinet;
	delete _voiceCabinet;
	delete _console;
}

namespace {
// Same bare-filename convention as "isf"/"ggd" above; not owned by the
// caller past this call (Cabinet::open always consumes stream, success
// or fail).
Common::Archive *openCabinet(const char *name) {
	Common::File *stream = new Common::File();
	if (!stream->open(name)) {
		delete stream;
		return nullptr;
	}
	return Format::Archive::Cabinet::open(stream);
}
} // namespace

Common::Error IkuraEngine::run() {
	// All 8 ikura games run at 640x480 except Hitomi (800x600); no
	// per-game resolution table yet. RGBA32 matches what every decoder in
	// formats/graphic/ already produces (see graphic.cpp), so the per-
	// frame blit below needs no format conversion.
	Graphics::PixelFormat screenFormat = Graphics::PixelFormat::createFormatRGBA32();
	initGraphics(640, 480, &screenFormat);

	DebugMan.addDebugChannel(kDebugScript, "script", "Ikura/GDL script interpreter");
	DebugMan.addDebugChannel(kDebugGraphics, "graphics", "Ikura/GDL graphics and presentation");
	DebugMan.addDebugChannel(kDebugSound, "sound", "Ikura/GDL audio and voice playback");

	_console = new Console(this);

	// The shared cabinet ships as a bare file named "isf" (no extension);
	// its entry-point member is always "TITLE.ISF" (see loadTitleScript).
	// Kept alive for the whole run - IOP_LS/IOP_LSBS load other named
	// scripts from it later.
	_scriptCabinet = openCabinet("isf");
	if (_scriptCabinet)
		_scriptContext = loadTitleScript(_scriptCabinet);

	// Same convention for the graphics/music/SE cabinets ("ggd"/"wmsc"/
	// "se", no extensions). Presentation/Audio are still wired up even if
	// one is missing - the matching opcodes just no-op then, same as
	// running headless in a test.
	_graphicsCabinet = openCabinet("ggd");
	if (_graphicsCabinet)
		_presentation = new Runtime::Presentation(_graphicsCabinet);
	_musicCabinet = openCabinet("wmsc");
	_seCabinet = openCabinet("se");
	_voiceCabinet = openCabinet("voice");
	_audio = new Runtime::Audio(_mixer, _musicCabinet, _seCabinet, _voiceCabinet);
	if (_scriptContext) {
		_scriptContext->setPresentation(_presentation);
		_scriptContext->setAudio(_audio);
		_scriptContext->setInput(&_input);
	}

	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			_input.handleEvent(event);
		}

		// kWaitingForAdvance (e.g. after IOP_PM prints a line) needs this
		// loop to gate on a real advance signal before calling run() again
		// - calling unconditionally would race straight past the pause.
		// kWaitingForTimer (IOP_GO) is the same gate, OR'd with a
		// wall-clock deadline. kYielded (e.g. IOP_IG polling and finding
		// nothing) needs the opposite: call run() again unconditionally
		// every frame and let the opcode that yielded consume whatever's
		// fresh in _input itself when the script's own poll loop re-enters
		// it. See the StepResult values' comments in script/interpreter.h.
		bool canResume = true;
		if (_pauseState == PauseState::kAdvance) {
			// VileVN reference: EventGameTick's IS_WAITTEXT branch - the
			// *first* advance press while dialogue is still typing
			// completes the reveal instantly instead of resuming the
			// script; only once the line is fully shown does a press
			// actually advance. Matches the real two-press dialogue UX
			// (press 1 completes typing, press 2 continues).
			if (_presentation && !_presentation->isTextFullyRevealed()) {
				if (_input.consumeAdvance())
					_presentation->completeTextReveal();
				canResume = false;
			} else {
				canResume = _input.consumeAdvance();
			}
		} else if (_pauseState == PauseState::kTimer) {
			canResume = g_system->getMillis() >= _scriptContext->waitDeadline() || _input.consumeAdvance();
		}

		// VileVN reference: EventGameTick's IS_WAITTIMER handling calls
		// SkipAnimation() whenever the wait ends, whether that's the
		// deadline passing naturally or a player press skipping it early
		// - same here, before the pause state gets overwritten below.
		if (_presentation && _pauseState == PauseState::kTimer && canResume)
			_presentation->completeFade();

		if (_scriptContext && canResume) {
			byte lastOpcode;
			VM::StepResult result = VM::run(*_scriptContext, lastOpcode);
			if (result == VM::StepResult::kWaitingForAdvance)
				_pauseState = PauseState::kAdvance;
			else if (result == VM::StepResult::kWaitingForTimer)
				_pauseState = PauseState::kTimer;
			else
				_pauseState = PauseState::kNone;

			if (_pauseState == PauseState::kNone && result != VM::StepResult::kYielded) {
				if (result == VM::StepResult::kUnimplementedOpcode)
					debugC(kDebugScript, "stopped on unimplemented opcode 0x%02x", lastOpcode);
				else if (result == VM::StepResult::kMalformedOpcode)
					debugC(kDebugScript, "stopped on malformed opcode 0x%02x", lastOpcode);
				delete _scriptContext;
				_scriptContext = nullptr;
			}
		}

		// Buffer 0 is the visible screen by Ikura/GDL convention (see
		// runtime/presentation.h) - scripts blit whatever they want shown
		// into it via IOP_GP/IOP_GC/IOP_GO. compositeScreen() also drives
		// any in-flight IOP_GO/IOP_GP fade crossfade. Dialogue text draws
		// on top of that, same order as the reference's own widget
		// compositing (Printer::Copy blends its glyph surface over
		// whatever the scene already drew). Reuses the real screen
		// surface ScummVM already manages rather than any new blit path.
		if (_presentation) {
			Graphics::Surface *out = g_system->lockScreen();
			_presentation->compositeScreen(*out, g_system->getMillis());
			_presentation->drawText(*out);
			g_system->unlockScreen();
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Ikura
