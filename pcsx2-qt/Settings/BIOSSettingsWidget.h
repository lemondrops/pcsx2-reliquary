// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_BIOSSettingsWidget.h"

#include "SettingsWidget.h"

class BIOSSettingsWidget : public SettingsWidget
{
	Q_OBJECT

public:
	BIOSSettingsWidget(SettingsWindow* settings_dialog, QWidget* parent);
	~BIOSSettingsWidget();

	static void populateList(QTreeWidget* list, const std::string& directory, const char* setting_key = "BIOS");

private Q_SLOTS:
	void refreshList();

	void retailListItemChanged(const QTreeWidgetItem* current, const QTreeWidgetItem* previous);
	void arcadeListItemChanged(const QTreeWidgetItem* current, const QTreeWidgetItem* previous);

	void fastBootChanged();

private:
	Ui::BIOSSettingsWidget m_ui;
};
