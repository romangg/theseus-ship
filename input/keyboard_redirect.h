/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xkb.h"

#include <QObject>

namespace KWin::input
{

class keyboard;
class keyboard_layout_spy;
class modifiers_changed_spy;
class redirect;

class KWIN_EXPORT keyboard_redirect : public QObject
{
    Q_OBJECT
public:
    explicit keyboard_redirect(input::redirect* parent);
    ~keyboard_redirect() override;

    void init();

    input::xkb* xkb() const;
    Qt::KeyboardModifiers modifiers() const;
    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const;

    void update();

    void processKey(uint32_t key,
                    redirect::KeyboardKeyState state,
                    uint32_t time,
                    input::keyboard* device = nullptr);
    void processModifiers(uint32_t modsDepressed,
                          uint32_t modsLatched,
                          uint32_t modsLocked,
                          uint32_t group);
    void processKeymapChange(int fd, uint32_t size);

Q_SIGNALS:
    void ledsChanged(input::xkb::LEDs);

private:
    input::redirect* redirect;
    bool m_inited = false;
    QScopedPointer<input::xkb> m_xkb;
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy* modifiers_spy = nullptr;
    keyboard_layout_spy* m_keyboardLayout = nullptr;
};

}
