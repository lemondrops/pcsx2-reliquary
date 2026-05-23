// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <QtWidgets/QWidget>

#include "ui_Python1SettingsWidget.h"

class SettingsWindow;

namespace GameList
{
	struct Entry;
}

class Python1SettingsWidget : public QWidget
{
	Q_OBJECT

private Q_SLOTS:
	void onHddImageBrowseClicked();
	void onBbsramBrowseClicked();
	void onIoBootRomBrowseClicked();
	void onDongleBlackBrowseClicked();
	void onDongleWhiteBrowseClicked();
	void onMemoryCardDongleBrowseClicked();

public:
	Python1SettingsWidget(const GameList::Entry* entry, SettingsWindow* window, QWidget* parent);
	~Python1SettingsWidget();

private:
	Ui::Python1SettingsWidget m_ui;
	SettingsWindow* m_window;
};
