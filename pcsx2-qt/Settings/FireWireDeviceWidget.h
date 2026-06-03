// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "pcsx2/Config.h"

#include "ui_FireWireDeviceWidget.h"

#include <QtGui/QIcon>
#include <QtWidgets/QWidget>

#include <span>

class ControllerSettingsWindow;

class FireWireBindingWidget;

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
	void doDeviceAutomaticBinding(const QString& device);

	Ui::FireWireDeviceWidget m_ui;

	ControllerSettingsWindow* m_dialog;
	std::string m_config_section;
	FireWireBindingWidget* m_bindings_widget = nullptr;
};

class FireWireBindingWidget final : public QWidget
{
	Q_OBJECT

public:
	FireWireBindingWidget(FireWireDeviceWidget* parent);
	~FireWireBindingWidget() override;

	__fi ControllerSettingsWindow* getDialog() const { return static_cast<FireWireDeviceWidget*>(parent())->getDialog(); }
	__fi const std::string& getConfigSection() const { return static_cast<FireWireDeviceWidget*>(parent())->getConfigSection(); }

private:
	std::string getBindingKey(const char* binding_name) const;
	void createWidgets(std::span<const InputBindingInfo> bindings);
};
