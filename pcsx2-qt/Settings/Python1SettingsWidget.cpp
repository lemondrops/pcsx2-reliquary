// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "PrecompiledHeader.h"

#include "pcsx2/GameList.h"

#include "Python1SettingsWidget.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsWindow.h"

Python1SettingsWidget::Python1SettingsWidget(const GameList::Entry* entry, SettingsWindow* window, QWidget* parent)
	: QWidget(parent)
	, m_window(window)
{
	SettingsInterface* sif = window->getSettingsInterface();

	m_ui.setupUi(this);
	m_ui.ioMode->addItem(tr("JVS"), QStringLiteral("JVS"));
	m_ui.ioMode->addItem(tr("EXTIO"), QStringLiteral("EXTIO"));
	m_ui.ioMode->addItem(tr("Pop'n"), QStringLiteral("POPN"));
	m_ui.ioMode->addItem(tr("Dogstation"), QStringLiteral("DOGSTATION"));
	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioMode, "Python1/Game", "IOMode", "JVS");

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.hddImagePath, "Python1/Game", "HddImageFile", "");
	m_ui.hddImagePath->setEnabled(true);
	connect(m_ui.hddImageBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onHddImageBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.bbsramPath, "Python1/Game", "BBSRamFile", "");
	m_ui.bbsramPath->setEnabled(true);
	connect(m_ui.bbsramBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onBbsramBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioBootRomPath, "Python1/Game", "IOBootRomFile", "");
	m_ui.ioBootRomPath->setEnabled(true);
	connect(m_ui.ioBootRomBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onIoBootRomBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioConfigRomPath, "Python1/Game", "IOConfigRomFile", "");
	m_ui.ioConfigRomPath->setEnabled(true);
	connect(m_ui.ioConfigRomBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onIoConfigRomBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.dongleBlackPath, "Python1/Game", "DongleBlackFile", "");
	m_ui.dongleBlackPath->setEnabled(true);
	connect(m_ui.dongleBlackBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onDongleBlackBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.dongleWhitePath, "Python1/Game", "DongleWhiteFile", "");
	m_ui.dongleWhitePath->setEnabled(true);
	connect(m_ui.dongleWhiteBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onDongleWhiteBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.memoryCardDonglePath, "Python1/Game", "MemoryCardDongleFile", "");
	m_ui.memoryCardDonglePath->setEnabled(true);
	connect(m_ui.memoryCardDongleBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onMemoryCardDongleBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.memoryCardIdPath, "Python1/Game", "MemoryCardIdFile", "");
	m_ui.memoryCardIdPath->setEnabled(true);
	connect(m_ui.memoryCardIdBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onMemoryCardIdBrowseClicked);
}

void Python1SettingsWidget::onHddImageBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("HDD Image File"),
			!m_ui.hddImagePath->text().isEmpty() ? m_ui.hddImagePath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.hddImagePath->setText(path);
	m_ui.hddImagePath->editingFinished();
}

void Python1SettingsWidget::onBbsramBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getSaveFileName(QtUtils::GetRootWidget(this), tr("BBSRAM File"),
			!m_ui.bbsramPath->text().isEmpty() ? m_ui.bbsramPath->text() : QString(), tr("All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.bbsramPath->setText(path);
	m_ui.bbsramPath->editingFinished();
}

void Python1SettingsWidget::onIoBootRomBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getSaveFileName(QtUtils::GetRootWidget(this), tr("I/O Boot ROM File"),
			!m_ui.ioBootRomPath->text().isEmpty() ? m_ui.ioBootRomPath->text() : QString(), tr("ROM (*.rom);;BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.ioBootRomPath->setText(path);
	m_ui.ioBootRomPath->editingFinished();
}

void Python1SettingsWidget::onIoConfigRomBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("I/O Config ROM File"),
			!m_ui.ioConfigRomPath->text().isEmpty() ? m_ui.ioConfigRomPath->text() : QString(), tr("ROM (*.rom);;BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.ioConfigRomPath->setText(path);
	m_ui.ioConfigRomPath->editingFinished();
}

void Python1SettingsWidget::onDongleBlackBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("Black Dongle File"),
			!m_ui.dongleBlackPath->text().isEmpty() ? m_ui.dongleBlackPath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.dongleBlackPath->setText(path);
	m_ui.dongleBlackPath->editingFinished();
}

void Python1SettingsWidget::onDongleWhiteBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("White Dongle File"),
			!m_ui.dongleWhitePath->text().isEmpty() ? m_ui.dongleWhitePath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.dongleWhitePath->setText(path);
	m_ui.dongleWhitePath->editingFinished();
}

void Python1SettingsWidget::onMemoryCardDongleBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("Memory Card Dongle File"),
			!m_ui.memoryCardDonglePath->text().isEmpty() ? m_ui.memoryCardDonglePath->text() : QString(),
			tr("Memory Cards (*.ps2 *.mcd *.mc2 *.bin);;All Files (*)"), nullptr, QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.memoryCardDonglePath->setText(path);
	m_ui.memoryCardDonglePath->editingFinished();
}

void Python1SettingsWidget::onMemoryCardIdBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("Memory Card ID File"),
			!m_ui.memoryCardIdPath->text().isEmpty() ? m_ui.memoryCardIdPath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.memoryCardIdPath->setText(path);
	m_ui.memoryCardIdPath->editingFinished();
}

Python1SettingsWidget::~Python1SettingsWidget() = default;

#include "moc_Python1SettingsWidget.cpp"
