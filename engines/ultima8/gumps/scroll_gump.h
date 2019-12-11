/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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
 */

#ifndef ULTIMA8_GUMPS_SCROLLGUMP_H
#define ULTIMA8_GUMPS_SCROLLGUMP_H

#include "ultima8/gumps/modal_gump.h"
#include "ultima8/usecode/intrinsics.h"
#include "ultima8/misc/p_dynamic_cast.h"

namespace Ultima8 {

class ScrollGump : public ModalGump {
	std::string text;
	ObjId textwidget;
public:
	ENABLE_RUNTIME_CLASSTYPE()

	ScrollGump();
	ScrollGump(ObjId owner, std::string msg);
	virtual ~ScrollGump();

	// Go to the next page on mouse click
	virtual void OnMouseClick(int button, int mx, int my);

	// Close on double click
	virtual void OnMouseDouble(int button, int mx, int my);

	// Init the gump, call after construction
	virtual void InitGump(Gump *newparent, bool take_focus = true);

	INTRINSIC(I_readScroll);

protected:
	void NextText();

public:
	bool loadData(IDataSource *ids, uint32 version);
protected:
	virtual void saveData(ODataSource *ods);
};

} // End of namespace Ultima8

#endif