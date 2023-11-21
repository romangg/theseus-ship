/*
SPDX-FileCopyrightText: 2012 Filip Wieladek <wattos@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mouseclick.h"

// KConfigSkeleton
#include "mouseclickconfig.h"

#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/vertex_buffer.h>

#include <KConfigGroup>
#include <QAction>
#include <QPainter>

#include <cmath>

namespace KWin
{

MouseClickEffect::MouseClickEffect()
{
    initConfig<MouseClickConfig>();
    m_enabled = false;

    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("ToggleMouseClick"));
    a->setText(i18n("Toggle Mouse Click Effect"));
    effects->registerGlobalShortcutAndDefault({static_cast<Qt::Key>(Qt::META) + Qt::Key_Asterisk},
                                              a);
    connect(a, &QAction::triggered, this, &MouseClickEffect::toggleEnabled);

    reconfigure(ReconfigureAll);

    m_buttons[0]
        = std::make_unique<MouseButton>(i18nc("Left mouse button", "Left"), Qt::LeftButton);
    m_buttons[1]
        = std::make_unique<MouseButton>(i18nc("Middle mouse button", "Middle"), Qt::MiddleButton);
    m_buttons[2]
        = std::make_unique<MouseButton>(i18nc("Right mouse button", "Right"), Qt::RightButton);
}

MouseClickEffect::~MouseClickEffect()
{
    if (m_enabled) {
        effects->stopMousePolling();
    }
}

void MouseClickEffect::reconfigure(ReconfigureFlags)
{
    MouseClickConfig::self()->read();
    m_colors[0] = MouseClickConfig::color1();
    m_colors[1] = MouseClickConfig::color2();
    m_colors[2] = MouseClickConfig::color3();
    m_lineWidth = MouseClickConfig::lineWidth();
    m_ringLife = MouseClickConfig::ringLife();
    m_ringMaxSize = MouseClickConfig::ringSize();
    m_ringCount = MouseClickConfig::ringCount();
    m_showText = MouseClickConfig::showText();
    m_font = MouseClickConfig::font();
}

void MouseClickEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    const int time
        = m_lastPresentTime.count() ? (data.present_time - m_lastPresentTime).count() : 0;

    for (auto& click : m_clicks) {
        click->m_time += time;
    }

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        if (m_buttons[i]->m_isPressed) {
            m_buttons[i]->m_time += time;
        }
    }

    while (m_clicks.size() > 0) {
        if (m_clicks.front()->m_time <= m_ringLife) {
            break;
        }
        m_clicks.pop_front();
    }

    if (isActive()) {
        m_lastPresentTime = data.present_time;
    } else {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    }

    effects->prePaintScreen(data);
}

void MouseClickEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    paintScreenSetup(data);
    for (const auto& click : m_clicks) {
        for (int i = 0; i < m_ringCount; ++i) {
            float alpha = computeAlpha(click.get(), i);
            float size = computeRadius(click.get(), i);
            if (size > 0 && alpha > 0) {
                QColor color = m_colors[click->m_button];
                color.setAlphaF(alpha);
                drawCircle(color, click->m_pos.x(), click->m_pos.y(), size);
            }
        }

        if (m_showText && click->m_frame) {
            float frameAlpha = (click->m_time * 2.0f - m_ringLife) / m_ringLife;
            frameAlpha = frameAlpha < 0 ? 1 : -(frameAlpha * frameAlpha) + 1;
            click->m_frame->render(infiniteRegion(), frameAlpha, frameAlpha);
        }
    }
    paintScreenFinish(data);
}

void MouseClickEffect::postPaintScreen()
{
    effects->postPaintScreen();
    repaint();
}

float MouseClickEffect::computeRadius(const MouseEvent* click, int ring)
{
    float ringDistance = m_ringLife / (m_ringCount * 3);
    if (click->m_press) {
        return ((click->m_time - ringDistance * ring) / m_ringLife) * m_ringMaxSize;
    }
    return ((m_ringLife - click->m_time - ringDistance * ring) / m_ringLife) * m_ringMaxSize;
}

float MouseClickEffect::computeAlpha(const MouseEvent* click, int ring)
{
    float ringDistance = m_ringLife / (m_ringCount * 3);
    return (m_ringLife - static_cast<float>(click->m_time) - ringDistance * (ring)) / m_ringLife;
}

void MouseClickEffect::slotMouseChanged(const QPoint& pos,
                                        const QPoint&,
                                        Qt::MouseButtons buttons,
                                        Qt::MouseButtons oldButtons,
                                        Qt::KeyboardModifiers,
                                        Qt::KeyboardModifiers)
{
    if (buttons == oldButtons)
        return;

    std::unique_ptr<MouseEvent> m;
    int i = BUTTON_COUNT;
    while (--i >= 0) {
        MouseButton* b = m_buttons[i].get();
        if (isPressed(b->m_button, buttons, oldButtons)) {
            m = std::make_unique<MouseEvent>(
                i, pos, 0, createEffectFrame(pos, b->m_labelDown), true);
            break;
        } else if (isReleased(b->m_button, buttons, oldButtons)
                   && (!b->m_isPressed || b->m_time > m_ringLife)) {
            // we might miss a press, thus also check !b->m_isPressed, bug #314762
            m = std::make_unique<MouseEvent>(
                i, pos, 0, createEffectFrame(pos, b->m_labelUp), false);
            break;
        }
        b->setPressed(b->m_button & buttons);
    }

    if (m) {
        m_clicks.push_back(std::move(m));
    }
    repaint();
}

std::unique_ptr<EffectFrame> MouseClickEffect::createEffectFrame(const QPoint& pos,
                                                                 const QString& text)
{
    if (!m_showText) {
        return nullptr;
    }
    QPoint point(pos.x() + m_ringMaxSize, pos.y());
    std::unique_ptr<EffectFrame> frame
        = effects->effectFrame(EffectFrameStyled, false, point, Qt::AlignLeft);
    frame->setFont(m_font);
    frame->setText(text);
    return frame;
}

void MouseClickEffect::repaint()
{
    if (m_clicks.size() > 0) {
        QRegion dirtyRegion;
        const int radius = m_ringMaxSize + m_lineWidth;
        for (auto& click : m_clicks) {
            dirtyRegion |= QRect(
                click->m_pos.x() - radius, click->m_pos.y() - radius, 2 * radius, 2 * radius);
            if (click->m_frame) {
                // we grant the plasma style 32px padding for stuff like shadows...
                dirtyRegion |= click->m_frame->geometry().adjusted(-32, -32, 32, 32);
            }
        }
        effects->addRepaint(dirtyRegion);
    }
}

bool MouseClickEffect::isReleased(Qt::MouseButtons button,
                                  Qt::MouseButtons buttons,
                                  Qt::MouseButtons oldButtons)
{
    return !(button & buttons) && (button & oldButtons);
}

bool MouseClickEffect::isPressed(Qt::MouseButtons button,
                                 Qt::MouseButtons buttons,
                                 Qt::MouseButtons oldButtons)
{
    return (button & buttons) && !(button & oldButtons);
}

void MouseClickEffect::toggleEnabled()
{
    m_enabled = !m_enabled;

    if (m_enabled) {
        connect(effects, &EffectsHandler::mouseChanged, this, &MouseClickEffect::slotMouseChanged);
        effects->startMousePolling();
    } else {
        disconnect(
            effects, &EffectsHandler::mouseChanged, this, &MouseClickEffect::slotMouseChanged);
        effects->stopMousePolling();
    }

    m_clicks.clear();

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        m_buttons[i]->m_time = 0;
        m_buttons[i]->m_isPressed = false;
    }
}

bool MouseClickEffect::isActive() const
{
    return m_enabled && (m_clicks.size() > 0);
}

void MouseClickEffect::drawCircle(const QColor& color, float cx, float cy, float r)
{
    if (effects->isOpenGLCompositing()) {
        drawCircleGl(color, cx, cy, r);
    } else {
        // Assume QPainter compositing.
        drawCircleQPainter(color, cx, cy, r);
    }
}

void MouseClickEffect::paintScreenSetup(effect::screen_paint_data const& data)
{
    if (effects->isOpenGLCompositing()) {
        paintScreenSetupGl(data);
    }
}

void MouseClickEffect::paintScreenFinish(effect::screen_paint_data const& data)
{
    if (effects->isOpenGLCompositing()) {
        paintScreenFinishGl(data);
    }
}

void MouseClickEffect::drawCircleGl(const QColor& color, float cx, float cy, float r)
{
    static const int num_segments = 80;
    static const float theta = 2 * 3.1415926 / float(num_segments);
    static const float c = cosf(theta); // precalculate the sine and cosine
    static const float s = sinf(theta);
    float t;

    float x = r; // we start at angle = 0
    float y = 0;

    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();

    QVector<QVector2D> verts;
    verts.reserve(num_segments * 2);

    for (int ii = 0; ii < num_segments; ++ii) {
        // output vertex
        verts.push_back(QVector2D(x + cx, y + cy));

        // apply the rotation matrix
        t = x;
        x = c * x - s * y;
        y = s * t + c * y;
    }

    vbo->setVertices(verts);
    ShaderManager::instance()->getBoundShader()->setUniform(GLShader::ColorUniform::Color, color);
    vbo->render(GL_LINE_LOOP);
}

void MouseClickEffect::drawCircleQPainter(const QColor& color, float cx, float cy, float r)
{
    QPainter* painter = effects->scenePainter();
    painter->save();
    painter->setPen(color);
    painter->drawArc(cx - r, cy - r, r * 2, r * 2, 0, 5760);
    painter->restore();
}

void MouseClickEffect::paintScreenSetupGl(effect::screen_paint_data const& data)
{
    auto shader = ShaderManager::instance()->pushShader(ShaderTrait::UniformColor);
    shader->setUniform(GLShader::ModelViewProjectionMatrix, effect::get_mvp(data));

    glLineWidth(m_lineWidth);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MouseClickEffect::paintScreenFinishGl(effect::screen_paint_data const& /*data*/)
{
    glDisable(GL_BLEND);

    ShaderManager::instance()->popShader();
}

QColor MouseClickEffect::color1() const
{
    return m_colors[0];
}

QColor MouseClickEffect::color2() const
{
    return m_colors[1];
}

QColor MouseClickEffect::color3() const
{
    return m_colors[2];
}

qreal MouseClickEffect::lineWidth() const
{
    return m_lineWidth;
}

int MouseClickEffect::ringLife() const
{
    return m_ringLife;
}

int MouseClickEffect::ringSize() const
{
    return m_ringMaxSize;
}

int MouseClickEffect::ringCount() const
{
    return m_ringCount;
}

bool MouseClickEffect::isShowText() const
{
    return m_showText;
}

QFont MouseClickEffect::font() const
{
    return m_font;
}

bool MouseClickEffect::isEnabled() const
{
    return m_enabled;
}

} // namespace
