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
 */

#ifndef SMT_H
#define SMT_H
 
#include "common/random.h"
#include "engines/engine.h"
#include "gui/debugger.h"
#include "common/scummsys.h"

#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/palette.h"
#include "graphics/surface.h"
// #include "smt/gfx/gfx.h"

struct ADGameDescription;
 
namespace SMT {
 
 
class Console;
 
class SMTEngine : public Engine {
public:
	SMTEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~SMTEngine();
 
	virtual Common::Error run();

	// Detection related functions
	const ADGameDescription *_gameDescription;
	const char *getGameId() const;
	Common::Platform getPlatform() const;
 
private:
	Console *_console;
	// Renderer *_gfx;
	Audio::SoundHandle _shandle;

	// We need random numbers
	Common::RandomSource *_rnd;
};
 
// Example console class
class Console : public GUI::Debugger {
public:
	Console(SMTEngine *vm) {}
	virtual ~Console(void) {}
};
 
} // End of namespace SMT
 
#endif
