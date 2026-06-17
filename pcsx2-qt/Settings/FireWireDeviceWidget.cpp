// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Settings/FireWireDeviceWidget.h"

#include "common/SettingsInterface.h"

#include "pcsx2/FW.h"
#include "pcsx2/Host.h"
#include "pcsx2/Input/InputManager.h"

#include "QtHost.h"
#include "QtUtils.h"
#include "Settings/ControllerSettingsWindow.h"
#include "Settings/InputBindingWidget.h"

#include <QtCore/QSignalBlocker>
#include <QtCore/QVariant>
#include <QtGui/QCursor>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>

FireWireDeviceWidget::FireWireDeviceWidget(QWidget* parent, ControllerSettingsWindow* dialog)
	: QWidget(parent)
	, m_dialog(dialog)
	, m_config_section(FireWire::GetConfigSection())
{
	m_ui.setupUi(this);

	m_bindings_widget = m_ui.bindingsPage;
	m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
	initializeBindingWidgets();

	connect(m_ui.bindings, &QPushButton::clicked, this, &FireWireDeviceWidget::onBindingsClicked);
	connect(m_ui.automaticBinding, &QPushButton::clicked, this, &FireWireDeviceWidget::onAutomaticBindingClicked);
	connect(m_ui.clearBindings, &QPushButton::clicked, this, &FireWireDeviceWidget::onClearBindingsClicked);

	updateHeaderToolButtons();
}

FireWireDeviceWidget::~FireWireDeviceWidget() = default;

QIcon FireWireDeviceWidget::getIcon() const
{
	return QIcon::fromTheme("keyboardmania-line");
}

void FireWireDeviceWidget::updateHeaderToolButtons()
{
	const QWidget* current_widget = m_ui.stackedWidget->currentWidget();
	const QSignalBlocker bindings_sb(m_ui.bindings);
	m_ui.bindings->setChecked(current_widget == m_bindings_widget);
	m_ui.automaticBinding->setEnabled(current_widget == m_bindings_widget);
	m_ui.clearBindings->setEnabled(current_widget == m_bindings_widget);
}

void FireWireDeviceWidget::onBindingsClicked()
{
	m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
	updateHeaderToolButtons();
}

void FireWireDeviceWidget::onAutomaticBindingClicked()
{
	QMenu menu(this);
	bool added = false;

	for (const QPair<QString, QString>& dev : m_dialog->getDeviceList())
	{
		QAction* action;
		if (dev.first.compare(dev.second, Qt::CaseInsensitive) == 0)
			action = menu.addAction(dev.first);
		else
			action = menu.addAction(QStringLiteral("%1: %2").arg(dev.first).arg(dev.second));
		action->setData(dev.first);
		connect(action, &QAction::triggered, this, [this, action]() { doDeviceAutomaticBinding(action->data().toString()); });
		added = true;
	}

	if (!added)
	{
		QAction* action = menu.addAction(tr("No devices available"));
		action->setEnabled(false);
	}

	menu.exec(QCursor::pos());
}

void FireWireDeviceWidget::onClearBindingsClicked()
{
	if (QMessageBox::question(QtUtils::GetRootWidget(this), tr("Clear Bindings"),
			tr("Are you sure you want to clear all FireWire bindings? This action cannot be undone.")) != QMessageBox::Yes)
	{
		return;
	}

	if (m_dialog->isEditingGlobalSettings())
	{
		{
			auto lock = Host::GetSettingsLock();
			FireWire::ClearP1IOBindings(*Host::Internal::GetBaseSettingsLayer());
		}
		Host::CommitBaseSettingChanges();
	}
	else
	{
		FireWire::ClearP1IOBindings(*m_dialog->getProfileSettingsInterface());
		m_dialog->getProfileSettingsInterface()->Save();
	}

	g_emu_thread->applySettings();
	m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
	reloadBindingWidgets();
	updateHeaderToolButtons();
}

void FireWireDeviceWidget::initializeBindingWidgets()
{
	SettingsInterface* sif = m_dialog->getProfileSettingsInterface();

	for (InputBindingWidget* widget : m_bindings_widget->findChildren<InputBindingWidget*>())
	{
		QString binding_name = widget->property("bindingName").toString();
		if (binding_name.isEmpty())
			binding_name = widget->objectName();

		QString io_mode = getBindingWidgetIOMode(widget);
		const std::string key = io_mode.isEmpty() ? FireWire::GetP1IOUniversalConfigSubKey(binding_name.toStdString()) :
			FireWire::GetP1IOConfigSubKey(io_mode.toStdString(), binding_name.toStdString());

		widget->initialize(sif, InputBindingInfo::Type::Button, getConfigSection(), key);
	}
}

QString FireWireDeviceWidget::getCurrentIOMode() const
{
	SettingsInterface* sif = m_dialog->getProfileSettingsInterface();
	std::string mode = sif ? sif->GetStringValue("Python1/Game", "IOMode", "JVS") : "JVS";
	if (QString::fromStdString(mode).compare(QStringLiteral("EXTIO"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("EXTIO");
	if (QString::fromStdString(mode).compare(QStringLiteral("POPN"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("POPN");
	if (QString::fromStdString(mode).compare(QStringLiteral("B22"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("B22");
	if (QString::fromStdString(mode).compare(QStringLiteral("DOGSTATIONDX"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("DOGSTATIONDX");
	return QStringLiteral("JVS");
}

QString FireWireDeviceWidget::getBindingWidgetIOMode(const InputBindingWidget* widget) const
{
	const QString name = widget->objectName();
	if (name.startsWith(QStringLiteral("jvs")))
		return QStringLiteral("JVS");
	if (name.startsWith(QStringLiteral("popn")))
		return QStringLiteral("POPN");
	if (name.startsWith(QStringLiteral("ddr")))
		return QStringLiteral("EXTIO");
	return QString();
}

void FireWireDeviceWidget::reloadBindingWidgets()
{
	for (InputBindingWidget* widget : m_bindings_widget->findChildren<InputBindingWidget*>())
		widget->reloadBinding();
}

void FireWireDeviceWidget::doDeviceAutomaticBinding(const QString& device)
{
	std::vector<std::pair<GenericInputBinding, std::string>> mapping = InputManager::GetGenericBindingMapping(device.toStdString());
	if (mapping.empty())
	{
		QMessageBox::critical(QtUtils::GetRootWidget(this), tr("Automatic Binding"),
			tr("No generic bindings were generated for device '%1'. The controller/source may not support automatic mapping.").arg(device));
		return;
	}

	bool result;
	if (m_dialog->isEditingGlobalSettings())
	{
		{
			auto lock = Host::GetSettingsLock();
			result = FireWire::MapP1IO(*Host::Internal::GetBaseSettingsLayer(), mapping);
		}
		if (result)
			Host::CommitBaseSettingChanges();
	}
	else
	{
		result = FireWire::MapP1IO(*m_dialog->getProfileSettingsInterface(), mapping);
		if (result)
		{
			m_dialog->getProfileSettingsInterface()->Save();
			g_emu_thread->reloadInputBindings();
		}
	}

	if (result)
	{
		g_emu_thread->applySettings();
		m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
		reloadBindingWidgets();
		updateHeaderToolButtons();
	}
}

#include "moc_FireWireDeviceWidget.cpp"
