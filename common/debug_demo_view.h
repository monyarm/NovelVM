#ifndef COMMON_DEBUG_DEMO_VIEW_H
#define COMMON_DEBUG_DEMO_VIEW_H

#include "common/events.h"
#include "common/random.h"

namespace Common {

/**
 * Phase sequencing and input classification for a three-phase format-support
 * demo view (opening video -> texture+BGM -> rotating 3D model), the pattern
 * used by engines that exist to prove format parsers against real game data
 * rather than to play the game. Owns only the parts that are genuinely
 * phase-independent: which phase is active, what phase comes next, and what
 * a Space/Up/Down keypress means given the current phase. Loading,
 * rendering, and the actual entry lists (models, textures, audio tracks)
 * stay with the caller -- they differ per phase and per engine/format
 * backend, so folding them into this class would be abstraction over
 * unrelated code rather than genuine reuse.
 */
class DebugDemoView {
public:
	enum Phase {
		kPhaseVideo,
		kPhaseTextureSong,
		kPhaseModel,
		kPhaseDone
	};

	enum Action {
		kActionNone,
		kActionQuit,
		kActionSkip,
		kActionCycleModel,
		kActionCycleTextureBgm
	};

	Phase phase() const { return _phase; }
	void advance() { _phase = nextPhase(_phase); }
	bool done() const { return _phase == kPhaseDone; }

	// Classifies one polled event against the current phase. For cycle
	// actions, `dir` is set to -1 (Up) or +1 (Down); the caller applies it
	// via cycleIndex() to whichever entry list(s) are relevant to the
	// active phase. Does not check list sizes -- the caller decides
	// whether cycling is meaningful (e.g. size() > 1) before acting.
	Action classify(const Common::Event &e, int &dir) const;

	// Shared by every list-backed section (models, textures, BGM/audio):
	// pick a random starting index, then Up/Down cycles it with wraparound.
	static int cycleIndex(int idx, int dir, int size) {
		return size > 0 ? (idx + dir + size) % size : 0;
	}
	static int pickRandomIndex(Common::RandomSource &rnd, int size) {
		return size > 0 ? (int)rnd.getRandomNumber(size - 1) : 0;
	}

private:
	static Phase nextPhase(Phase current);

	Phase _phase = kPhaseVideo;
};

} // End of namespace Common

#endif
