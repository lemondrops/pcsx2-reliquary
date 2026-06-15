// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "pcsx2/Config.h"

#include "ui_FireWireDeviceWidget.h"

#include <QtGui/QIcon>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

class ControllerSettingsWindow;
class InputBindingWidget;

class FireWireDeviceWidget final : public QWidget
{
	Q_OBJECT

public:
	FireWireDeviceWidget(QWidget* parent, ControllerSettingsWindow* dialog);
	~FireWireDeviceWidget();

	QIcon getIcon() const;

	__fi ControllerSettingsWindow* getDialog() const { return m_dialog; }
	__fi const std::string& getConfigSection() const { return m_config_section; }

private Q_SLOTS:
	void onBindingsClicked();
	void onAutomaticBindingClicked();
	void onClearBindingsClicked();

private:
	void updateHeaderToolButtons();
	void initializeBindingWidgets();
	void reloadBindingWidgets();
	QString getCurrentIOMode() const;
	QString getBindingWidgetIOMode(const InputBindingWidget* widget) const;
	void doDeviceAutomaticBinding(const QString& device);

	Ui::FireWireDeviceWidget m_ui;

	ControllerSettingsWindow* m_dialog;
	std::string m_config_section;
	QWidget* m_bindings_widget = nullptr;
};
