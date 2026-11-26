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

namespace AI5 {

const PlainGameDescriptor ai5Games[] = {
	{ "isaku", "Isaku Renewal", kUnstableGame, "https://vndb.org/v2340" },
	{ "yuno", "Kono Yo no Hate de Koi o Utau Shoujo YU-NO", kUnstableGame, "https://vndb.org/v1377" },
	{ "doukyuusei", "Doukyuusei", kUnstableGame, "https://vndb.org/v34136" },
	{ "doukyuusei2", "Doukyuusei 2", kUnstableGame, "https://vndb.org/v2337" },
	{ "kakyuusei", "Kakyuusei", kUnstableGame, "https://vndb.org/v2341" },
	{ "shuusaku", "Shuusaku", kUnstableGame, "https://vndb.org/v1130" },
	{ "aishimai", "Ai Shimai: Futari no Kajitsu", kUnstableGame, "https://vndb.org/v1836" },
	{ "shangrlia", "Shangrlia", kUnstableGame, "https://vndb.org/v3182" },
	{ nullptr, nullptr }
};

const AI5GameDescription gameDescriptions[] = {
	{
		{
			"isaku",
			"Renewal",
			AD_ENTRY1s("MES.ARC", "1b7b72e5b65eeee72bd2ae9bb80e5387", 1195868),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_ISAKU
	},

	{
		{
			"doukyuusei",
			"",
			AD_ENTRY1s("MES.ARC", "2df55687e448626f34290d66b831f26b", 2118591),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_DOUKYUUSEI
	},

	{
		{
			"doukyuusei2",
			"",
			AD_ENTRY1s("MES.ARC", "2ea8c108b86c23603eb99d68e77edfa7", 3017674),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_DOUKYUUSEI2
	},

	// Elf Classics (Elf Otona no Kanzume) release
	{
		{
			"yuno",
			"Elf Classics",
			AD_ENTRY1s("MES.ARC", "24b705af92e9952cd28d0e6d82fed470", 4110549),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_YUNO
	},

	// Elf Classics (Elf Otona no Kanzume) release; incomplete upstream
	{
		{
			"shangrlia",
			"Elf Classics",
			AD_ENTRY1s("MES.ARC", "0f456a466e380c6a81b56c556d5036cd", 95632),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_SHANGRLIA
	},

	// DVD-ROM release
	{
		{
			"shuusaku",
			"",
			AD_ENTRY1s("MESSAGE.ARC", "37ae0d69197fef8df4353855dc0e8338", 3124775),
			Common::JA_JPN,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO0()
		},
		GID_SHUUSAKU
	},

	{ AD_TABLE_END_MARKER, GID_ISAKU }
};

} // End of namespace AI5
