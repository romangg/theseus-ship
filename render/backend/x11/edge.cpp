/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "edge.h"
#include "atoms.h"
#include "input/cursor.h"

namespace KWin::render::backend::x11
{

WindowBasedEdge::WindowBasedEdge(ScreenEdges* parent)
    : Edge(parent)
    , m_window(XCB_WINDOW_NONE)
    , m_approachWindow(XCB_WINDOW_NONE)
{
}

WindowBasedEdge::~WindowBasedEdge()
{
}

void WindowBasedEdge::doActivate()
{
    createWindow();
    createApproachWindow();
    doUpdateBlocking();
}

void WindowBasedEdge::doDeactivate()
{
    m_window.reset();
    m_approachWindow.reset();
}

void WindowBasedEdge::createWindow()
{
    if (m_window.isValid()) {
        return;
    }
    const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    const uint32_t values[] = {true,
                               XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                   | XCB_EVENT_MASK_POINTER_MOTION};
    m_window.create(geometry, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
    m_window.map();
    // Set XdndAware on the windows, so that DND enter events are received (#86998)
    xcb_atom_t version = 4; // XDND version
    xcb_change_property(connection(),
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atoms->xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        (unsigned char*)(&version));
}

void WindowBasedEdge::createApproachWindow()
{
    if (!activatesForPointer()) {
        return;
    }
    if (m_approachWindow.isValid()) {
        return;
    }
    if (!approach_geometry.isValid()) {
        return;
    }
    const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    const uint32_t values[] = {true,
                               XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                   | XCB_EVENT_MASK_POINTER_MOTION};
    m_approachWindow.create(approach_geometry, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
    m_approachWindow.map();
}

void WindowBasedEdge::doGeometryUpdate()
{
    m_window.setGeometry(geometry);
    if (m_approachWindow.isValid()) {
        m_approachWindow.setGeometry(approach_geometry);
    }
}

void WindowBasedEdge::doStartApproaching()
{
    if (!activatesForPointer()) {
        return;
    }
    m_approachWindow.unmap();
    auto cursor = input::get_cursor();
#ifndef KWIN_UNIT_TEST
    m_cursorPollingConnection
        = connect(cursor, &input::cursor::pos_changed, this, &WindowBasedEdge::updateApproaching);
#endif
    cursor->start_mouse_polling();
}

void WindowBasedEdge::doStopApproaching()
{
    if (!m_cursorPollingConnection) {
        return;
    }
    disconnect(m_cursorPollingConnection);
    m_cursorPollingConnection = QMetaObject::Connection();
    input::get_cursor()->stop_mouse_polling();
    m_approachWindow.map();
}

void WindowBasedEdge::doUpdateBlocking()
{
    if (reserved_count == 0) {
        return;
    }
    if (is_blocked) {
        m_window.unmap();
        m_approachWindow.unmap();
    } else {
        m_window.map();
        m_approachWindow.map();
    }
}

}
