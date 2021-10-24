/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "appmenu.h"
#include "deco.h"
#include "placement.h"
#include "screen.h"

#include "decorations/decorationbridge.h"
#include "rules/rule_book.h"

#include <KDecoration2/Decoration>

namespace KWin::win
{

template<typename Win>
void setup_rules(Win* win, bool ignore_temporary)
{
    // TODO(romangg): This disconnects all connections of captionChanged to the window itself.
    //                There is only one so this works fine but it's not robustly specified.
    //                Either reshuffle later or use explicit connection object.
    QObject::disconnect(win, &Win::captionChanged, win, nullptr);
    win->control->set_rules(RuleBook::self()->find(win, ignore_temporary));
    // check only after getting the rules, because there may be a rule forcing window type
    // TODO(romangg): what does this mean?
}

template<typename Win>
void evaluate_rules(Win* win)
{
    setup_rules(win, true);
    win->applyWindowRules();
}

template<typename Win>
void setup_connections(Win* win)
{
    QObject::connect(win, &Win::clientStartUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(win, &Win::clientFinishUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(
        win, &Win::clientStartUserMovedResized, win, &Win::removeCheckScreenConnection);
    QObject::connect(
        win, &Win::clientFinishUserMovedResized, win, &Win::setupCheckScreenConnection);

    QObject::connect(win, &Win::paletteChanged, win, [win] { trigger_decoration_repaint(win); });

    QObject::connect(Decoration::DecorationBridge::self(), &QObject::destroyed, win, [win] {
        win->control->destroy_decoration();
    });

    // Replace on-screen-display on size changes.
    QObject::connect(win,
                     &Win::frame_geometry_changed,
                     win,
                     [win]([[maybe_unused]] Toplevel* toplevel, QRect const& old) {
                         if (!is_on_screen_display(win)) {
                             // Not an on-screen-display.
                             return;
                         }
                         if (win->frameGeometry().isEmpty()) {
                             // No current geometry to set.
                             return;
                         }
                         if (old.size() == win->frameGeometry().size()) {
                             // No change.
                             return;
                         }
                         if (win->isInitialPositionSet()) {
                             // Position (geometry?) already set.
                             return;
                         }
                         geometry_updates_blocker blocker(win);

                         auto const area = workspace()->clientArea(
                             PlacementArea, Screens::self()->current(), win->desktop());

                         win::place(win, area);
                     });

    QObject::connect(app_menu::self(), &app_menu::applicationMenuEnabledChanged, win, [win] {
        Q_EMIT win->hasApplicationMenuChanged(win->control->has_application_menu());
    });
}

}
