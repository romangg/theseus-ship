/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

// Must be included before wlr/render/gles2.h
#include <epoxy/egl.h>
#include <epoxy/gl.h>

// Epoxy may not define this, but it's needed by wlroots.
#ifndef PFNEGLQUERYWAYLANDBUFFERWL
typedef EGLBoolean(EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWLPROC)(EGLDisplay dpy,
                                                                struct wl_resource* buffer,
                                                                EGLint attribute,
                                                                EGLint* value);
#define PFNEGLQUERYWAYLANDBUFFERWL PFNEGLQUERYWAYLANDBUFFERWLPROC
#endif

extern "C" {
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#define static
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#undef static
}
