/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "effect/update.h"

#include "base/wayland/server.h"
#include "main.h"
#include "render/window.h"
#include "toplevel.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

namespace KWin::render::wayland
{

effects_handler_impl::effects_handler_impl(render::compositor* compositor, render::scene* scene)
    : render::effects_handler_impl(compositor, scene)
    , blur{*this, *waylandServer()->display}
    , contrast{*this, *waylandServer()->display}
{
    reconfigure();

    auto space = static_cast<win::wayland::space*>(workspace());

    // TODO(romangg): We do this for every window here, even for windows that are not an xdg-shell
    //                type window. Restrict that?
    QObject::connect(space, &win::wayland::space::wayland_window_added, this, [this](auto c) {
        if (c->readyForPainting()) {
            slotXdgShellClientShown(c);
        } else {
            QObject::connect(
                c, &Toplevel::windowShown, this, &effects_handler_impl::slotXdgShellClientShown);
        }
    });

    // TODO(romangg): We do this here too for every window.
    for (auto window : space->m_windows) {
        auto wayland_window = qobject_cast<win::wayland::window*>(window);
        if (!wayland_window) {
            continue;
        }
        if (wayland_window->readyForPainting()) {
            setupAbstractClientConnections(wayland_window);
        } else {
            QObject::connect(wayland_window,
                             &Toplevel::windowShown,
                             this,
                             &effects_handler_impl::slotXdgShellClientShown);
        }
    }
}

bool effects_handler_impl::eventFilter(QObject* watched, QEvent* event)
{
    handle_internal_window_effect_update_event(blur, watched, event);
    handle_internal_window_effect_update_event(contrast, watched, event);
    return false;
}

EffectWindow* effects_handler_impl::find_window_by_surface(Wrapland::Server::Surface* surface) const
{
    if (auto win = static_cast<win::wayland::space*>(workspace())->find_window(surface)) {
        return win->render->effect.get();
    }
    return nullptr;
}

Wrapland::Server::Display* effects_handler_impl::waylandDisplay() const
{
    return waylandServer()->display.get();
}

effect::region_integration& effects_handler_impl::get_blur_integration()
{
    return blur;
}

effect::color_integration& effects_handler_impl::get_contrast_integration()
{
    return contrast;
}

void effects_handler_impl::handle_effect_destroy(Effect& effect)
{
    blur.remove(effect);
    contrast.remove(effect);
}

}
