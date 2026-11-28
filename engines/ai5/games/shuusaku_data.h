/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 * Data tables for the Shuusaku port. Generated from the ai5-sdl2
 * sources; do not edit by hand.
 */

#ifndef AI5_GAMES_SHUUSAKU_DATA_H
#define AI5_GAMES_SHUUSAKU_DATA_H

#include "common/scummsys.h"
#include "ai5/games/shuusaku.h"

namespace AI5 {

// Schedule: away events (per location; t/character/flagNo; {0} terminated)
static const SchedAwayEvent kAwayEventsNagisa[] = {
	{ 100, kCharShiho, 4700 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsKaori[] = {
	{ 58, kCharNagisa, 4701 },
	{ 59, kCharNagisa, 4702 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsShiho[] = {
	{ 4, kCharNagisa, 4776 },
	{ 21, kCharNagisa, 4703 },
	{ 61, kCharNagisa, 4704 },
	{ 65, kCharNagisa, 4705 },
	{ 110, kCharNagisa, 4706 },
	{ 111, kCharNagisa, 4707 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsChiaki[] = {
	{ 6, kCharAsami, 4777 },
	{ 28, kCharAsami, 4708 },
	{ 29, kCharAsami, 4709 },
	{ 69, kCharAsami, 4710 },
	{ 94, kCharAsami, 4711 },
	{ 95, kCharAsami, 4712 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsAsami[] = {
	{ 27, kCharChiaki, 4713 },
	{ 129, kCharMoeko, 4714 },
	{ 130, kCharMoeko, 4715 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsMoeko[] = {
	{ 15, kCharKaori, 4716 },
	{ 16, kCharKaori, 4717 },
	{ 33, kCharKaori, 4718 },
	{ 34, kCharKaori, 4719 },
	{ 93, kCharKaori, 4720 },
	{ 0, 0, 0 }
};

// ERI has no away events
// AYAKA has no away events
static const SchedAwayEvent kAwayEventsToilet[] = {
	{ 4, kCharEri, 4204 },
	{ 7, kCharChiaki, 4207 },
	{ 10, kCharShiho, 4210 },
	{ 11, kCharKaori, 4211 },
	{ 12, kCharNagisa, 4212 },
	{ 15, kCharAsami, 4215 },
	{ 20, kCharNagisa, 4220 },
	{ 21, kCharMoeko, 4221 },
	{ 22, kCharChiaki, 4222 },
	{ 23, kCharShiho, 4223 },
	{ 24, kCharEri, 4224 },
	{ 30, kCharKaori, 4230 },
	{ 35, kCharMoeko, 4235 },
	{ 38, kCharAsami, 4238 },
	{ 39, kCharChiaki, 4239 },
	{ 40, kCharNagisa, 4240 },
	{ 42, kCharKaori, 4242 },
	{ 44, kCharMoeko, 4244 },
	{ 55, kCharShiho, 4255 },
	{ 56, kCharEri, 4256 },
	{ 60, kCharAsami, 4260 },
	{ 62, kCharNagisa, 4262 },
	{ 63, kCharChiaki, 4263 },
	{ 71, kCharAsami, 4271 },
	{ 73, kCharEri, 4273 },
	{ 75, kCharNagisa, 4275 },
	{ 76, kCharMoeko, 4276 },
	{ 82, kCharKaori, 4282 },
	{ 83, kCharChiaki, 4283 },
	{ 91, kCharMoeko, 4291 },
	{ 92, kCharAsami, 4292 },
	{ 97, kCharKaori, 4297 },
	{ 99, kCharShiho, 4299 },
	{ 100, kCharEri, 4300 },
	{ 101, kCharNagisa, 4301 },
	{ 105, kCharAsami, 4305 },
	{ 106, kCharAsami, 4306 },
	{ 107, kCharAsami, 4307 },
	{ 113, kCharKaori, 4313 },
	{ 118, kCharEri, 4318 },
	{ 119, kCharShiho, 4319 },
	{ 120, kCharChiaki, 4320 },
	{ 121, kCharNagisa, 4321 },
	{ 122, kCharMoeko, 4322 },
	{ 125, kCharAsami, 4325 },
	{ 127, kCharKaori, 4327 },
	{ 128, kCharChiaki, 4328 },
	{ 131, kCharAsami, 4331 },
	{ 137, kCharShiho, 4337 },
	{ 138, kCharShiho, 4338 },
	{ 139, kCharAsami, 4339 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsChanging[] = {
	{ 16, kCharNagisa, 4721 },
	{ 16, kCharShiho, 4722 },
	{ 16, kCharEri, 4724 },
	{ 18, kCharAsami, 4726 },
	{ 19, kCharChiaki, 4728 },
	{ 20, kCharShiho, 4727 },
	{ 21, kCharKaori, 4730 },
	{ 26, kCharKaori, 4732 },
	{ 26, kCharMoeko, 4733 },
	{ 32, kCharChiaki, 4382 },
	{ 33, kCharChiaki, 4383 },
	{ 60, kCharEri, 4410 },
	{ 61, kCharEri, 4411 },
	{ 62, kCharEri, 4412 },
	{ 68, kCharNagisa, 4418 },
	{ 69, kCharNagisa, 4419 },
	{ 70, kCharNagisa, 4420 },
	{ 72, kCharShiho, 4422 },
	{ 73, kCharShiho, 4423 },
	{ 74, kCharShiho, 4424 },
	{ 76, kCharAsami, 4426 },
	{ 77, kCharAsami, 4427 },
	{ 78, kCharMoeko, 4428 },
	{ 79, kCharMoeko, 4429 },
	{ 80, kCharMoeko, 4430 },
	{ 81, kCharChiaki, 4431 },
	{ 82, kCharChiaki, 4432 },
	{ 84, kCharMoeko, 4434 },
	{ 88, kCharKaori, 4438 },
	{ 89, kCharKaori, 4439 },
	{ 90, kCharKaori, 4440 },
	{ 111, kCharMoeko, 4461 },
	{ 112, kCharMoeko, 4734 },
	{ 112, kCharEri, 4735 },
	{ 113, kCharNagisa, 4736 },
	{ 113, kCharShiho, 4737 },
	{ 116, kCharShiho, 4741 },
	{ 117, kCharKaori, 4789 },
	{ 118, kCharAsami, 4790 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent kAwayEventsKanrinin[] = {
	{ 1, kCharEri, 4501 },
	{ 2, kCharNagisa, 4502 },
	{ 2, kCharShiho, 4502 },
	{ 4, kCharChiaki, 4504 },
	{ 4, kCharAsami, 4504 },
	{ 6, kCharKaori, 4506 },
	{ 8, kCharNagisa, 4744 },
	{ 8, kCharKaori, 4745 },
	{ 8, kCharShiho, 4746 },
	{ 8, kCharAsami, 4747 },
	{ 9, kCharAsami, 4509 },
	{ 10, kCharNagisa, 4748 },
	{ 10, kCharKaori, 4749 },
	{ 10, kCharShiho, 4750 },
	{ 10, kCharAsami, 4751 },
	{ 11, kCharAsami, 4511 },
	{ 12, kCharKaori, 4775 },
	{ 12, kCharChiaki, 4771 },
	{ 13, kCharNagisa, 4752 },
	{ 13, kCharKaori, 4753 },
	{ 13, kCharChiaki, 4772 },
	{ 14, kCharChiaki, 4773 },
	{ 14, kCharMoeko, 4514 },
	{ 15, kCharChiaki, 4515 },
	{ 16, kCharNagisa, 4754 },
	{ 16, kCharShiho, 4755 },
	{ 17, kCharKaori, 4517 },
	{ 18, kCharKaori, 4778 },
	{ 18, kCharChiaki, 4779 },
	{ 18, kCharAsami, 4780 },
	{ 19, kCharNagisa, 4756 },
	{ 19, kCharMoeko, 4757 },
	{ 20, kCharShiho, 4520 },
	{ 21, kCharKaori, 4521 },
	{ 23, kCharChiaki, 4781 },
	{ 23, kCharAsami, 4782 },
	{ 24, kCharNagisa, 4524 },
	{ 25, kCharNagisa, 4525 },
	{ 26, kCharKaori, 4758 },
	{ 26, kCharEri, 4759 },
	{ 27, kCharKaori, 4527 },
	{ 29, kCharKaori, 4529 },
	{ 30, kCharAsami, 4530 },
	{ 31, kCharAsami, 4531 },
	{ 36, kCharMoeko, 4536 },
	{ 37, kCharMoeko, 4537 },
	{ 38, kCharMoeko, 4538 },
	{ 39, kCharMoeko, 4539 },
	{ 40, kCharMoeko, 4540 },
	{ 41, kCharMoeko, 4541 },
	{ 42, kCharMoeko, 4542 },
	{ 43, kCharMoeko, 4543 },
	{ 61, kCharChiaki, 4561 },
	{ 62, kCharKaori, 4562 },
	{ 63, kCharKaori, 4783 },
	{ 63, kCharShiho, 4784 },
	{ 64, kCharAsami, 4564 },
	{ 65, kCharKaori, 4565 },
	{ 67, kCharAsami, 4567 },
	{ 68, kCharChiaki, 4568 },
	{ 69, kCharShiho, 4569 },
	{ 70, kCharShiho, 4570 },
	{ 71, kCharMoeko, 4571 },
	{ 72, kCharNagisa, 4572 },
	{ 73, kCharAsami, 4573 },
	{ 74, kCharAsami, 4574 },
	{ 76, kCharNagisa, 4576 },
	{ 77, kCharChiaki, 4577 },
	{ 78, kCharShiho, 4578 },
	{ 80, kCharKaori, 4580 },
	{ 81, kCharAsami, 4581 },
	{ 83, kCharMoeko, 4583 },
	{ 84, kCharKaori, 4584 },
	{ 85, kCharKaori, 4585 },
	{ 86, kCharChiaki, 4586 },
	{ 87, kCharMoeko, 4587 },
	{ 88, kCharAsami, 4760 },
	{ 88, kCharMoeko, 4761 },
	{ 89, kCharChiaki, 4589 },
	{ 92, kCharShiho, 4592 },
	{ 93, kCharChiaki, 4593 },
	{ 94, kCharKaori, 4594 },
	{ 96, kCharChiaki, 4762 },
	{ 96, kCharMoeko, 4763 },
	{ 98, kCharShiho, 4598 },
	{ 99, kCharNagisa, 4599 },
	{ 101, kCharKaori, 4601 },
	{ 102, kCharKaori, 4602 },
	{ 104, kCharNagisa, 4764 },
	{ 104, kCharKaori, 4765 },
	{ 104, kCharShiho, 4766 },
	{ 107, kCharNagisa, 4767 },
	{ 107, kCharShiho, 4768 },
	{ 108, kCharNagisa, 4608 },
	{ 109, kCharMoeko, 4609 },
	{ 110, kCharAsami, 4610 },
	{ 111, kCharAsami, 4611 },
	{ 112, kCharKaori, 4785 },
	{ 112, kCharShiho, 4786 },
	{ 113, kCharNagisa, 4787 },
	{ 113, kCharMoeko, 4788 },
	{ 116, kCharNagisa, 4769 },
	{ 116, kCharShiho, 4770 },
	{ 117, kCharChiaki, 4617 },
	{ 124, kCharNagisa, 4624 },
	{ 125, kCharNagisa, 4625 },
	{ 127, kCharMoeko, 4627 },
	{ 0, 0, 0 }
};

static const SchedAwayEvent *const kAwayEvents[kNrLoc] = {
	&kAwayEventsNagisa[0], // NAGISA
	&kAwayEventsKaori[0], // KAORI
	&kAwayEventsShiho[0], // SHIHO
	&kAwayEventsChiaki[0], // CHIAKI
	&kAwayEventsAsami[0], // ASAMI
	&kAwayEventsMoeko[0], // MOEKO
	nullptr, // ERI
	nullptr, // AYAKA
	&kAwayEventsToilet[0], // TOILET
	&kAwayEventsChanging[0], // CHANGING
	&kAwayEventsKanrinin[0], // KANRININ
};

// Schedule: camera events (per location; {0} terminated)
static const SchedCamEvent kCamEventsNagisa[] = {
	{ 3, { { 2800, "ev16", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 11, { { 2801, "ev18", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 15, { { 2802, "ev21", nullptr }, { 2803, "ev22", nullptr }, { 0, nullptr, nullptr } } },
	{ 22, { { 2804, "ev24", "ev24up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 27, { { 2805, "ev25", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 60, { { 2806, "ev27", "ev27up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 73, { { 2807, "ev29", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 112, { { 2808, "ev21", nullptr }, { 2809, "ev22", nullptr }, { 0, nullptr, nullptr } } },
	{ 119, { { 2810, "ev24", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 127, { { 2811, "ev25", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsKaori[] = {
	{ 7, { { 2812, "ev56", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 14, { { 2813, "ev57", "ev57up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 28, { { 2814, "ev60", "ev60up" }, { 2815, "ev60a", "ev60aup" }, { 0, nullptr, nullptr } } },
	{ 64, { { 2816, "ev57", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 92, { { 2817, "ev63", "ev63up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 125, { { 2818, "ev56", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsShiho[] = {
	{ 3, { { 2819, "ev17", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 24, { { 2820, "ev42", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 29, { { 2821, "ev43", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 58, { { 2822, "ev44", "ev44up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 67, { { 2823, "ev45", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 68, { { 2824, "ev45", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 107, { { 2825, "ev47", "ev47up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 120, { { 2826, "ev42", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 130, { { 2827, "ev43", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsChiaki[] = {
	{ 5, { { 2828, "ev112", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 30, { { 2829, "ev116", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 35, { { 2830, "ev118", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 64, { { 2831, "ev119", "ev119up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 79, { { 2832, "ev121", "ev121up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 85, { { 2833, "ev123", "ev123up" }, { 2834, "ev123", "ev123aup" }, { 0, nullptr, nullptr } } },
	{ 124, { { 2835, "ev116", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsAsami[] = {
	{ 5, { { 2836, "ev92", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 33, { { 2837, "ev96", "ev96up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 62, { { 2838, "ev97", "ev97up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 89, { { 2839, "ev100", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 90, { { 2840, "ev100", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 91, { { 2841, "ev100", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 132, { { 2842, "ev96", "ev96up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsMoeko[] = {
	{ 15, { { 2843, "ev74", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 16, { { 2844, "ev74", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 36, { { 2845, "ev77", "ev77up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 45, { { 2846, "ev77", "ev77up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 72, { { 2847, "ev78", "ev78up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 95, { { 2848, "ev83", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 124, { { 2849, "ev77", "ev77up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsEri[] = {
	{ 2, { { 2850, "ev142", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 25, { { 2851, "ev144", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 57, { { 2852, "ev145", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 68, { { 2853, "ev148", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 69, { { 2854, "ev148", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 70, { { 2855, "ev148", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 71, { { 2856, "ev148", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 96, { { 2857, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 97, { { 2858, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 98, { { 2859, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 99, { { 2860, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 101, { { 2861, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 102, { { 2862, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 103, { { 2863, "ev150", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 117, { { 2864, "ev144", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

// AYAKA has no camera events
static const SchedCamEvent kCamEventsToilet[] = {
	{ 10, { { 2865, "ev19", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 21, { { 2866, "ev75", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 30, { { 2867, "ev61", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 40, { { 2868, "ev26", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 71, { { 2869, "ev98", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 73, { { 2870, "ev149", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 76, { { 2871, "ev75", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 83, { { 2872, "ev122", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 99, { { 2873, "ev19", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 121, { { 2874, "ev26", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsChanging[] = {
	{ 16, { { 2875, "ev23", nullptr }, { 2876, "ev39", nullptr }, { 2877, "ev143", nullptr } } },
	{ 18, { { 2878, "ev94", "ev94up" }, { 2879, "ev94", "ev94aup" }, { 0, nullptr, nullptr } } },
	{ 19, { { 2880, "ev115", "ev115up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 20, { { 2881, "ev40", "ev40up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 26, { { 2882, "ev59", "ev59up" }, { 2883, "ev76", "ev76up" }, { 0, nullptr, nullptr } } },
	{ 32, { { 2884, "ev117", "ev117up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 60, { { 2885, "ev147", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 61, { { 2886, "ev147", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 62, { { 2887, "ev147", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 68, { { 2888, "ev28", "ev28up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 72, { { 2889, "ev46", "ev46up" }, { 2892, "ev46", "ev46aup" }, { 0, nullptr, nullptr } } },
	{ 73, { { 2890, "ev46", "ev46up" }, { 2893, "ev46", "ev46aup" }, { 0, nullptr, nullptr } } },
	{ 74, { { 2891, "ev46", "ev46up" }, { 2894, "ev46", "ev46aup" }, { 0, nullptr, nullptr } } },
	{ 76, { { 2895, "ev99", "ev99up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 77, { { 2896, "ev99", "ev99up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 84, { { 2897, "ev80", "ev80up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 112, { { 2898, "ev76", "ev76up" }, { 2899, "ev143", nullptr }, { 0, nullptr, nullptr } } },
	{ 113, { { 2900, "ev30", "ev30up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 116, { { 2901, "ev48", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 117, { { 2902, "ev65", "ev65up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 118, { { 2903, "ev102", "ev102up" }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent kCamEventsKanrinin[] = {
	{ 12, { { 2904, "ev113", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 13, { { 2905, "ev20", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 14, { { 2907, "ev113", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 15, { { 2908, "ev113", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 20, { { 2909, "ev41", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 31, { { 2910, "ev95", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 68, { { 2911, "ev120", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 69, { { 2912, "ev41", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 70, { { 2913, "ev41", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 83, { { 2914, "ev79", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 84, { { 2915, "ev62", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 85, { { 2916, "ev62", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 87, { { 2917, "ev82", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 88, { { 2918, "ev82", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 101, { { 2919, "ev58", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 108, { { 2920, "ev20", nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } },
	{ 0, { { 0, nullptr, nullptr }, { 0, nullptr, nullptr }, { 0, nullptr, nullptr } } }
};

static const SchedCamEvent *const kCamEvents[kNrLoc] = {
	&kCamEventsNagisa[0], // NAGISA
	&kCamEventsKaori[0], // KAORI
	&kCamEventsShiho[0], // SHIHO
	&kCamEventsChiaki[0], // CHIAKI
	&kCamEventsAsami[0], // ASAMI
	&kCamEventsMoeko[0], // MOEKO
	&kCamEventsEri[0], // ERI
	nullptr, // AYAKA
	&kCamEventsToilet[0], // TOILET
	&kCamEventsChanging[0], // CHANGING
	&kCamEventsKanrinin[0], // KANRININ
};

// Scene viewer: unlocked scene lists (per character; {0} terminated)
static const Scene kScenesNagisa[] = {
	{ 2700, 1, "ev33.gpx" },
	{ 2706, 7, "ev33r.gpx" },
	{ 2701, 2, "ev34.gpx" },
	{ 2707, 8, "ev34r.gpx" },
	{ 2702, 3, "ev35.gpx" },
	{ 2708, 9, "ev35r.gpx" },
	{ 2703, 4, "ev36.gpx" },
	{ 2709, 10, "ev36r.gpx" },
	{ 2704, 5, "ev37.gpx" },
	{ 2710, 11, "ev37r.gpx" },
	{ 2705, 6, "ev38.gpx" },
	{ 2711, 12, "ev38r.gpx" },
	{ 1051, 13, "ev163.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesKaori[] = {
	{ 2712, 1, "ev67.gpx" },
	{ 2718, 7, "ev67r.gpx" },
	{ 2713, 2, "ev68.gpx" },
	{ 2719, 8, "ev68r.gpx" },
	{ 2714, 3, "ev69.gpx" },
	{ 2720, 9, "ev69r.gpx" },
	{ 2715, 4, "ev70.gpx" },
	{ 2721, 10, "ev70r.gpx" },
	{ 2716, 5, "ev71.gpx" },
	{ 2722, 11, "ev71r.gpx" },
	{ 2717, 6, "ev72.gpx" },
	{ 2723, 12, "ev72r.gpx" },
	{ 2952, 13, "ev62asp.gpx" },
	{ 566, 14, "ev64.gpx" },
	{ 1052, 15, "ev168.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesShiho[] = {
	{ 2724, 1, "ev50.gpx" },
	{ 2730, 7, "ev50r.gpx" },
	{ 2725, 2, "ev51.gpx" },
	{ 2731, 8, "ev51r.gpx" },
	{ 2726, 3, "ev52.gpx" },
	{ 2732, 9, "ev52r.gpx" },
	{ 2727, 4, "ev53.gpx" },
	{ 2733, 10, "ev53r.gpx" },
	{ 2728, 5, "ev54.gpx" },
	{ 2734, 11, "ev54r.gpx" },
	{ 2729, 6, "ev55.gpx" },
	{ 2735, 12, "ev55r.gpx" },
	{ 1053, 13, "ev165.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesChiaki[] = {
	{ 2736, 1, "ev125.gpx" },
	{ 2742, 7, "ev125r.gpx" },
	{ 2737, 2, "ev126.gpx" },
	{ 2743, 8, "ev126r.gpx" },
	{ 2738, 3, "ev127.gpx" },
	{ 2744, 9, "ev127r.gpx" },
	{ 2739, 4, "ev128.gpx" },
	{ 2745, 10, "ev128r.gpx" },
	{ 2740, 5, "ev129.gpx" },
	{ 2746, 11, "ev129r.gpx" },
	{ 2741, 6, "ev130.gpx" },
	{ 2747, 12, "ev130r.gpx" },
	{ 2797, 13, "ev113asp.gpx" },
	{ 2953, 14, "u14sp.gpx" },
	{ 1054, 15, "ev166.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesAsami[] = {
	{ 2748, 1, "ev105.gpx" },
	{ 2754, 7, "ev105r.gpx" },
	{ 2749, 2, "ev106.gpx" },
	{ 2755, 8, "ev106r.gpx" },
	{ 2750, 3, "ev108.gpx" },
	{ 2756, 9, "ev108r.gpx" },
	{ 2751, 4, "ev109.gpx" },
	{ 2757, 10, "ev109r.gpx" },
	{ 2752, 5, "ev110.gpx" },
	{ 2758, 11, "ev110r.gpx" },
	{ 2753, 6, "ev111.gpx" },
	{ 2759, 12, "ev111r.gpx" },
	{ 708, 13, "ev93a.gpx" },
	{ 771, 14, "ev101.gpx" },
	{ 2954, 15, "ev95asp.gpx" },
	{ 1055, 16, "ev164.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesMoeko[] = {
	{ 2760, 1, "ev86.gpx" },
	{ 2766, 7, "ev86r.gpx" },
	{ 2761, 2, "ev87.gpx" },
	{ 2767, 8, "ev87r.gpx" },
	{ 2762, 3, "ev88.gpx" },
	{ 2768, 9, "ev88r.gpx" },
	{ 2763, 4, "ev89.gpx" },
	{ 2769, 10, "ev89r.gpx" },
	{ 2764, 5, "ev90.gpx" },
	{ 2770, 11, "ev90r.gpx" },
	{ 2765, 6, "ev91.gpx" },
	{ 2771, 12, "ev91r.gpx" },
	{ 2798, 13, "ev81.gpx" },
	{ 600, 14, "ev73.gpx" },
	{ 661, 15, "ev73r.gpx" },
	{ 663, 16, "ev84.gpx" },
	{ 1056, 17, "ev167.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesEri[] = {
	{ 2784, 1, "ev151sp.gpx" },
	{ 2785, 2, "ev152e.gpx" },
	{ 2786, 3, "ev152r.gpx" },
	{ 2787, 4, "ev154.gpx" },
	{ 2788, 5, "ev155.gpx" },
	{ 2789, 6, "ev156.gpx" },
	{ 2790, 7, "ev158.gpx" },
	{ 2791, 8, "ev160.gpx" },
	{ 2792, 9, "ev161.gpx" },
	{ 2793, 10, "ev171.gpx" },
	{ 0, 0, nullptr }
};

static const Scene kScenesAyaka[] = {
	{ 2772, 1, "ev135.gpx" },
	{ 2778, 7, "ev135r.gpx" },
	{ 2773, 2, "ev136.gpx" },
	{ 2779, 8, "ev136r.gpx" },
	{ 2774, 3, "ev137.gpx" },
	{ 2780, 9, "ev137r.gpx" },
	{ 2775, 4, "ev138.gpx" },
	{ 2781, 10, "ev138r.gpx" },
	{ 2776, 5, "ev139.gpx" },
	{ 2782, 11, "ev139r.gpx" },
	{ 2777, 6, "ev140.gpx" },
	{ 2783, 12, "ev140r.gpx" },
	{ 2958, 13, "ev131.gpx" },
	{ 2957, 14, "ev131asp.gpx" },
	{ 2955, 15, "ev131bsp.gpx" },
	{ 2950, 16, "ev141.gpx" },
	{ 2951, 17, "ev141asp.gpx" },
	{ 1057, 18, "ev169.gpx" },
	{ 0, 0, nullptr }
};

static const Scene *const kSceneLists[kNrChar] = {
	&kScenesNagisa[0], // NAGISA
	&kScenesKaori[0], // KAORI
	&kScenesShiho[0], // SHIHO
	&kScenesChiaki[0], // CHIAKI
	&kScenesAsami[0], // ASAMI
	&kScenesMoeko[0], // MOEKO
	&kScenesEri[0], // ERI
	&kScenesAyaka[0], // AYAKA
};

// Name input: kana blocks (5 chars each; U+3007 marks skipped cells)
static const char *const kKana[] = {
	"\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86\xe3\x81\x88\xe3\x81\x8a", // あいうえお
	"\xe3\x81\x8b\xe3\x81\x8d\xe3\x81\x8f\xe3\x81\x91\xe3\x81\x93", // かきくけこ
	"\xe3\x81\x95\xe3\x81\x97\xe3\x81\x99\xe3\x81\x9b\xe3\x81\x9d", // さしすせそ
	"\xe3\x81\x9f\xe3\x81\xa1\xe3\x81\xa4\xe3\x81\xa6\xe3\x81\xa8", // たちつてと
	"\xe3\x81\xaa\xe3\x81\xab\xe3\x81\xac\xe3\x81\xad\xe3\x81\xae", // なにぬねの
	"\xe3\x81\xaf\xe3\x81\xb2\xe3\x81\xb5\xe3\x81\xb8\xe3\x81\xbb", // はひふへほ
	"\xe3\x81\xbe\xe3\x81\xbf\xe3\x82\x80\xe3\x82\x81\xe3\x82\x82", // まみむめも
	"\xe3\x82\x84\xe3\x80\x87\xe3\x82\x86\xe3\x80\x87\xe3\x82\x88", // や〇ゆ〇よ
	"\xe3\x82\x89\xe3\x82\x8a\xe3\x82\x8b\xe3\x82\x8c\xe3\x82\x8d", // らりるれろ
	"\xe3\x82\x8f\xe3\x82\x90\xe3\x82\x91\xe3\x82\x92\xe3\x82\x93", // わゐゑをん
	"\xe3\x81\x8c\xe3\x81\x8e\xe3\x81\x90\xe3\x81\x92\xe3\x81\x94", // がぎぐげご
	"\xe3\x81\x96\xe3\x81\x98\xe3\x81\x9a\xe3\x81\x9c\xe3\x81\x9e", // ざじずぜぞ
	"\xe3\x81\xa0\xe3\x81\xa2\xe3\x81\xa5\xe3\x81\xa7\xe3\x81\xa9", // だぢづでど
	"\xe3\x81\xb0\xe3\x81\xb3\xe3\x81\xb6\xe3\x81\xb9\xe3\x81\xbc", // ばびぶべぼ
	"\xe3\x81\xb1\xe3\x81\xb4\xe3\x81\xb7\xe3\x81\xba\xe3\x81\xbd", // ぱぴぷぺぽ
	"\xe3\x81\x81\xe3\x81\x83\xe3\x81\x85\xe3\x81\x87\xe3\x81\x89", // ぁぃぅぇぉ
	"\xe3\x81\xa3\xe3\x82\x83\xe3\x82\x85\xe3\x82\x87\xe3\x82\x8e", // っゃゅょゎ
	"\xe3\x80\x87\xe3\x80\x87\xe3\x80\x87\xe3\x80\x87\xe3\x80\x87", // 〇〇〇〇〇
	"\xe3\x82\xa2\xe3\x82\xa4\xe3\x82\xa6\xe3\x82\xa8\xe3\x82\xaa", // アイウエオ
	"\xe3\x82\xab\xe3\x82\xad\xe3\x82\xaf\xe3\x82\xb1\xe3\x82\xb3", // カキクケコ
	"\xe3\x82\xb5\xe3\x82\xb7\xe3\x82\xb9\xe3\x82\xbb\xe3\x82\xbd", // サシスセソ
	"\xe3\x82\xbf\xe3\x83\x81\xe3\x83\x84\xe3\x83\x86\xe3\x83\x88", // タチツテト
	"\xe3\x83\x8a\xe3\x83\x8b\xe3\x83\x8c\xe3\x83\x8d\xe3\x83\x8e", // ナニヌネノ
	"\xe3\x83\x8f\xe3\x83\x92\xe3\x83\x95\xe3\x83\x98\xe3\x83\x9b", // ハヒフヘホ
	"\xe3\x83\x9e\xe3\x83\x9f\xe3\x83\xa0\xe3\x83\xa1\xe3\x83\xa2", // マミムメモ
	"\xe3\x83\xa4\xe3\x80\x87\xe3\x83\xa6\xe3\x80\x87\xe3\x83\xa8", // ヤ〇ユ〇ヨ
	"\xe3\x83\xa9\xe3\x83\xaa\xe3\x83\xab\xe3\x83\xac\xe3\x83\xad", // ラリルレロ
	"\xe3\x83\xaf\xe3\x83\xb0\xe3\x83\xb1\xe3\x83\xb2\xe3\x83\xb3", // ワヰヱヲン
	"\xe3\x82\xac\xe3\x82\xae\xe3\x82\xb0\xe3\x82\xb2\xe3\x82\xb4", // ガギグゲゴ
	"\xe3\x82\xb6\xe3\x82\xb8\xe3\x82\xba\xe3\x82\xbc\xe3\x82\xbe", // ザジズゼゾ
	"\xe3\x83\x80\xe3\x83\x82\xe3\x83\x85\xe3\x83\x87\xe3\x83\x89", // ダヂヅデド
	"\xe3\x83\x90\xe3\x83\x93\xe3\x83\x96\xe3\x83\x99\xe3\x83\x9c", // バビブベボ
	"\xe3\x83\x91\xe3\x83\x94\xe3\x83\x97\xe3\x83\x9a\xe3\x83\x9d", // パピプペポ
	"\xe3\x82\xa1\xe3\x82\xa3\xe3\x82\xa5\xe3\x82\xa7\xe3\x82\xa9", // ァィゥェォ
	"\xe3\x83\x83\xe3\x83\xa3\xe3\x83\xa5\xe3\x83\xa7\xe3\x83\xae", // ッャュョヮ
	"\xe3\x83\xb4\xe3\x83\xb5\xe3\x83\xb6\xe3\x80\x87\xe3\x80\x87", // ヴヵヶ〇〇
};

// Name input: symbol blocks (U+3000 marks skipped cells)
static const char *const kKigou[] = {
	"\xef\xbc\xa1\xef\xbc\xa2\xef\xbc\xa3\xef\xbc\xa4\xef\xbc\xa5\xef\xbc\xa6\xef\xbc\xa7\xef\xbc\xa8\xef\xbc\xa9\xef\xbc\xaa\xef\xbc\xab\xef\xbc\xac\xef\xbc\xad\xef\xbc\xae\xef\xbc\xaf\xef\xbc\xb0\xef\xbc\xb1\xef\xbc\xb2", // ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲ
	"\xef\xbc\xb3\xef\xbc\xb4\xef\xbc\xb5\xef\xbc\xb6\xef\xbc\xb7\xef\xbc\xb8\xef\xbc\xb9\xef\xbc\xba", // ＳＴＵＶＷＸＹＺ
	"\xef\xbd\x81\xef\xbd\x82\xef\xbd\x83\xef\xbd\x84\xef\xbd\x85\xef\xbd\x86\xef\xbd\x87\xef\xbd\x88\xef\xbd\x89\xef\xbd\x8a\xef\xbd\x8b\xef\xbd\x8c\xef\xbd\x8d\xef\xbd\x8e\xef\xbd\x8f\xef\xbd\x90\xef\xbd\x91\xef\xbd\x92", // ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒ
	"\xef\xbd\x93\xef\xbd\x94\xef\xbd\x95\xef\xbd\x96\xef\xbd\x97\xef\xbd\x98\xef\xbd\x99\xef\xbd\x9a", // ｓｔｕｖｗｘｙｚ
	"\xef\xbc\x90\xef\xbc\x91\xef\xbc\x92\xef\xbc\x93\xef\xbc\x94\xef\xbc\x95\xef\xbc\x96\xef\xbc\x97\xef\xbc\x98\xef\xbc\x99", // ０１２３４５６７８９
	"\xe3\x83\xbb\xef\xbf\xa5\xe3\x80\x80\xe2\x88\x92\xef\xbc\x9a\xef\xbc\x9b", // ・￥　−：；
};

// Name input: kanji blocks (kana label + kanji candidates)
struct ShuusakuKanjiBlock { const char *kana; const char *kanji; };
static const ShuusakuKanjiBlock kKanji[] = {
	{ "\xe3\x81\xac", "\xe6\xbf\xa1" }, // ぬ
	{ "\xe3\x81\xad", "\xe7\xa6\xb0\xe7\xa5\xa2\xe5\xaf\xa7\xe8\x91\xb1\xe7\x8c\xab\xe7\x86\xb1\xe5\xb9\xb4\xe5\xbf\xb5\xe6\x8d\xbb\xe6\x92\x9a\xe7\x87\x83\xe7\xb2\x98" }, // ね
	{ "\xe3\x81\xae", "\xe4\xb9\x83\xe5\xbb\xbc\xe4\xb9\x8b\xe5\x9f\x9c\xe5\x9a\xa2\xe6\x82\xa9\xe6\xbf\x83\xe7\xb4\x8d\xe8\x83\xbd\xe8\x84\xb3\xe8\x86\xbf\xe8\xbe\xb2\xe8\xa6\x97\xe8\x9a\xa4" }, // の
	{ "\xe3\x81\xbf", "\xe5\x91\xb3\xe6\x9c\xaa\xe9\xad\x85\xe5\xb7\xb3\xe7\xae\x95\xe5\xb2\xac\xe5\xaf\x86\xe8\x9c\x9c\xe6\xb9\x8a\xe8\x93\x91\xe7\xa8\x94\xe8\x84\x88\xe5\xa6\x99\xe7\xb2\x8d\xe6\xb0\x91\xe7\x9c\xa0" }, // み
	{ "\xe3\x82\x80", "\xe5\x8b\x99\xe5\xa4\xa2\xe7\x84\xa1\xe7\x89\x9f\xe7\x9f\x9b\xe9\x9c\xa7\xe9\xb5\xa1\xe6\xa4\x8b\xe5\xa9\xbf\xe5\xa8\x98" }, // む
	{ "\xe3\x82\x81", "\xe5\x86\xa5\xe5\x90\x8d\xe5\x91\xbd\xe6\x98\x8e\xe7\x9b\x9f\xe8\xbf\xb7\xe9\x8a\x98\xe9\xb3\xb4\xe5\xa7\xaa\xe7\x89\x9d\xe6\xbb\x85\xe5\x85\x8d\xe6\xa3\x89\xe7\xb6\xbf\xe7\xb7\xac\xe9\x9d\xa2\xe9\xba\xba" }, // め
	{ "\xe3\x82\x84", "\xe4\xb9\x9f\xe5\x86\xb6\xe5\xa4\x9c\xe7\x88\xba\xe8\x80\xb6\xe9\x87\x8e\xe5\xbc\xa5\xe7\x9f\xa2\xe5\x8e\x84\xe5\xbd\xb9\xe7\xb4\x84\xe8\x96\xac\xe8\xa8\xb3\xe8\xba\x8d\xe9\x9d\x96\xe6\x9f\xb3\xe8\x96\xae\xe9\x91\x93" }, // や
	{ "\xe3\x82\x8b", "\xe7\x91\xa0\xe5\xa1\x81\xe6\xb6\x99\xe7\xb4\xaf\xe9\xa1\x9e" }, // る
};

} // namespace AI5

#endif
