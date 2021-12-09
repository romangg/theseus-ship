/*
 * Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "clockskewnotifierengine_p.h"
#if defined(Q_OS_LINUX)
#include "clockskewnotifierengine_linux.h"
#endif

namespace KWin
{

ClockSkewNotifierEngine* ClockSkewNotifierEngine::create(QObject* parent)
{
#if defined(Q_OS_LINUX)
    return LinuxClockSkewNotifierEngine::create(parent);
#else
    return nullptr;
#endif
}

ClockSkewNotifierEngine::ClockSkewNotifierEngine(QObject* parent)
    : QObject(parent)
{
}

} // namespace KWin
