/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#define WL_EGL_PLATFORM 1
#include "egl_wayland_backend.h"
// kwin
#include "composite.h"
#include "options.h"
#include "wayland_backend.h"
#include <KWayland/Client/surface.h>
#include "xcbutils.h"
// kwin libs
#include <kwinglplatform.h>
// KDE
// Qt
#include <QOpenGLContext>

namespace KWin
{

EglWaylandBackend::EglWaylandBackend()
    : QObject(NULL)
    , OpenGLBackend()
    , m_context(EGL_NO_CONTEXT)
    , m_bufferAge(0)
    , m_wayland(Wayland::WaylandBackend::self())
    , m_overlay(NULL)
{
    if (!m_wayland) {
        setFailed("Wayland Backend has not been created");
        return;
    }
    qCDebug(KWIN_CORE) << "Connected to Wayland display?" << (m_wayland->display() ? "yes" : "no" );
    if (!m_wayland->display()) {
        setFailed("Could not connect to Wayland compositor");
        return;
    }
    connect(m_wayland, SIGNAL(shellSurfaceSizeChanged(QSize)), SLOT(overlaySizeChanged(QSize)));
    initializeEgl();
    init();
    // Egl is always direct rendering
    setIsDirectRendering(true);

    qCWarning(KWIN_CORE) << "Using Wayland rendering backend";
    qCWarning(KWIN_CORE) << "This is a highly experimental backend, do not use for productive usage!";
    qCWarning(KWIN_CORE) << "Please do not report any issues you might encounter when using this backend!";
}

EglWaylandBackend::~EglWaylandBackend()
{
    cleanupGL();
    doneCurrent();
    eglDestroyContext(m_display, m_context);
    eglDestroySurface(m_display, m_surface);
    eglTerminate(m_display);
    eglReleaseThread();
    if (m_overlay) {
        wl_egl_window_destroy(m_overlay);
    }
}

bool EglWaylandBackend::initializeEgl()
{
    // Get the list of client extensions
    const QByteArray clientExtensionString = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (clientExtensionString.isEmpty()) {
        // If eglQueryString() returned NULL, the implementation doesn't support
        // EGL_EXT_client_extensions. Expect an EGL_BAD_DISPLAY error.
        (void) eglGetError();
    }

    const QList<QByteArray> clientExtensions = clientExtensionString.split(' ');

    // Use eglGetPlatformDisplayEXT() to get the display pointer
    // if the implementation supports it.
    m_havePlatformBase = clientExtensions.contains("EGL_EXT_platform_base");
    if (m_havePlatformBase) {
        // Make sure that the wayland platform is supported
        if (!clientExtensions.contains("EGL_EXT_platform_wayland"))
            return false;

        m_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, m_wayland->display(), nullptr);
    } else {
        m_display = eglGetDisplay(m_wayland->display());
    }

    if (m_display == EGL_NO_DISPLAY)
        return false;

    EGLint major, minor;
    if (eglInitialize(m_display, &major, &minor) == EGL_FALSE)
        return false;
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_CORE) << "Error during eglInitialize " << error;
        return false;
    }
    qCDebug(KWIN_CORE) << "Egl Initialize succeeded";

#ifdef KWIN_HAVE_OPENGLES
    eglBindAPI(EGL_OPENGL_ES_API);
#else
    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
        qCCritical(KWIN_CORE) << "bind OpenGL API failed";
        return false;
    }
#endif
    qCDebug(KWIN_CORE) << "EGL version: " << major << "." << minor;
    return true;
}

void EglWaylandBackend::init()
{
    if (!initRenderingContext()) {
        setFailed("Could not initialize rendering context");
        return;
    }

    initEGL();
    GLPlatform *glPlatform = GLPlatform::instance();
    glPlatform->detect(EglPlatformInterface);
    glPlatform->printResults();
    initGL(EglPlatformInterface);

    setSupportsBufferAge(false);

    if (hasGLExtension(QByteArrayLiteral("EGL_EXT_buffer_age"))) {
        const QByteArray useBufferAge = qgetenv("KWIN_USE_BUFFER_AGE");

        if (useBufferAge != "0")
            setSupportsBufferAge(true);
    }
}

bool EglWaylandBackend::initRenderingContext()
{
    initBufferConfigs();

#ifdef KWIN_HAVE_OPENGLES
    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, context_attribs);
#else
    const EGLint context_attribs_31_core[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 1,
        EGL_CONTEXT_FLAGS_KHR,         EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
        EGL_NONE
    };

    const EGLint context_attribs_legacy[] = {
        EGL_NONE
    };

    const QByteArray eglExtensions = eglQueryString(m_display, EGL_EXTENSIONS);
    const QList<QByteArray> extensions = eglExtensions.split(' ');

    // Try to create a 3.1 core context
    if (options->glCoreProfile() && extensions.contains("EGL_KHR_create_context"))
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, context_attribs_31_core);

    if (m_context == EGL_NO_CONTEXT)
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, context_attribs_legacy);
#endif

    if (m_context == EGL_NO_CONTEXT) {
        qCCritical(KWIN_CORE) << "Create Context failed";
        return false;
    }

    if (!m_wayland->surface()) {
        return false;
    }

    const QSize &size = m_wayland->shellSurfaceSize();
    auto s = m_wayland->surface();
    connect(s, &KWayland::Client::Surface::frameRendered, Compositor::self(), &Compositor::bufferSwapComplete);
    m_overlay = wl_egl_window_create(*s, size.width(), size.height());
    if (!m_overlay) {
        qCCritical(KWIN_CORE) << "Creating Wayland Egl window failed";
        return false;
    }

    if (m_havePlatformBase)
        m_surface = eglCreatePlatformWindowSurfaceEXT(m_display, m_config, (void *) m_overlay, nullptr);
    else
        m_surface = eglCreateWindowSurface(m_display, m_config, m_overlay, nullptr);

    if (m_surface == EGL_NO_SURFACE) {
        qCCritical(KWIN_CORE) << "Create Window Surface failed";
        return false;
    }

    return makeContextCurrent();
}

bool EglWaylandBackend::makeContextCurrent()
{
    if (eglMakeCurrent(m_display, m_surface, m_surface, m_context) == EGL_FALSE) {
        qCCritical(KWIN_CORE) << "Make Context Current failed";
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_CORE) << "Error occurred while creating context " << error;
        return false;
    }
    return true;
}

bool EglWaylandBackend::initBufferConfigs()
{
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,         EGL_WINDOW_BIT,
        EGL_RED_SIZE,             1,
        EGL_GREEN_SIZE,           1,
        EGL_BLUE_SIZE,            1,
        EGL_ALPHA_SIZE,           0,
#ifdef KWIN_HAVE_OPENGLES
        EGL_RENDERABLE_TYPE,      EGL_OPENGL_ES2_BIT,
#else
        EGL_RENDERABLE_TYPE,      EGL_OPENGL_BIT,
#endif
        EGL_CONFIG_CAVEAT,        EGL_NONE,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig configs[1024];
    if (eglChooseConfig(m_display, config_attribs, configs, 1, &count) == EGL_FALSE) {
        qCCritical(KWIN_CORE) << "choose config failed";
        return false;
    }
    if (count != 1) {
        qCCritical(KWIN_CORE) << "choose config did not return a config" << count;
        return false;
    }
    m_config = configs[0];

    return true;
}

void EglWaylandBackend::present()
{
    m_wayland->surface()->setupFrameCallback();
    Compositor::self()->aboutToSwapBuffers();

    if (supportsBufferAge()) {
        eglSwapBuffers(m_display, m_surface);
        eglQuerySurface(m_display, m_surface, EGL_BUFFER_AGE_EXT, &m_bufferAge);
        setLastDamage(QRegion());
        return;
    } else {
        eglSwapBuffers(m_display, m_surface);
        setLastDamage(QRegion());
    }
}

void EglWaylandBackend::screenGeometryChanged(const QSize &size)
{
    Q_UNUSED(size)
    // no backend specific code needed
    // TODO: base implementation in OpenGLBackend

    // The back buffer contents are now undefined
    m_bufferAge = 0;
}

SceneOpenGL::TexturePrivate *EglWaylandBackend::createBackendTexture(SceneOpenGL::Texture *texture)
{
    return new EglWaylandTexture(texture, this);
}

QRegion EglWaylandBackend::prepareRenderingFrame()
{
    if (!lastDamage().isEmpty())
        present();
    QRegion repaint;
    if (supportsBufferAge())
        repaint = accumulatedDamageHistory(m_bufferAge);
    eglWaitNative(EGL_CORE_NATIVE_ENGINE);
    startRenderTimer();
    return repaint;
}

void EglWaylandBackend::endRenderingFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    if (damagedRegion.isEmpty()) {
        setLastDamage(QRegion());

        // If the damaged region of a window is fully occluded, the only
        // rendering done, if any, will have been to repair a reused back
        // buffer, making it identical to the front buffer.
        //
        // In this case we won't post the back buffer. Instead we'll just
        // set the buffer age to 1, so the repaired regions won't be
        // rendered again in the next frame.
        if (!renderedRegion.isEmpty())
            glFlush();

        m_bufferAge = 1;
        return;
    }

    setLastDamage(renderedRegion);

    if (!blocksForRetrace()) {
        // This also sets lastDamage to empty which prevents the frame from
        // being posted again when prepareRenderingFrame() is called.
        present();
    } else {
        // Make sure that the GPU begins processing the command stream
        // now and not the next time prepareRenderingFrame() is called.
        glFlush();
    }

    // Save the damaged region to history
    if (supportsBufferAge())
        addToDamageHistory(damagedRegion);
}

bool EglWaylandBackend::makeCurrent()
{
    if (QOpenGLContext *context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    const bool current = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    return current;
}

void EglWaylandBackend::doneCurrent()
{
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

Xcb::Shm *EglWaylandBackend::shm()
{
    if (m_shm.isNull()) {
        m_shm.reset(new Xcb::Shm);
    }
    return m_shm.data();
}

void EglWaylandBackend::overlaySizeChanged(const QSize &size)
{
    wl_egl_window_resize(m_overlay, size.width(), size.height(), 0, 0);
}

bool EglWaylandBackend::usesOverlayWindow() const
{
    return false;
}

/************************************************
 * EglTexture
 ************************************************/

EglWaylandTexture::EglWaylandTexture(KWin::SceneOpenGL::Texture *texture, KWin::EglWaylandBackend *backend)
    : SceneOpenGL::TexturePrivate()
    , q(texture)
    , m_backend(backend)
    , m_referencedPixmap(XCB_PIXMAP_NONE)
{
    m_target = GL_TEXTURE_2D;
}

EglWaylandTexture::~EglWaylandTexture()
{
}

OpenGLBackend *EglWaylandTexture::backend()
{
    return m_backend;
}

bool EglWaylandTexture::loadTexture(xcb_pixmap_t pix, const QSize &size, xcb_visualid_t visual)
{
    Q_UNUSED(visual)

    // HACK: egl wayland platform doesn't support texture from X11 pixmap through the KHR_image_pixmap
    // extension. To circumvent this problem we copy the pixmap content into a SHM image and from there
    // to the OpenGL texture. This is a temporary solution. In future we won't need to get the content
    // from X11 pixmaps. That's what we have XWayland for to get the content into a nice Wayland buffer.
    if (pix == XCB_PIXMAP_NONE)
        return false;

    m_referencedPixmap = pix;

    Xcb::Shm *shm = m_backend->shm();
    if (!shm->isValid()) {
        return false;
    }

    xcb_shm_get_image_cookie_t cookie = xcb_shm_get_image_unchecked(connection(), pix, 0, 0, size.width(),
        size.height(), ~0, XCB_IMAGE_FORMAT_Z_PIXMAP, shm->segment(), 0);

    glGenTextures(1, &m_texture);
    q->setWrapMode(GL_CLAMP_TO_EDGE);
    q->setFilter(GL_LINEAR);
    q->bind();

    ScopedCPointer<xcb_shm_get_image_reply_t> image(xcb_shm_get_image_reply(connection(), cookie, NULL));
    if (image.isNull()) {
        return false;
    }

    // TODO: other formats
#ifndef KWIN_HAVE_OPENGLES
    glTexImage2D(m_target, 0, GL_RGBA8, size.width(), size.height(), 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, shm->buffer());
#endif

    q->unbind();
    q->setYInverted(true);
    m_size = size;
    updateMatrix();
    return true;
}

bool EglWaylandTexture::update(const QRegion &damage)
{
    if (m_referencedPixmap == XCB_PIXMAP_NONE) {
        return false;
    }

    Xcb::Shm *shm = m_backend->shm();
    if (!shm->isValid()) {
        return false;
    }

    // TODO: optimize by only updating the damaged areas
    const QRect &damagedRect = damage.boundingRect();
    xcb_shm_get_image_cookie_t cookie = xcb_shm_get_image_unchecked(connection(), m_referencedPixmap,
        damagedRect.x(), damagedRect.y(), damagedRect.width(), damagedRect.height(),
        ~0, XCB_IMAGE_FORMAT_Z_PIXMAP, shm->segment(), 0);

    q->bind();

    ScopedCPointer<xcb_shm_get_image_reply_t> image(xcb_shm_get_image_reply(connection(), cookie, NULL));
    if (image.isNull()) {
        return false;
    }

    // TODO: other formats
#ifndef KWIN_HAVE_OPENGLES
    glTexSubImage2D(m_target, 0, damagedRect.x(), damagedRect.y(), damagedRect.width(), damagedRect.height(), GL_BGRA, GL_UNSIGNED_BYTE, shm->buffer());
#endif

    q->unbind();
    return true;
}

} // namespace
