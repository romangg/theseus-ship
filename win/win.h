/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_H
#define KWIN_WIN_H

#include "controlling.h"
#include "deco.h"
#include "input.h"
#include "move.h"
#include "net.h"
#include "types.h"

#include "appmenu.h"
#include "atoms.h"
#include "effects.h"
#include "main.h"
#include "screen.h"
#include "shadow.h"
#include "toplevel.h"
#include "utils.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"

#include <QList>

namespace KWin::win
{

inline bool compositing()
{
    return Workspace::self() && Workspace::self()->compositing();
}

inline Xcb::Property fetch_skip_close_animation(xcb_window_t window)
{
    return Xcb::Property(false, window, atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

template<typename Win>
auto scene_window(Win* win)
{
    auto eff_win = win->effectWindow();
    return eff_win ? eff_win->sceneWindow() : nullptr;
}

/**
 * Returns the pointer to the window's shadow. A shadow is only available if Compositing is enabled
 * and on X11 if the corresponding X window has the shadow property set.
 *
 * @returns The shadow belonging to @param win, @c null if there's no shadow.
 */
template<typename Win>
auto shadow(Win* win)
{
    auto sc_win = scene_window(win);
    return sc_win ? sc_win->shadow() : nullptr;
}

/**
 * Updates the shadow associated with @param win.
 * Call this method when the windowing system notifies a change or compositing is started.
 */
template<typename Win>
auto update_shadow(Win* win)
{
    // Old & new shadow region
    QRect dirty_rect;

    auto const old_visible_rect = win->visibleRect();

    if (auto shdw = shadow(win)) {
        dirty_rect = shdw->shadowRegion().boundingRect();
        if (!shdw->updateShadow()) {
            scene_window(win)->updateShadow(nullptr);
        }
        Q_EMIT win->shadowChanged();
    } else if (win->effectWindow()) {
        Shadow::createShadow(win);
    }

    if (auto shdw = shadow(win)) {
        dirty_rect |= shdw->shadowRegion().boundingRect();
    }

    if (old_visible_rect != win->visibleRect()) {
        Q_EMIT win->paddingChanged(win, old_visible_rect);
    }

    if (dirty_rect.isValid()) {
        dirty_rect.translate(win->pos());
        win->addLayerRepaint(dirty_rect);
    }
}

template<typename Win>
void set_shade(Win* win, bool set)
{
    set ? win->setShade(shade::normal) : win->setShade(shade::none);
}

/**
 * Sets the client's active state to \a act.
 *
 * This function does only change the visual appearance of the client,
 * it does not change the focus setting. Use
 * Workspace::activateClient() or Workspace::requestFocus() instead.
 *
 * If a client receives or looses the focus, it calls setActive() on
 * its own.
 */
template<typename Win>
void set_active(Win* win, bool active)
{
    if (win->control()->active() == active) {
        return;
    }
    win->control()->set_active(active);

    auto const ruledOpacity = active
        ? win->control()->rules().checkOpacityActive(qRound(win->opacity() * 100.0))
        : win->control()->rules().checkOpacityInactive(qRound(win->opacity() * 100.0));
    win->setOpacity(ruledOpacity / 100.0);

    workspace()->setActiveClient(active ? win : nullptr);

    if (!active) {
        win->control()->cancel_auto_raise();
    }

    if (!active && win->shadeMode() == shade::activated) {
        win->setShade(shade::normal);
    }

    StackingUpdatesBlocker blocker(workspace());
    workspace()->updateClientLayer(win); // active windows may get different layer
    auto leads = win->transient()->leads();
    for (auto lead : leads) {
        if (lead->control()->fullscreen()) {
            // Fullscreens go high even if their transient is active.
            workspace()->updateClientLayer(lead);
        }
    }

    win->doSetActive();
    Q_EMIT win->activeChanged();
    win->control()->update_mouse_grab();
}

template<typename Win>
bool is_active_fullscreen(Win const* win)
{
    if (!win->control()->fullscreen()) {
        return false;
    }

    // Instead of activeClient() - avoids flicker.
    auto const ac = workspace()->mostRecentlyActivatedClient();

    // According to NETWM spec implementation notes suggests "focused windows having state
    // _NET_WM_STATE_FULLSCREEN" to be on the highest layer. Also take the screen into account.
    return ac
        && (ac == win || ac->screen() != win->screen() || contains(ac->transient()->leads(), win));
}

template<typename Win>
layer belong_to_layer(Win* win)
{
    // NOTICE while showingDesktop, desktops move to the AboveLayer
    // (interchangeable w/ eg. yakuake etc. which will at first remain visible)
    // and the docks move into the NotificationLayer (which is between Above- and
    // ActiveLayer, so that active fullscreen windows will still cover everything)
    // Since the desktop is also activated, nothing should be in the ActiveLayer, though
    if (win->isInternal()) {
        return win::layer::unmanaged;
    }
    if (win->isLockScreen()) {
        return win::layer::unmanaged;
    }
    if (is_desktop(win)) {
        return workspace()->showingDesktop() ? win::layer::above : win::layer::desktop;
    }
    if (is_splash(win)) {
        return win::layer::normal;
    }
    if (is_dock(win)) {
        if (workspace()->showingDesktop()) {
            return win::layer::notification;
        }
        return win->layer_for_dock();
    }
    if (is_on_screen_display(win)) {
        return win::layer::on_screen_display;
    }
    if (is_notification(win)) {
        return win::layer::notification;
    }
    if (is_critical_notification(win)) {
        return win::layer::critical_notification;
    }
    if (workspace()->showingDesktop() && win->belongsToDesktop()) {
        return win::layer::above;
    }
    if (win->control()->keep_below()) {
        return win::layer::below;
    }
    if (is_active_fullscreen(win)) {
        return win::layer::active;
    }
    if (win->control()->keep_above()) {
        return win::layer::above;
    }
    return win::layer::normal;
}

template<typename Win>
void update_layer(Win* win)
{
    if (win->layer() == belong_to_layer(win)) {
        return;
    }
    StackingUpdatesBlocker blocker(workspace());

    // Invalidate, will be updated when doing restacking.
    invalidate_layer(win);

    for (auto const transient : qAsConst(win->transient()->children())) {
        update_layer(transient);
    }
}

template<typename Win>
void send_to_screen(Win* win, int new_screen)
{
    new_screen = win->control()->rules().checkScreen(new_screen);

    if (win->control()->active()) {
        screens()->setCurrent(new_screen);
        // might impact the layer of a fullscreen window
        for (auto cc : workspace()->allClientList()) {
            if (cc->control()->fullscreen() && cc->screen() == new_screen) {
                update_layer(cc);
            }
        }
    }
    if (win->screen() == new_screen) {
        // Don't use isOnScreen(), that's true even when only partially.
        return;
    }

    geometry_updates_blocker blocker(win);

    // operating on the maximized / quicktiled window would leave the old geom_restore behind,
    // so we clear the state first
    auto max_mode = win->maximizeMode();
    auto qtMode = win->control()->quicktiling();
    if (max_mode != maximize_mode::restore) {
        win::maximize(win, win::maximize_mode::restore);
    }

    if (qtMode != quicktiles::none) {
        set_quicktile_mode(win, quicktiles::none, true);
    }

    auto oldScreenArea = workspace()->clientArea(MaximizeArea, win);
    auto screenArea = workspace()->clientArea(MaximizeArea, new_screen, win->desktop());

    // the window can have its center so that the position correction moves the new center onto
    // the old screen, what will tile it where it is. Ie. the screen is not changed
    // this happens esp. with electric border quicktiling
    if (qtMode != quicktiles::none) {
        keep_in_area(win, oldScreenArea, false);
    }

    auto oldGeom = win->frameGeometry();
    auto newGeom = oldGeom;
    // move the window to have the same relative position to the center of the screen
    // (i.e. one near the middle of the right edge will also end up near the middle of the right
    // edge)
    QPoint center = newGeom.center() - oldScreenArea.center();
    center.setX(center.x() * screenArea.width() / oldScreenArea.width());
    center.setY(center.y() * screenArea.height() / oldScreenArea.height());
    center += screenArea.center();
    newGeom.moveCenter(center);
    win->setFrameGeometry(newGeom);

    // If the window was inside the old screen area, explicitly make sure its inside also the new
    // screen area. Calling checkWorkspacePosition() should ensure that, but when moving to a small
    // screen the window could be big enough to overlap outside of the new screen area, making
    // struts from other screens come into effect, which could alter the resulting geometry.
    if (oldScreenArea.contains(oldGeom)) {
        keep_in_area(win, screenArea, false);
    }

    // align geom_restore - checkWorkspacePosition operates on it
    win->setGeometryRestore(win->frameGeometry());

    check_workspace_position(win, oldGeom);

    // re-align geom_restore to constrained geometry
    win->setGeometryRestore(win->frameGeometry());

    // finally reset special states
    // NOTICE that MaximizeRestore/quicktiles::none checks are required.
    // eg. setting quicktiles::none would break maximization
    if (max_mode != maximize_mode::restore) {
        maximize(win, max_mode);
    }

    if (qtMode != quicktiles::none && qtMode != win->control()->quicktiling()) {
        set_quicktile_mode(win, qtMode, true);
    }

    auto tso = workspace()->ensureStackingOrder(win->transient()->children());
    for (auto const& transient : tso) {
        send_to_screen(transient, new_screen);
    }
}

template<typename Win>
bool is_popup(Win* win)
{
    switch (win->windowType()) {
    case NET::ComboBox:
    case NET::DropdownMenu:
    case NET::PopupMenu:
    case NET::Tooltip:
        return true;
    default:
        return win->is_popup_end();
    }
}

/**
 * Tells if @p win is "special", in contrast normal windows are with a border, can be moved by the
 * user, can be closed, etc.
 */
template<typename Win>
bool is_special_window(Win* win)
{
    return is_desktop(win) || is_dock(win) || is_splash(win) || is_toolbar(win)
        || is_notification(win) || is_critical_notification(win) || is_on_screen_display(win);
}

template<typename Win>
void finish_rules(Win* win)
{
    win->updateWindowRules(Rules::All);
    win->control()->set_rules(WindowRules());
}

/**
 * Looks for another window with same captionNormal and captionSuffix.
 * If no such window exists @c nullptr is returned.
 */
template<typename Win>
Win* find_client_with_same_caption(Win const* win)
{
    auto fetchNameInternalPredicate = [win](Win const* cl) {
        return (!is_special_window(cl) || is_toolbar(cl)) && cl != win
            && cl->captionNormal() == win->captionNormal()
            && cl->captionSuffix() == win->captionSuffix();
    };
    return workspace()->findAbstractClient(fetchNameInternalPredicate);
}

/**
 * @brief Finds the window matching the condition expressed in @p func in @p list.
 *
 * @param list The list to search in.
 * @param func The condition function (compare std::find_if).
 * @return The found window or @c null if there is no matching window.
 */
template<class Win, class W>
Win* find_in_list(std::vector<Win*> const& list, std::function<bool(W const*)> func)
{
    static_assert(std::is_base_of<W, Win>::value, "W must be derived from Win");

    auto const it = std::find_if(list.cbegin(), list.cend(), func);
    if (it == list.cend()) {
        return nullptr;
    }
    return *it;
}

template<typename Win1, typename Win2>
bool belong_to_same_client(Win1 win1,
                           Win2 win2,
                           same_client_check checks = flags<same_client_check>())
{
    return win1->belongsToSameApplication(win2, checks);
}

}

#endif
