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

#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

#include "fmt/format.h"

FireWireDeviceWidget::FireWireDeviceWidget(QWidget* parent, ControllerSettingsWindow* dialog)
	: QWidget(parent)
	, m_dialog(dialog)
	, m_config_section(FireWire::GetConfigSection())
{
	m_ui.setupUi(this);

	m_bindings_widget = new FireWireBindingWidget(this);
	m_ui.stackedWidget->addWidget(m_bindings_widget);
	m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);

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
	m_ui.stackedWidget->removeWidget(m_bindings_widget);
	m_bindings_widget->deleteLater();
	m_bindings_widget = new FireWireBindingWidget(this);
	m_ui.stackedWidget->addWidget(m_bindings_widget);
	m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
	updateHeaderToolButtons();
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
		m_ui.stackedWidget->removeWidget(m_bindings_widget);
		m_bindings_widget->deleteLater();
		m_bindings_widget = new FireWireBindingWidget(this);
		m_ui.stackedWidget->addWidget(m_bindings_widget);
		m_ui.stackedWidget->setCurrentWidget(m_bindings_widget);
		updateHeaderToolButtons();
	}
}

FireWireBindingWidget::FireWireBindingWidget(FireWireDeviceWidget* parent)
	: QWidget(parent)
{
	createWidgets(FireWire::GetP1IOBindings());
}

FireWireBindingWidget::~FireWireBindingWidget() = default;

std::string FireWireBindingWidget::getBindingKey(const char* binding_name) const
{
	return FireWire::GetConfigSubKey(binding_name);
}

void FireWireBindingWidget::createWidgets(std::span<const InputBindingInfo>)
{
	SettingsInterface* sif = getDialog()->getProfileSettingsInterface();

	QScrollArea* scrollarea = new QScrollArea(this);
	QWidget* scrollarea_widget = new QWidget(scrollarea);
	scrollarea->setWidget(scrollarea_widget);
	scrollarea->setWidgetResizable(true);
	scrollarea->setFrameShape(QFrame::NoFrame);
	scrollarea->setFrameShadow(QFrame::Plain);

	auto add_binding = [this, sif](QGridLayout* layout, int row, int column, const char* title, const char* binding_name,
		int minimum_width = 150, int column_span = 1) {
		QGroupBox* gbox = new QGroupBox(qApp->translate("FireWire", title));
		QVBoxLayout* box_layout = new QVBoxLayout(gbox);
		box_layout->setContentsMargins(6, 6, 6, 6);
		InputBindingWidget* widget = new InputBindingWidget(
			gbox, sif, InputBindingInfo::Type::Button, getConfigSection(), getBindingKey(binding_name));
		widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		widget->setMinimumWidth(minimum_width);
		box_layout->addWidget(widget);
		layout->addWidget(gbox, row, column, 1, column_span);
	};

	auto create_player_controls = [add_binding](QWidget* parent, const QString& title, const char* prefix) {
		QGroupBox* group = new QGroupBox(title, parent);
		QGridLayout* layout = new QGridLayout(group);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(6);

		const std::string start = fmt::format("{}Start", prefix);
		const std::string up = fmt::format("{}Up", prefix);
		const std::string down = fmt::format("{}Down", prefix);
		const std::string left = fmt::format("{}Left", prefix);
		const std::string right = fmt::format("{}Right", prefix);
		add_binding(layout, 0, 1, "Up", up.c_str());
		add_binding(layout, 1, 0, "Left", left.c_str());
		add_binding(layout, 1, 1, "Start", start.c_str());
		add_binding(layout, 1, 2, "Right", right.c_str());
		add_binding(layout, 2, 1, "Down", down.c_str());

		for (int i = 0; i < 6; i++)
		{
			const std::string button = fmt::format("{}Button{}", prefix, i + 1);
			const std::string label = fmt::format("Button {}", i + 1);
			add_binding(layout, 3 + (i / 3), i % 3, label.c_str(), button.c_str());
		}

		return group;
	};

	auto create_ddr_controls = [add_binding](QWidget* parent, const QString& title, const char* prefix) {
		QGroupBox* group = new QGroupBox(title, parent);
		QGridLayout* layout = new QGridLayout(group);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(6);

		const std::string start = fmt::format("{}Start", prefix);
		const std::string up = fmt::format("{}Up", prefix);
		const std::string down = fmt::format("{}Down", prefix);
		const std::string left = fmt::format("{}Left", prefix);
		const std::string right = fmt::format("{}Right", prefix);
		const std::string select_left = fmt::format("{}Button1", prefix);
		const std::string select_right = fmt::format("{}Button2", prefix);

		add_binding(layout, 0, 1, "Up", up.c_str());
		add_binding(layout, 1, 0, "Left", left.c_str());
		QLabel* pad_label = new QLabel(tr("Pad"), group);
		pad_label->setMinimumSize(120, 88);
		pad_label->setFrameShape(QFrame::StyledPanel);
		pad_label->setFrameShadow(QFrame::Sunken);
		pad_label->setAlignment(Qt::AlignCenter);
		layout->addWidget(pad_label, 1, 1);
		add_binding(layout, 1, 2, "Right", right.c_str());
		add_binding(layout, 2, 1, "Down", down.c_str());
		add_binding(layout, 3, 0, "Select Left", select_left.c_str());
		add_binding(layout, 3, 1, "Start", start.c_str());
		add_binding(layout, 3, 2, "Select Right", select_right.c_str());

		return group;
	};

	QGridLayout* layout = new QGridLayout(scrollarea_widget);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setHorizontalSpacing(12);
	layout->setVerticalSpacing(12);

	QHBoxLayout* cabinet_row = new QHBoxLayout();
	cabinet_row->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));
	QGroupBox* cabinet = new QGroupBox(tr("Cabinet Controls"), scrollarea_widget);
	QGridLayout* cabinet_layout = new QGridLayout(cabinet);
	add_binding(cabinet_layout, 0, 0, "Test Button", "Test", 220);
	add_binding(cabinet_layout, 0, 1, "Service Button", "Service", 220);
	add_binding(cabinet_layout, 1, 0, "Coin Player 1", "Coin1", 220);
	add_binding(cabinet_layout, 1, 1, "Coin Player 2", "Coin2", 220);
	cabinet_row->addWidget(cabinet);
	cabinet_row->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));
	layout->addLayout(cabinet_row, 0, 0, 1, 2);

	layout->addWidget(create_player_controls(scrollarea_widget, tr("JVS Controls Player 1"), "P1"), 1, 0);
	layout->addWidget(create_player_controls(scrollarea_widget, tr("JVS Controls Player 2"), "P2"), 1, 1);

	QGroupBox* popn = new QGroupBox(tr("Pop'n Controls"), scrollarea_widget);
	QGridLayout* popn_layout = new QGridLayout(popn);
	popn_layout->setHorizontalSpacing(8);
	popn_layout->setVerticalSpacing(6);
	add_binding(popn_layout, 0, 0, "Start", "P1Start", 130);
	add_binding(popn_layout, 0, 1, "Yellow Left", "P1Down", 130);
	add_binding(popn_layout, 0, 2, "Blue Left", "P1Right", 130);
	add_binding(popn_layout, 0, 3, "Blue Right", "P2Up", 130);
	add_binding(popn_layout, 0, 4, "Yellow Right", "P2Left", 130);
	add_binding(popn_layout, 1, 0, "White Left", "P1Up", 130);
	add_binding(popn_layout, 1, 1, "Green Left", "P1Left", 130);
	add_binding(popn_layout, 1, 2, "Red", "P1Button1", 130);
	add_binding(popn_layout, 1, 3, "Green Right", "P2Down", 130);
	add_binding(popn_layout, 1, 4, "White Right", "P2Right", 130);
	QHBoxLayout* popn_row = new QHBoxLayout();
	popn_row->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));
	popn_row->addWidget(popn);
	popn_row->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));
	layout->addLayout(popn_row, 2, 0, 1, 2);

	layout->addWidget(create_ddr_controls(scrollarea_widget, tr("DDR Controls Player 1"), "P1"), 3, 0);
	layout->addWidget(create_ddr_controls(scrollarea_widget, tr("DDR Controls Player 2"), "P2"), 3, 1);
	layout->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding), 4, 0);

	QHBoxLayout* main_layout = new QHBoxLayout(this);
	main_layout->setContentsMargins(0, 0, 0, 0);
	main_layout->addWidget(scrollarea);
}

#include "moc_FireWireDeviceWidget.cpp"
