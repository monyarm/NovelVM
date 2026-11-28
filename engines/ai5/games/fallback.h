/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 */

#ifndef AI5_GAMES_FALLBACK_H
#define AI5_GAMES_FALLBACK_H

namespace AI5 {

struct Game;

// Generic loader for AI5WIN releases that are not in the detection
// table. Modeled on the most common classic AI5WIN layout; unknown
// engine functions warn and continue, and diagnostics are logged to
// the console and to ai5-fallback.log in the game directory.
void initGameAi5Fallback(Game &g);

} // namespace AI5

#endif // AI5_GAMES_FALLBACK_H
