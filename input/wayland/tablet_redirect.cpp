/*
    SPDX-FileCopyrightText: 2019 Aleix Pol Gonzalez <aleixpol@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_redirect.h"

#include "input/event.h"
#include "input/event_filter.h"
#include "input/event_spy.h"

#include "wayland_server.h"
#include "workspace.h"

#include <cassert>

namespace KWin::input::wayland
{

void tablet_redirect::init()
{
    assert(!inited());
    setInited(true);
    device_redirect::init();

    QObject::connect(workspace(), &QObject::destroyed, this, [this] { setInited(false); });
    QObject::connect(waylandServer(), &QObject::destroyed, this, [this] { setInited(false); });
}

QPointF tablet_redirect::position() const
{
    return last_position;
}

bool tablet_redirect::positionValid() const
{
    return !last_position.isNull();
}

void tablet_redirect::tabletToolEvent(redirect::TabletEventType type,
                                      QPointF const& pos,
                                      qreal pressure,
                                      int x_tilt,
                                      int y_tilt,
                                      qreal rotation,
                                      bool tip_down,
                                      bool tip_near,
                                      quint64 serial_id,
                                      quint64 /*toolId*/,
                                      void* /*device*/)
{
    if (!inited()) {
        return;
    }
    last_position = pos;

    QEvent::Type t;
    switch (type) {
    case redirect::Axis:
        t = QEvent::TabletMove;
        break;
    case redirect::Tip:
        t = tip_down ? QEvent::TabletPress : QEvent::TabletRelease;
        break;
    case redirect::Proximity:
        t = tip_near ? QEvent::TabletEnterProximity : QEvent::TabletLeaveProximity;
        break;
    }

    auto const button = tip.down ? Qt::LeftButton : Qt::NoButton;
    QTabletEvent ev(t,
                    pos,
                    pos,
                    QTabletEvent::Stylus,
                    QTabletEvent::Pen,
                    pressure,
                    x_tilt,
                    y_tilt,
                    0, // tangentialPressure
                    rotation,
                    0, // z
                    Qt::NoModifier,
                    serial_id,
                    button,
                    button);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::tabletToolEvent, std::placeholders::_1, &ev));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::tabletToolEvent, std::placeholders::_1, &ev));

    tip.down = tip_down;
    tip.near = tip_near;
}

void tablet_redirect::tabletToolButtonEvent(uint button, bool isPressed)
{
    if (isPressed) {
        pressed_buttons.tool.insert(button);
    } else {
        pressed_buttons.tool.remove(button);
    }

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::tabletToolButtonEvent, std::placeholders::_1, pressed_buttons.tool));
    kwinApp()->input->redirect->processFilters(std::bind(
        &input::event_filter::tabletToolButtonEvent, std::placeholders::_1, pressed_buttons.tool));
}

void tablet_redirect::tabletPadButtonEvent(uint button, bool isPressed)
{
    if (isPressed) {
        pressed_buttons.pad.insert(button);
    } else {
        pressed_buttons.pad.remove(button);
    }

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::tabletPadButtonEvent, std::placeholders::_1, pressed_buttons.pad));
    kwinApp()->input->redirect->processFilters(std::bind(
        &input::event_filter::tabletPadButtonEvent, std::placeholders::_1, pressed_buttons.pad));
}

void tablet_redirect::tabletPadStripEvent(int number, int position, bool is_finger)
{
    kwinApp()->input->redirect->processSpies(std::bind(
        &event_spy::tabletPadStripEvent, std::placeholders::_1, number, position, is_finger));
    kwinApp()->input->redirect->processFilters(std::bind(&input::event_filter::tabletPadStripEvent,
                                                         std::placeholders::_1,
                                                         number,
                                                         position,
                                                         is_finger));
}

void tablet_redirect::tabletPadRingEvent(int number, int position, bool is_finger)
{
    kwinApp()->input->redirect->processSpies(std::bind(
        &event_spy::tabletPadRingEvent, std::placeholders::_1, number, position, is_finger));
    kwinApp()->input->redirect->processFilters(std::bind(&input::event_filter::tabletPadRingEvent,
                                                         std::placeholders::_1,
                                                         number,
                                                         position,
                                                         is_finger));
}

void tablet_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* /*old*/,
                                        Decoration::DecoratedClientImpl* /*now*/)
{
}

void tablet_redirect::cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
{
}

void tablet_redirect::focusUpdate(KWin::Toplevel* /*old*/, KWin::Toplevel* /*now*/)
{
}

}
