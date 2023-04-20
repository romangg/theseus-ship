/*
    SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_CUBE_CONFIG_H
#define KWIN_CUBE_CONFIG_H

#include <KCModule>

#include "ui_cube_config.h"

namespace KWin
{

class CubeEffectConfigForm : public QWidget, public Ui::CubeEffectConfigForm
{
    Q_OBJECT
public:
    explicit CubeEffectConfigForm(QWidget* parent);
};

class CubeEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit CubeEffectConfig(QObject* parent,
                              const KPluginMetaData& data,
                              const QVariantList& args);

public Q_SLOTS:
    void save() override;

private Q_SLOTS:
    void capsSelectionChanged();

private:
    CubeEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
