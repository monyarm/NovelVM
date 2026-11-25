/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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

#ifndef DIRECTOR_DETECTION_TABLES_H
#define DIRECTOR_DETECTION_TABLES_H

static const PlainGameDescriptor directorGames[] = {
	{ "director", "Macromedia Director Game", kUnknownGame, "" },
	{ "beyondthewall", "Beyond the Wall of Stars", kUnstableGame, "https://vndb.org/v25088" },
	{ "cockpit", "The Cockpit", kUnstableGame, "https://vndb.org/v53573" },
	{ "kyoto", "Cosmology of Kyoto", kUnstableGame, "https://vndb.org/v14314" },
	{ "missihb", "MISS Interactive Hard Boiled", kUnstableGame, "https://vndb.org/v33291" },
	{ "sinkha", "Sinkha: The 3D Multimedia Novel", kUnstableGame, "https://vndb.org/v3836" },
	{ "sinkha1", "Sinkha: Hyleyn", kUnstableGame, "https://vndb.org/v3842" },
	{ "sinkha2", "Sinkha: Atmosphere / Planet of the Clouds", kUnstableGame, "https://vndb.org/v3843" }, // also https://vndb.org/v3844
	{ "trans", "Trans' ～僕とあたしの境界線～", kUnstableGame, "https://vndb.org/v7428" },
	{ "twistynight1", "Twisty Night #1 合わせ月の夜", kUnstableGame, "https://vndb.org/v55406" },
	{ "twistynight2", "Twisty Night #2 フェンネル", kUnstableGame, "https://vndb.org/v55407" },
	{ "twistynight3", "Twisty Night #3 いにしえびと", kUnstableGame, "https://vndb.org/v55408" },
	{ "xmaspresent", "Christmas Present", kUnstableGame, "https://vndb.org/v11397" },
	{ 0, 0 }

};

namespace Director {

#define SUPPORT_STATUS ADGF_UNSTABLE

#define GENGAME1_(t,e,f,m,s,l,p,fl,v)				{ { t, e, AD_ENTRY1s(f, m, s), l, p, ((fl) | SUPPORT_STATUS), GUIO1(GUIO_NOASPECT) }, GID_GENERIC, v }
#define GENGAME1t_(t,e,f,m,s,l,p,fl,v)				{ { t, e, AD_ENTRY1s(f, m, s), l, p, ((fl) | SUPPORT_STATUS | ADGF_TAILMD5), GUIO1(GUIO_NOASPECT) }, GID_GENERIC, v }
#define GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,p,fl,v)	{ { t, e, AD_ENTRY2s(f1, m1, s1, f2, m2, s2), l, p, ((fl) | SUPPORT_STATUS), GUIO1(GUIO_NOASPECT) }, GID_GENERIC, v }
#define GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,l,p,fl,v)	{ { t, e, AD_ENTRY2s(f1, m1, s1, f2, m2, s2), l, p, ((fl) | SUPPORT_STATUS | ADGF_TAILMD5), GUIO1(GUIO_NOASPECT) }, GID_GENERIC, v }

#define MACGAME1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define MACGAME1f(t,e,f,m,s,v,fl)   GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK),v)
#define MACGAME1t(t,e,f,m,s,v)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define MACGAME1tf(t,e,f,m,s,v,fl)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK),v)
#define PIPGAME1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformPippin,(ADGF_MACRESFORK),v)
#define PIPGAME1f(t,e,f,m,s,v,fl)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformPippin,((fl)|ADGF_MACRESFORK),v)
#define WINGAME1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME1f(t,e,f,m,s,v,fl)   GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,((fl)|ADGF_NO_FLAGS),v)
#define WINGAME1t(t,e,f,m,s,v)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME1tf(t,e,f,m,s,v,fl)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,((fl)|ADGF_NO_FLAGS),v)
#define FMTGAME1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformFMTowns,ADGF_NO_FLAGS,v)
#define SHKGAME1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformShockwave,ADGF_NO_FLAGS,v)
#define MACDEMO1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define MACDEMO1t(t,e,f,m,s,v)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define WINDEMO1(t,e,f,m,s,v)	GENGAME1_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,ADGF_DEMO,v)
#define WINDEMO1t(t,e,f,m,s,v)	GENGAME1t_(t,e,f,m,s,Common::EN_ANY,Common::kPlatformWindows,ADGF_DEMO,v)
#define MACGAME1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define MACGAME1f_l(t,e,f,m,s,l,v,fl) GENGAME1_(t,e,f,m,s,l,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK),v)
#define PIPGAME1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformPippin,(ADGF_MACRESFORK),v)
#define WINGAME1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME1f_l(t,e,f,m,s,l,v,fl) GENGAME1_(t,e,f,m,s,l,Common::kPlatformWindows,fl,v)
#define WINGAME1t_l(t,e,f,m,s,l,v) GENGAME1t_(t,e,f,m,s,l,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME1tf_l(t,e,f,m,s,l,v,fl) GENGAME1t_(t,e,f,m,s,l,Common::kPlatformWindows,fl,v)
#define MACGAME1t_l(t,e,f,m,s,l,v) GENGAME1t_(t,e,f,m,s,l,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define FMTGAME1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformFMTowns,ADGF_NO_FLAGS,v)
#define SHKGAME1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformShockwave,ADGF_NO_FLAGS,v)
#define MACDEMO1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformMacintosh,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define MACDEMO1f_l(t,e,f,m,s,l,v,fl) GENGAME1_(t,e,f,m,s,l,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK|ADGF_DEMO),v)
#define PIPDEMO1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformPippin,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define WINDEMO1_l(t,e,f,m,s,l,v) GENGAME1_(t,e,f,m,s,l,Common::kPlatformWindows,ADGF_DEMO,v)
#define WINDEMO1t_l(t,e,f,m,s,l,v) GENGAME1t_(t,e,f,m,s,l,Common::kPlatformWindows,ADGF_DEMO,v)

#define MACGAME2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define MACGAME2f(t,e,f1,m1,s1,f2,m2,s2,v,fl)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK),v)
#define MACGAME2tf(t,e,f1,m1,s1,f2,m2,s2,v,fl)	GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformMacintosh,((fl)|ADGF_MACRESFORK),v)
#define PIPGAME2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformPippin,(ADGF_MACRESFORK),v)
#define WINGAME2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME2f(t,e,f1,m1,s1,f2,m2,s2,v,fl)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,((fl)|ADGF_NO_FLAGS),v)
#define WINGAME2f_l(t,e,f1,m1,s1,f2,m2,s2,l,v,fl) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,((fl)|ADGF_NO_FLAGS),v)
#define WINGAME2t(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define WINGAME2tf(t,e,f1,m1,s1,f2,m2,s2,v,fl)	GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,((fl)|ADGF_NO_FLAGS),v)
#define FMTGAME2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformFMTowns,ADGF_NO_FLAGS,v)
#define MACDEMO2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformMacintosh,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define WINDEMO2(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,ADGF_DEMO,v)
#define WINDEMO2t(t,e,f1,m1,s1,f2,m2,s2,v)	GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,ADGF_DEMO,v)
#define MACDEMO2f(t,e,f1,m1,s1,f2,m2,s2,v,fl) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformMacintosh,((fl)|ADGF_DEMO),v)
#define WINDEMO2f(t,e,f1,m1,s1,f2,m2,s2,v,fl) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,Common::EN_ANY,Common::kPlatformWindows,((fl)|ADGF_DEMO),v)
#define MACGAME2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformMacintosh,(ADGF_MACRESFORK),v)
#define PIPGAME2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformPippin,(ADGF_MACRESFORK),v)
#define WINGAME2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define MACGAME2f_l(t,e,f1,m1,s1,f2,m2,s2,l,v,fl) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformMacintosh, ((fl)|ADGF_MACRESFORK), v)
#define WINGAME2t_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,ADGF_NO_FLAGS,v)
#define FMTGAME2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformFMTowns,ADGF_NO_FLAGS,v)
#define MACDEMO2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformMacintosh,(ADGF_MACRESFORK|ADGF_DEMO),v)
#define WINDEMO2_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,ADGF_DEMO,v)
#define WINDEMO2t_l(t,e,f1,m1,s1,f2,m2,s2,l,v) GENGAME2t_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,ADGF_DEMO,v)
#define WINDEMO2f_l(t,e,f1,m1,s1,f2,m2,s2,l,v,fl) GENGAME2_(t,e,f1,m1,s1,f2,m2,s2,l,Common::kPlatformWindows,((fl)|ADGF_DEMO),v)

static const DirectorGameDescription gameDescriptions[] = {
	MACGAME1("beyondthewall", "",  "Beyond The Wall Of Stars", "rt:449d133e14d82d6fc87d76fd048c588d", 465267, 300),
	MACGAME1("beyondthewall", "",  "Desktop Folder/bstars", "rt:449d133e14d82d6fc87d76fd048c588d", 465267, 300),
	WINGAME2t("beyondthewall", "", "PCMMMFIL/WALL.EXE",	"t:36a8d553a58437752e12b250389a4629", 225253,
								   "PCMMMFIL/TITLE.MMM",	"d:9e77d5f51082677628f62112f79f2eeb", 661220, 300),
	MACGAME1("kyoto", "v1.01", "Cosmology of KYOTO", "f5277c53bacd27936158dd3867e587e2", 392249, 311),
	MACDEMO1("kyoto", "Demo", "COK Overview-E", "f5277c53bacd27936158dd3867e587e2", 2140545, 311),
	MACDEMO1_l("kyoto", "Demo", "DemoStart(J)", "f5277c53bacd27936158dd3867e587e2", 441040, Common::JA_JPN, 311),
	// Demo of "The Cockpit" CD based on the manga by Matsumoto
	// Found on UMEZMA
	MACDEMO1_l("cockpit", "Demo", "MANGAROM/COCKDEMO.APPL",		  "rt:e63337750ae3043299238d5868af8bb6", 502685, Common::JA_JPN, 404),
	WINDEMO1_l("cockpit", "Demo", "MANGAROM/ISOIRE/COCKDEMO.EXE", "t:db1164fdd4d0580d70f427b6df2874fe",  748231, Common::JA_JPN, 404),
	MACGAME1("kyoto", "v2.0", "Cosmology of KYOTO", "8719de9c083aca942fc0e5c0a127b6dc", 484114, 404),
	WINGAME2("kyoto", "", "PLAY_E.EXE",					  "84cc8880f8c46b4da121b4c9f184418a", 691269,
						  "CK_DATA/OPENING/ID_INPUT.DXR", "7c17a91e5b06471c11e3c6977040773a", 550062,  404),
	MACGAME1_l("kyoto", "v2.0", "Cosmology of KYOTO", "8719de9c083aca942fc0e5c0a127b6dc", 484189, Common::JA_JPN, 404),
	WINGAME2_l("kyoto", "", "PLAY.EXE",		"865b1b59e32e51a6670b9105ac8501ec", 741137,
							"OPENING1.DXR", "b96015c8c138c6626b10fffaed28efea", 679328, Common::JA_JPN, 404),
	// Original filenames are 合わせ月の夜 (disc 1), フェンネル (disc 2), いにしえびと (disc 3)
	MACGAME1_l("twistynight1", "", "to HD/xn--b9jvbwfx46n2kf6mx", "r:8ea9e9a699c61edddffd79ddd0c994d7", 290426, Common::JA_JPN, 402),
	MACGAME1_l("twistynight2", "", "to HD#2/xn--hckzdwa4grb",	  "r:8ea9e9a699c61edddffd79ddd0c994d7", 290426, Common::JA_JPN, 402),
	MACGAME1_l("twistynight3", "", "to HD#3/xn--n8ji8bydm4c",	  "r:8ea9e9a699c61edddffd79ddd0c994d7", 290426, Common::JA_JPN, 402),
	MACGAME1_l("xmaspresent", "", "CHRISTMAS PRESENT/Christmas Present", "rt:9576fc48f465de7add09d3e5dffe0287", 308920, Common::JA_JPN, 402),
	MACGAME1_l("xmaspresent", "Music", "xn-- MUSIC -", "r:75e98233db68c83153cb1d6cfd601286", 310000, Common::JA_JPN, 402),
	MACGAME1_l("missihb", "", "MISS", "3f932e8f25860fdae3426c1eb4f31817", 718756, Common::JA_JPN, 501),
	WINGAME1_l("missihb", "", "MISS95.EXE", "38b75ecdedf662326fe4931a68ae60cd", 1410206, Common::JA_JPN, 501),
	// Created by Virtual Views / Marco Patrito
	// Published in the US by Mojave, St George, Utah (1995)
	// Plays in English, Italian, Spanish, French, German
	// French version from covermount of PC Fun Nº38 from PressImage, 10ᵉ Paris
	// Mac filename is ' Sinkha '
	MACGAME1("sinkha", "",	 "xn-- Sinkha -", "cb91232ecece0045461d236d5914c03d", 719033, 501),
	WINGAME2tf("sinkha", "",	 "SINKHA.EXE",	  "t:b7c21569f21e9b7e8e9e996264e41827", 1393825,
							 "SINKHA.DXR",	  "t:206c7f91d9dd1593e90e468c4e80c36d", 745484, 500, GF_TRUECOLOR),
	WINGAME2f_l("sinkha", "", "SINKHA.EXE",	  "t:b7c21569f21e9b7e8e9e996264e41827", 1393825,
							 "SINKHA.DXR",	  "t:206c7f91d9dd1593e90e468c4e80c36d", 709524, Common::FR_FRA, 500, GF_TRUECOLOR),
	MACGAME1("sinkha", "Music and Animation", "Music&Animation", "08d6f6e39fbd8388faf57f602acaebcc", 705445, 500),
	WINGAME2("sinkha", "Music and Animation", "MUSIC.EXE",		 "3460ad87d2ba57104e2810a77b53c220", 1393827,
											  "MUS_ANIM.DXR",    "2e31baec8a4957593db517bbf46c6740", 1141212, 500),
	// Published by Virtual Views, Turin (2003) in English, French, Italian
	// Players were posted online throughout the decade for compatibility with later OS versions
	MACGAME1("sinkha1", "", "HYLEYN",	  "08c9ea94f6469e71ae0c71987bbf6323", 157809, 850),
	WINGAME1("sinkha1", "", "HYLEYN.exe", "c163f36141579ee374f7b4b2bddee95a", 2117678, 850),
	// Players for Classic Mac OS / Win9x, available here:
	// http://www.sinkha.com/dfiles/players/HY_MOSC.sit.bin
	// http://www.sinkha.com/dfiles/players/HY_WIN1.zip
	MACGAME2("sinkha1", "Classic", "START",		 "12f06b96b6e1e721d22a4e60e938effc", 157815,
								   "ME/MAA.cxt", "3f79a60e8f8a884f67855a5c1f0e66b5", 835098, 850),
	WINGAME2("sinkha1", "98 - XP SP1", "START_(Win_98-ME-2000-XP_sp1).exe", "c163f36141579ee374f7b4b2bddee95a", 2117678,
									   "ME/MAA.cxt",						"3f79a60e8f8a884f67855a5c1f0e66b5", 835098, 850),
	// Published in 2007 (see notes for episode 1)
	// Episodes 2 and 3 were sold together on one CD
	// Episode selection is available after starting game
	// Mac OSX version is D10
	MACGAME2("sinkha2", "Classic", "START",		 "r:12f06b96b6e1e721d22a4e60e938effc", 235702,
								   "ME/MAA.cxt", "d:252ded5347d19ec02bbbc2335640b7f6", 745596, 850),
	WINGAME2("sinkha2", "", 	   "START.exe",	 "c163f36141579ee374f7b4b2bddee95a",   2117678,
								   "ME/MAA.cxt", "252ded5347d19ec02bbbc2335640b7f6",   745596, 850),
	// Players for Classic Mac OS / Win9x, available here:
	// http://www.sinkha.com/dfiles/players/ATM_MOSC.sit.bin
	// http://www.sinkha.com/dfiles/players/ATM_WIN1.zip
	MACGAME2("sinkha2", "Classic", "START",		 "12f06b96b6e1e721d22a4e60e938effc", 238342,
								   "ME/MAA.cxt", "26f7dcd7a5690910f2116205f54cb44b", 759494, 850),
	WINGAME2("sinkha2", "98 - XP SP1", "START_(Win_98-ME-2000-XP_sp1).exe", "c163f36141579ee374f7b4b2bddee95a", 2117678,
									   "ME/MAA.cxt",						"26f7dcd7a5690910f2116205f54cb44b", 759494, 850),
	MACGAME1_l("trans", "", "Trans", "r:2e860522900a5342e52e5ce51bcad9dd", 219919, Common::JA_JPN, 800),
	WINGAME1_l("trans", "", "Trans.exe", "t:de336a369a0fa4aab1b3d9dc07b1624d", 2117070, Common::JA_JPN, 850),
	// Player for newer OSes, available here:
	// http://www.sinkha.com/dfiles/players/HY_MOSX1.zip
	MACGAME2("sinkha1", "OS X 10.1-10.4", "START.osx",	"5af79c7672a0db0bc2ddacb9dee4d86c", 3121400,
										  "ME/MAA.cxt", "3f79a60e8f8a884f67855a5c1f0e66b5", 835098, 1000),
	// OSX player on Sinkha Episode 2-3 CD
	// Classic player is D8.5
	MACGAME2("sinkha2", "OS X", "START.osx",  "r:9a8a6c9b5204bd86ef0b26e60dc8e039", 344560,
								"ME/MAA.cxt", "r:5ce71027811056309ef8bde7e9433b93", 37748, 1000),
	// Player for newer OSes, available here:
	// http://www.sinkha.com/dfiles/players/ATM_MOSX1.zip
	MACGAME2("sinkha2", "OS X 10.1-10.4", "START.osx",	"5af79c7672a0db0bc2ddacb9dee4d86c", 3121400,
										  "ME/MAA.cxt", "26f7dcd7a5690910f2116205f54cb44b", 759494, 1000),
	// Players for newer OSes, available here:
	// http://www.sinkha.com/dfiles/players/HY_MOSX2.zip
	// http://www.sinkha.com/dfiles/players/HY_WIN2.zip
	MACGAME2("sinkha1", "OS X 10.4-10.5", "START.app/Contents/MacOS/start", "f74a89f3d1d3d7fe55df0fa3c8c125a3", 68550,
										  "ME/MAA.cxt",						"3f79a60e8f8a884f67855a5c1f0e66b5", 835098, 1100),
	WINGAME2("sinkha1", "XP SP2 - Vista", "START_(Win_XP_sp2-Vista).exe",   "3d6ccc7532d94fa518988e9685e45ba9", 4355622,
										  "ME/MAA.cxt",						"3f79a60e8f8a884f67855a5c1f0e66b5", 835098, 1100),
	// http://www.sinkha.com/dfiles/players/ATM_MOSX2.zip
	// http://www.sinkha.com/dfiles/players/ATM_WIN2.zip
	MACGAME2("sinkha2", "OS X 10.4-10.5", "START.app/Contents/MacOS/start", "f74a89f3d1d3d7fe55df0fa3c8c125a3", 68550,
										  "ME/MAA.cxt",						"26f7dcd7a5690910f2116205f54cb44b", 759494, 1100),
	WINGAME2("sinkha2", "XP SP2 - Vista", "START_(Win_XP_sp2-Vista).exe",   "3d6ccc7532d94fa518988e9685e45ba9", 4355622,
										  "ME/MAA.cxt",						"26f7dcd7a5690910f2116205f54cb44b", 759494, 1100),
	{ AD_TABLE_END_MARKER, GID_GENERIC, 0 }

};

} // End of namespace Director

#endif
