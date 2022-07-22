/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "toplevel.h"

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, base::output const* output)
{
    if (!output) {
        return false;
    }
    return output->geometry().intersects(win->frameGeometry());
}

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop.
 *
 * In case that separate output focus is used only windows on the current output are considered.
 * If no window for activation is found @c null is returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Space>
base::output const* get_current_output(Space const& space)
{
    auto const& base = kwinApp()->get_base();

    if (kwinApp()->options->get_current_output_follows_mouse()) {
        return base::get_nearest_output(base.get_outputs(), input::get_cursor()->pos());
    }

    auto const cur = base.topology.current;
    if (auto client = space.active_client; client && !win::on_screen(client, cur)) {
        return client->central_output;
    }
    return cur;
}

template<typename Base, typename Win>
void set_current_output_by_window(Base& base, Win const& window)
{
    if (!window.control->active()) {
        return;
    }
    if (window.central_output && !win::on_screen(&window, base.topology.current)) {
        base::set_current_output(base, window.central_output);
    }
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, get_current_output(win->space));
}

}
