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

#ifndef GRAPHICS_AFPACK_TRAIL_H
#define GRAPHICS_AFPACK_TRAIL_H

#include "common/array.h"
#include "graphics/epl_effect.h"

namespace Graphics {

/** EffectTextureRef::globalSlot of the weapon-trail texture, effect/z0.tmx (after the 22 effect textures). */
enum { kTrailTextureSlot = 22 };

/**
 * A Persona 3 FES / Persona 4 "AfPack" (RMD chunk 0xF0F000E1): weapon-trail ribbons swept
 * between pairs of helper points, each section driven by its own command stream. One instance
 * is played from the start each time its animation slot starts; it is finished (and should be
 * dropped) once every section's stream has ended.
 */
class AfPackTrail {
public:
	/** World (here: model-space) position of a helper; false if the model has no such helper. */
	typedef bool (*HelperLookup)(void *context, uint32 helper, float position[3]);

	AfPackTrail(const byte *data, uint32 size);

	bool ok() const { return _ok; }
	/** Every section has reached the end of its stream: nothing more to update or draw. */
	bool finished() const;
	/** Start over: reset every section and run each stream once (run 0, which never samples). */
	void restart();
	/** One game tick: run the commands, sample the helpers, advance the fade clock. */
	void update(HelperLookup lookup, void *context);
	/** Additive ribbons in the helpers' space, vertex colours times `modelColor`. */
	void draw(uint32 modelColor, Common::Array<EffectBatch> &out) const;

private:
	struct Param {
		uint16 id = 0;
		float midAxis = 0, midNewer = 0, midOlder = 0, headAxis = 0, headOlder = 0, tailAxis = 0, tailNewer = 0;
		float kLen[2] = {0, 0}, kTurn[2] = {0, 0};
		float lengthScale = 1;
	};
	struct Material {
		uint16 id = 0;
		bool textured = false;
		byte colors[4][4] = {}; ///< c0, c1 (edge A newest, oldest), c2, c3 (edge B), RGBA
	};
	struct Sample {
		float edge[2][3] = {};       ///< A, B
		float leave[2][3] = {};      ///< tangent leaving this sample toward the older one, per edge
		float arrive[2][3] = {};     ///< tangent arriving at the next older sample, per edge
	};
	struct Section {
		uint32 param = 0, material = 0;
		int32 start = 0, end = 0;
		uint32 capacity = 0, rows = 0, mode = 0;
		Common::Array<uint16> commands;
		// interpreter
		uint32 cursor = 0;
		bool waiting = false, recording = false;
		uint32 elapsed = 0;
		int16 delay = 0;
		uint32 fadeFrames = 0;
		bool fading = false;
		float fadeStart = 0, fadeDuration = 0, clock = 0;
		// history ring, logical index 0 = newest
		Common::Array<Sample> ring;
		uint32 writeIndex = 0, count = 0;
	};

	bool _ok = false;
	Common::Array<Param> _params;
	Common::Array<Material> _materials;
	Common::Array<Section> _sections;

	bool parse(const byte *data, uint32 size);
	static void run(Section &section);
	static void push(Section &section, const Param &param, const float a[3], const float b[3]);
	static void computeTangents(Section &section, const Param &param, uint32 segment);
	static void point(const Section &section, int edge, float d, float lengthScale, float out[3]);
	static float fade(const Section &section);
	void drawSection(const Section &section, uint32 modelColor, Common::Array<EffectBatch> &out) const;
};

} // End of namespace Graphics

#endif
