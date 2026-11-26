/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on VileVN, Copyright (C) ViLE Team (GPL-3.0).
 *
 */

#ifndef IKURA_H
#define IKURA_H

#include "common/error.h"
#include "common/random.h"
#include "common/scummsys.h"
#include "engines/engine.h"
#include "gui/debugger.h"
#include "ikura/runtime/input.h"

struct ADGameDescription;

namespace Common {
class Archive;
}

namespace Ikura {

namespace VM {
class Context;
}

namespace Runtime {
class Presentation;
class Audio;
}

/**
 * Debug channels registered with DebugMan in IkuraEngine::run().
 */
enum IkuraDebugChannels {
	kDebugScript = 1 << 0,
	kDebugGraphics = 1 << 1,
	kDebugSound = 1 << 2
};

class Console;

class IkuraEngine : public Engine {
public:
	IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~IkuraEngine() override;

	Common::Error run() override;

	const char *getGameId() const;
	Common::Platform getPlatform() const;

private:
	const ADGameDescription *_gameDescription;
	Console *_console;
	Common::RandomSource _rnd;
	Input _input;
	VM::Context *_scriptContext;
	Common::Archive *_scriptCabinet;
	Common::Archive *_graphicsCabinet;
	Runtime::Presentation *_presentation;
	Common::Archive *_musicCabinet;
	Common::Archive *_seCabinet;
	Common::Archive *_voiceCabinet;
	Runtime::Audio *_audio;

	// Which of StepResult's pause kinds (if any) the interpreter is
	// currently sitting in - see script/interpreter.h's StepResult
	// comments for what each caller-side resume policy needs to check.
	enum class PauseState { kNone, kAdvance, kTimer };
	PauseState _pauseState = PauseState::kNone;
};

class Console : public GUI::Debugger {
public:
	Console(IkuraEngine *vm) {}
	~Console() override {}
};

} // End of namespace Ikura

#endif
