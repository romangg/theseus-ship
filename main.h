/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

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

#ifndef MAIN_H
#define MAIN_H

#include "base/platform.h"
#include "input/platform.h"

#include <kwinglobals.h>
#include <config-kwin.h>

#include <QApplication>
#include <QProcessEnvironment>

#include <memory>

class QCommandLineParser;

namespace KWin
{

namespace base
{

namespace wayland
{
class server;
}

namespace x11
{
class event_filter_manager;
}

}

namespace desktop
{
class screen_locker_watcher;
}

class KWIN_EXPORT Application : public  QApplication
{
    Q_OBJECT
    Q_PROPERTY(quint32 x11Time READ x11Time CONSTANT)
    Q_PROPERTY(quint32 x11RootWindow READ x11RootWindow CONSTANT)
    Q_PROPERTY(void *x11Connection READ x11Connection NOTIFY x11ConnectionChanged)
public:
    /**
     * @brief This enum provides the various operation modes of KWin depending on the available
     * Windowing Systems at startup. For example whether KWin only talks to X11 or also to a Wayland
     * Compositor.
     *
     */
    enum OperationMode {
        /**
         * @brief KWin uses only X11 for managing windows and compositing
         */
        OperationModeX11,
        /**
         * @brief KWin uses only Wayland
         */
        OperationModeWaylandOnly,
        /**
         * @brief KWin uses Wayland and controls a nested Xwayland server.
         */
        OperationModeXwayland
    };

    ~Application() override;

    virtual base::platform& get_base() = 0;

    /**
     * @brief The operation mode used by KWin.
     *
     * @return OperationMode
     */
    OperationMode operationMode() const;
    void setOperationMode(OperationMode mode);
    bool shouldUseWaylandForCompositing() const;

    void setupEventFilters();
    void setupTranslator();
    void setupCommandLine(QCommandLineParser *parser);
    void processCommandLine(QCommandLineParser *parser);

    xcb_timestamp_t x11Time() const {
        return const_cast<Application*>(this)->get_base().x11_data.time;
    }

    void update_x11_time_from_clock();
    void update_x11_time_from_event(xcb_generic_event_t *event);

    static void setCrashCount(int count);
    static bool wasCrash();
    void resetCrashesCount();

    /**
     * Creates the KAboutData object for the KWin instance and registers it as
     * KAboutData::setApplicationData.
     */
    static void createAboutData();

    /**
     * @returns the X11 root window.
     */
    xcb_window_t x11RootWindow() const {
        return const_cast<Application*>(this)->get_base().x11_data.root_window;
    }

    /**
     * @returns the X11 xcb connection
     */
    xcb_connection_t *x11Connection() const {
        return const_cast<Application*>(this)->get_base().x11_data.connection;
    }

    virtual QProcessEnvironment processStartupEnvironment() const;
    virtual void setProcessStartupEnvironment(QProcessEnvironment const& environment);

    bool isTerminating() const {
        return m_terminating;
    }

    static void setupMalloc();
    static void setupLocalizedString();
    virtual void notifyKSplash() {}

    virtual bool is_screen_locked() const;

    std::unique_ptr<base::x11::event_filter_manager> x11_event_filters;
    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;

Q_SIGNALS:
    void x11ConnectionChanged();
    void x11ConnectionAboutToBeDestroyed();
    void startup_finished();
    void virtualTerminalCreated();

protected:
    Application(OperationMode mode, int &argc, char **argv);

    void prepare_start();

    void setTerminating() {
        m_terminating = true;
    }

protected:
    static int crashes;

private:
    OperationMode m_operationMode;
    bool m_terminating = false;
};

inline static Application *kwinApp()
{
    return static_cast<Application*>(QCoreApplication::instance());
}

}

#endif
