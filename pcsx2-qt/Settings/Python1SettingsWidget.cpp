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
	m_ui.ioMode->addItem(tr("B22"), QStringLiteral("B22"));
	m_ui.ioMode->addItem(tr("DogStation DX"), QStringLiteral("DOGSTATIONDX"));
	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioMode, "Python1/Game", "IOMode", "JVS");

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.hddImagePath, "Python1/Game", "HddImageFile", "");
	m_ui.hddImagePath->setEnabled(true);
	connect(m_ui.hddImageBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onHddImageBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.cfImagePath, "Python1/Game", "CfImageFile", "");
	m_ui.cfImagePath->setEnabled(true);
	connect(m_ui.cfImageBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onCfImageBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.bbsramPath, "Python1/Game", "BBSRamFile", "");
	m_ui.bbsramPath->setEnabled(true);
	connect(m_ui.bbsramBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onBbsramBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioBootRomPath, "Python1/Game", "IOBootRomFile", "");
	m_ui.ioBootRomPath->setEnabled(true);
	connect(m_ui.ioBootRomBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onIoBootRomBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.ioConfigRomPath, "Python1/Game", "IOConfigRomFile", "");
	m_ui.ioConfigRomPath->setEnabled(true);
	connect(m_ui.ioConfigRomBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onIoConfigRomBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.internalDonglePath, "Python1/Game", "InternalDongleFile", "");
	m_ui.internalDonglePath->setEnabled(true);
	connect(m_ui.internalDongleBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onInternalDongleBrowseClicked);

	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.externalDonglePath, "Python1/Game", "ExternalDongleFile", "");
	m_ui.externalDonglePath->setEnabled(true);
	connect(m_ui.externalDongleBrowse, &QPushButton::clicked, this, &Python1SettingsWidget::onExternalDongleBrowseClicked);

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
			!m_ui.hddImagePath->text().isEmpty() ? m_ui.hddImagePath->text() : QString(), tr("HDD Images (*.bin *.raw *.chd *.dsk);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.hddImagePath->setText(path);
	m_ui.hddImagePath->editingFinished();
}

void Python1SettingsWidget::onCfImageBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("CF Image File"),
			!m_ui.cfImagePath->text().isEmpty() ? m_ui.cfImagePath->text() : QString(), tr("CF Images (*.bin *.raw *.dsk);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.cfImagePath->setText(path);
	m_ui.cfImagePath->editingFinished();
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

void Python1SettingsWidget::onInternalDongleBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("Internal Dongle File"),
			!m_ui.internalDonglePath->text().isEmpty() ? m_ui.internalDonglePath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.internalDonglePath->setText(path);
	m_ui.internalDonglePath->editingFinished();
}

void Python1SettingsWidget::onExternalDongleBrowseClicked()
{
	QString path =
		QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(this), tr("External Dongle File"),
			!m_ui.externalDonglePath->text().isEmpty() ? m_ui.externalDonglePath->text() : QString(), tr("BIN (*.bin);;All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite));

	if (path.isEmpty())
		return;

	m_ui.externalDonglePath->setText(path);
	m_ui.externalDonglePath->editingFinished();
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
