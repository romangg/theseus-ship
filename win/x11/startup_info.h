/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "main.h"

#include <KStartupInfo>

namespace KWin::win::x11
{

template<typename Win>
void startup_id_changed(Win* win)
{
    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    bool asn_valid = win->space.checkStartupNotification(win->xcb_window, asn_id, asn_data);
    if (!asn_valid)
        return;
    // If the ASN contains desktop, move it to the desktop, otherwise move it to the current
    // desktop (since the new ASN should make the window act like if it's a new application
    // launched). However don't affect the window's desktop if it's set to be on all desktops.
    int desktop = win->space.virtual_desktop_manager->current();
    if (asn_data.desktop() != 0)
        desktop = asn_data.desktop();
    if (!win->isOnAllDesktops()) {
        win->space.sendClientToDesktop(win, desktop, true);
    }
    if (asn_data.xinerama() != -1) {
        auto output = base::get_output(kwinApp()->get_base().get_outputs(), asn_data.xinerama());
        if (output) {
            send_to_screen(win->space, win, *output);
        }
    }
    auto const timestamp = asn_id.timestamp();
    if (timestamp != 0) {
        auto activate = win->space.allowClientActivation(win, timestamp);
        if (asn_data.desktop() != 0 && !win->isOnCurrentDesktop()) {
            // it was started on different desktop than current one
            activate = false;
        }
        if (activate) {
            win->space.activateClient(win);
        } else {
            set_demands_attention(win, true);
        }
    }
}

}
