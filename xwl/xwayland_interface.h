/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "kwinglobals.h"

#include <QObject>
#include <QPoint>

namespace KWin
{
class Toplevel;

namespace xwl
{
enum class drag_event_reply {
    // event should be ignored by the filter
    ignore,
    // event is filtered out
    take,
    // event should be handled as a Wayland native one
    wayland,
};

class KWIN_EXPORT xwayland_interface : public QObject
{
    Q_OBJECT

public:
    static xwayland_interface* self();

    virtual drag_event_reply drag_move_filter(Toplevel* target, QPoint const& pos) = 0;

protected:
    xwayland_interface();
    ~xwayland_interface() override;

private:
    Q_DISABLE_COPY(xwayland_interface)
};

}

inline xwl::xwayland_interface* xwayland()
{
    return xwl::xwayland_interface::self();
}

}
