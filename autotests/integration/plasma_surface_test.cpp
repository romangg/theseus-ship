/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/control.h"
#include "win/move.h"
#include "win/net.h"
#include "win/space.h"
#include "win/stacking_order.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

Q_DECLARE_METATYPE(KWin::win::layer)

namespace KWin
{

class PlasmaSurfaceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testRoleOnAllDesktops_data();
    void testRoleOnAllDesktops();
    void testAcceptsFocus_data();
    void testAcceptsFocus();

    void testDesktopIsOpaque();
    void testPanelWindowsCanCover_data();
    void testPanelWindowsCanCover();
    void testOSDPlacement();
    void testOSDPlacementManualPosition();
    void testPanelTypeHasStrut_data();
    void testPanelTypeHasStrut();
    void testPanelActivate_data();
    void testPanelActivate();
    void test_open_under_cursor_data();
    void test_open_under_cursor();

private:
    Wrapland::Client::Compositor* m_compositor = nullptr;
    PlasmaShell* m_plasmaShell = nullptr;
};

void PlasmaSurfaceTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void PlasmaSurfaceTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::plasma_shell);
    m_compositor = Test::get_client().interfaces.compositor.get();
    m_plasmaShell = Test::get_client().interfaces.plasma_shell.get();

    Test::cursor()->set_pos(640, 512);
}

void PlasmaSurfaceTest::cleanup()
{
    Test::destroy_wayland_connection();
    QTRY_VERIFY(Test::app()->base.space->stacking.order.stack.empty());
}

void PlasmaSurfaceTest::testRoleOnAllDesktops_data()
{
    QTest::addColumn<PlasmaShellSurface::Role>("role");
    QTest::addColumn<bool>("expectedOnAllDesktops");

    QTest::newRow("Desktop") << PlasmaShellSurface::Role::Desktop << true;
    QTest::newRow("Panel") << PlasmaShellSurface::Role::Panel << true;
    QTest::newRow("OSD") << PlasmaShellSurface::Role::OnScreenDisplay << true;
    QTest::newRow("Normal") << PlasmaShellSurface::Role::Normal << false;
    QTest::newRow("Notification") << PlasmaShellSurface::Role::Notification << true;
    QTest::newRow("ToolTip") << PlasmaShellSurface::Role::ToolTip << true;
    QTest::newRow("CriticalNotification") << PlasmaShellSurface::Role::CriticalNotification << true;
    QTest::newRow("AppletPopup") << PlasmaShellSurface::Role::AppletPopup << true;
}

void PlasmaSurfaceTest::testRoleOnAllDesktops()
{
    // this test verifies that a XdgShellClient is set on all desktops when the role changes
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);

    // now render to map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);

    // currently the role is not yet set, so the window should not be on all desktops
    QCOMPARE(win::on_all_desktops(c), false);

    // now let's try to change that
    QSignalSpy onAllDesktopsSpy(c->qobject.get(), &win::window_qobject::desktopChanged);
    QVERIFY(onAllDesktopsSpy.isValid());
    QFETCH(PlasmaShellSurface::Role, role);
    plasmaSurface->setRole(role);
    QFETCH(bool, expectedOnAllDesktops);
    QCOMPARE(onAllDesktopsSpy.wait(500), expectedOnAllDesktops);
    QCOMPARE(win::on_all_desktops(c), expectedOnAllDesktops);

    // let's create a second window where we init a little bit different
    // first creating the PlasmaSurface then the Shell Surface
    std::unique_ptr<Surface> surface2(Test::create_surface());
    QVERIFY(surface2);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface2(
        m_plasmaShell->createSurface(surface2.get()));
    QVERIFY(plasmaSurface2);
    plasmaSurface2->setRole(role);
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    QVERIFY(shellSurface2);
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(c2);
    QVERIFY(c != c2);

    QCOMPARE(win::on_all_desktops(c2), expectedOnAllDesktops);
}

void PlasmaSurfaceTest::testAcceptsFocus_data()
{
    QTest::addColumn<PlasmaShellSurface::Role>("role");
    QTest::addColumn<bool>("wantsInput");
    QTest::addColumn<bool>("active");

    QTest::newRow("Desktop") << PlasmaShellSurface::Role::Desktop << true << true;
    QTest::newRow("Panel") << PlasmaShellSurface::Role::Panel << true << false;
    QTest::newRow("OSD") << PlasmaShellSurface::Role::OnScreenDisplay << false << false;
    QTest::newRow("Normal") << PlasmaShellSurface::Role::Normal << true << true;
    QTest::newRow("Notification") << PlasmaShellSurface::Role::Notification << false << false;
    QTest::newRow("ToolTip") << PlasmaShellSurface::Role::ToolTip << false << false;
    QTest::newRow("CriticalNotification")
        << PlasmaShellSurface::Role::CriticalNotification << false << false;
    QTest::newRow("AppletPopup") << PlasmaShellSurface::Role::AppletPopup << true << true;
}

void PlasmaSurfaceTest::testAcceptsFocus()
{
    // this test verifies that some surface roles don't get focus
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    QFETCH(PlasmaShellSurface::Role, role);
    plasmaSurface->setRole(role);

    // now render to map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QTEST(c->wantsInput(), "wantsInput");
    QTEST(c->control->active, "active");
}

void PlasmaSurfaceTest::testDesktopIsOpaque()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Desktop);

    // now render to map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(c->windowType(), NET::Desktop);
    QVERIFY(win::is_desktop(c));

    QVERIFY(!win::has_alpha(*c));
    QCOMPARE(c->render_data.bit_depth, 24);
}

void PlasmaSurfaceTest::testOSDPlacement()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::OnScreenDisplay);

    // now render and map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(c->windowType(), NET::OnScreenDisplay);
    QVERIFY(win::is_on_screen_display(c));
    QCOMPARE(c->geo.frame, QRect(590, 657, 100, 50));

    // change the screen size
    QSignalSpy screensChangedSpy(&Test::app()->base, &base::platform::topology_changed);
    QVERIFY(screensChangedSpy.isValid());

    auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
    Test::app()->set_outputs(geometries);

    QCOMPARE(screensChangedSpy.count(), 1);
    Test::test_outputs_geometries(geometries);
    QCOMPARE(c->geo.frame, QRect(590, 657, 100, 50));

    // change size of window
    QSignalSpy geometryChangedSpy(c->qobject.get(), &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    Test::render(surface, QSize(200, 100), Qt::red);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(c->geo.frame, QRect(540, 632, 200, 100));
}

void PlasmaSurfaceTest::testOSDPlacementManualPosition()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::OnScreenDisplay);

    plasmaSurface->setPosition(QPoint(50, 70));

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // now render and map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QVERIFY(c->isInitialPositionSet());
    QCOMPARE(c->windowType(), NET::OnScreenDisplay);
    QVERIFY(win::is_on_screen_display(c));
    QCOMPARE(c->geo.frame, QRect(50, 70, 100, 50));
}

void PlasmaSurfaceTest::testPanelTypeHasStrut_data()
{
    QTest::addColumn<PlasmaShellSurface::PanelBehavior>("panelBehavior");
    QTest::addColumn<bool>("expectedStrut");
    QTest::addColumn<QRect>("expectedMaxArea");
    QTest::addColumn<KWin::win::layer>("expectedLayer");

    QTest::newRow("always visible") << PlasmaShellSurface::PanelBehavior::AlwaysVisible << true
                                    << QRect(0, 50, 1280, 974) << KWin::win::layer::dock;
    QTest::newRow("autohide") << PlasmaShellSurface::PanelBehavior::AutoHide << false
                              << QRect(0, 0, 1280, 1024) << KWin::win::layer::above;
    QTest::newRow("windows can cover")
        << PlasmaShellSurface::PanelBehavior::WindowsCanCover << false << QRect(0, 0, 1280, 1024)
        << KWin::win::layer::normal;
    QTest::newRow("windows go below") << PlasmaShellSurface::PanelBehavior::WindowsGoBelow << false
                                      << QRect(0, 0, 1280, 1024) << KWin::win::layer::above;
}

void PlasmaSurfaceTest::testPanelTypeHasStrut()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<QObject> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaSurface->setPosition(QPoint(0, 0));
    QFETCH(PlasmaShellSurface::PanelBehavior, panelBehavior);
    plasmaSurface->setPanelBehavior(panelBehavior);

    // now render and map the window
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(c->windowType(), NET::Dock);
    QVERIFY(win::is_dock(c));
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QTEST(c->hasStrut(), "expectedStrut");
    QTEST(win::space_window_area(*Test::app()->base.space, MaximizeArea, 0, 0), "expectedMaxArea");
    QTEST(win::get_layer(*c), "expectedLayer");
}

void PlasmaSurfaceTest::testPanelWindowsCanCover_data()
{
    QTest::addColumn<QRect>("panelGeometry");
    QTest::addColumn<QRect>("windowGeometry");
    QTest::addColumn<QPoint>("triggerPoint");

    QTest::newRow("top-full-edge")
        << QRect(0, 0, 1280, 30) << QRect(0, 0, 200, 300) << QPoint(100, 0);
    QTest::newRow("top-left-edge")
        << QRect(0, 0, 1000, 30) << QRect(0, 0, 200, 300) << QPoint(100, 0);
    QTest::newRow("top-right-edge")
        << QRect(280, 0, 1000, 30) << QRect(1000, 0, 200, 300) << QPoint(1000, 0);
    QTest::newRow("bottom-full-edge")
        << QRect(0, 994, 1280, 30) << QRect(0, 724, 200, 300) << QPoint(100, 1023);
    QTest::newRow("bottom-left-edge")
        << QRect(0, 994, 1000, 30) << QRect(0, 724, 200, 300) << QPoint(100, 1023);
    QTest::newRow("bottom-right-edge")
        << QRect(280, 994, 1000, 30) << QRect(1000, 724, 200, 300) << QPoint(1000, 1023);
    QTest::newRow("left-full-edge")
        << QRect(0, 0, 30, 1024) << QRect(0, 0, 200, 300) << QPoint(0, 100);
    QTest::newRow("left-top-edge")
        << QRect(0, 0, 30, 800) << QRect(0, 0, 200, 300) << QPoint(0, 100);
    QTest::newRow("left-bottom-edge")
        << QRect(0, 200, 30, 824) << QRect(0, 0, 200, 300) << QPoint(0, 250);
    QTest::newRow("right-full-edge")
        << QRect(1250, 0, 30, 1024) << QRect(1080, 0, 200, 300) << QPoint(1279, 100);
    QTest::newRow("right-top-edge")
        << QRect(1250, 0, 30, 800) << QRect(1080, 0, 200, 300) << QPoint(1279, 100);
    QTest::newRow("right-bottom-edge")
        << QRect(1250, 200, 30, 824) << QRect(1080, 0, 200, 300) << QPoint(1279, 250);
}

void PlasmaSurfaceTest::testPanelWindowsCanCover()
{
    // this test verifies the behavior of a panel with windows can cover
    // triggering the screen edge should raise the panel.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    QFETCH(QRect, panelGeometry);
    plasmaSurface->setPosition(panelGeometry.topLeft());
    plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::WindowsCanCover);

    // now render and map the window
    auto panel = Test::render_and_wait_for_shown(surface, panelGeometry.size(), Qt::blue);

    QVERIFY(panel);
    QCOMPARE(panel->windowType(), NET::Dock);
    QVERIFY(win::is_dock(panel));
    QCOMPARE(panel->geo.frame, panelGeometry);
    QCOMPARE(panel->hasStrut(), false);
    QCOMPARE(win::space_window_area(*Test::app()->base.space, MaximizeArea, 0, 0),
             QRect(0, 0, 1280, 1024));
    QCOMPARE(win::get_layer(*panel), KWin::win::layer::normal);

    // create a Window
    std::unique_ptr<Surface> surface2(Test::create_surface());
    QVERIFY(surface2);
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    QVERIFY(shellSurface2);

    QFETCH(QRect, windowGeometry);
    auto c = Test::render_and_wait_for_shown(surface2, windowGeometry.size(), Qt::red);

    QVERIFY(c);
    QCOMPARE(c->windowType(), NET::Normal);
    QVERIFY(c->control->active);
    QCOMPARE(win::get_layer(*c), KWin::win::layer::normal);
    win::move(c, windowGeometry.topLeft());
    QCOMPARE(c->geo.frame, windowGeometry);

    auto stackingOrder = Test::app()->base.space->stacking.order.stack;
    QCOMPARE(stackingOrder.size(), 2);
    QCOMPARE(Test::get_wayland_window(stackingOrder.front()), panel);
    QCOMPARE(Test::get_wayland_window(stackingOrder.back()), c);

    QSignalSpy stackingOrderChangedSpy(Test::app()->base.space->stacking.order.qobject.get(),
                                       &win::stacking_order_qobject::changed);
    QVERIFY(stackingOrderChangedSpy.isValid());
    // trigger screenedge
    QFETCH(QPoint, triggerPoint);
    Test::cursor()->set_pos(triggerPoint);
    QCOMPARE(stackingOrderChangedSpy.count(), 1);
    stackingOrder = Test::app()->base.space->stacking.order.stack;
    QCOMPARE(stackingOrder.size(), 2);
    QCOMPARE(Test::get_wayland_window(stackingOrder.front()), c);
    QCOMPARE(Test::get_wayland_window(stackingOrder.back()), panel);
}

void PlasmaSurfaceTest::testPanelActivate_data()
{
    QTest::addColumn<bool>("wantsFocus");
    QTest::addColumn<bool>("active");

    QTest::newRow("no focus") << false << false;
    QTest::newRow("focus") << true << true;
}

void PlasmaSurfaceTest::testPanelActivate()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    QFETCH(bool, wantsFocus);
    plasmaSurface->setPanelTakesFocus(wantsFocus);

    auto panel = Test::render_and_wait_for_shown(surface, QSize(100, 200), Qt::blue);

    QVERIFY(panel);
    QCOMPARE(panel->windowType(), NET::Dock);
    QVERIFY(win::is_dock(panel));
    QFETCH(bool, active);
    QCOMPARE(panel->dockWantsInput(), active);
    QCOMPARE(panel->control->active, active);
}

void PlasmaSurfaceTest::test_open_under_cursor_data()
{
    QTest::addColumn<QPoint>("cursor_pos");
    QTest::addColumn<QRect>("expected_place");

    QTest::newRow("origin") << QPoint(0, 0) << QRect(0, 0, 100, 50);
    QTest::newRow("offset-small") << QPoint(50, 50) << QRect(0, 25, 100, 50);
    QTest::newRow("offset-large") << QPoint(500, 400) << QRect(450, 375, 100, 50);
}

void PlasmaSurfaceTest::test_open_under_cursor()
{
    QFETCH(QPoint, cursor_pos);
    Test::cursor()->set_pos(cursor_pos);

    auto surface = Test::create_surface();
    QVERIFY(surface);

    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);

    auto plasmaSurface
        = std::unique_ptr<PlasmaShellSurface>(m_plasmaShell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->request_open_under_cursor();

    QFETCH(QRect, expected_place);
    auto c = Test::render_and_wait_for_shown(surface, expected_place.size(), Qt::blue);

    QVERIFY(c);
    QCOMPARE(c->geo.frame, expected_place);
}

}

WAYLANDTEST_MAIN(KWin::PlasmaSurfaceTest)
#include "plasma_surface_test.moc"
