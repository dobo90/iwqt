#include "tray.hpp"

#include "agent.hpp"
#include "iwd.hpp"

#include "device.hpp"

#include "utils.hpp"

#include <QWidgetAction>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QMessageBox>

#include <QWidget>
#include <QIcon>
#include <QInputDialog>
#include <QThread>
#include <QToolTip>

#include <cstdio>
#include <unistd.h>

constexpr auto ICON_THEME_AUTO = "auto";
constexpr auto ICON_THEME_DARK = "dark";
constexpr auto ICON_THEME_LIGHT = "light";
constexpr auto ICON_THEME_SYSTEM = "system";

Tray::Tray(iwd &in): manager(in) {
    updateIconTheme();

    createTray();

    createItems();
    instantiateDevice();

    fillMenu();
    createManageWindow();

    connect(trayIcon, &QSystemTrayIcon::activated, this, &Tray::iconActivated);

    refreshTray(true);

    makeAgent();
}

void Tray::createTray() {
    trayIcon = new QSystemTrayIcon(this);
    switch (iconTheme) {
        case IconTheme::System:
            trayIcon->setIcon(QIcon::fromTheme("network-wireless-disconnected"));
            break;
        case IconTheme::Dark:
            trayIcon->setIcon(Utils::getIcon(FAILURE_ICON_PATH));
            break;
        case IconTheme::Light:
            trayIcon->setIcon(Utils::getIcon(DARK_FAILURE_ICON_PATH));
            break;
    }
}

void Tray::iconActivated(QSystemTrayIcon::ActivationReason reason){
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        if(mwindow->isVisible()){
            mwindow->hide();
        } else{
            mwindow->show();
        }
        break;
    default:
        break;
    }
}

void Tray::instantiateDevice() {
    try{
        this->cur_adapter = manager.get_first_adapter().value();
    } catch(...){
        Utils::adapterNotFound(this);
        exit(0);
    }
        
    updateEnabledTray(cur_adapter.get_powered());

    try {
        this->cur_device = this->cur_adapter.get_first_device().value();
    } catch(...) {
        trayIcon->show();

        Utils::deviceNotFound(this);

        for(;;) {
            try {
                this->cur_device = this->cur_adapter.get_first_device().value();
                break;
            } catch(...) {
                QThread::sleep(2);
            }
        }
    }
}

std::string Tray::requestPassphrase(const std::string& path) {
    std::string out;
    bool ok = false;

    QMetaObject::invokeMethod(this, [this, &path, &out, &ok]() {
        QString title = tr("Credentials for:\n%1")
                        .arg(QString::fromStdString(path));

        QString pw = QInputDialog::getText(
                         this,
                         tr("Wi‑Fi Passphrase"),
                         title,
                         QLineEdit::Password,
                         QString(),
                         &ok
                     );
        if(!ok) {
            return;
        }
        out = pw.toStdString();
    },
    Qt::BlockingQueuedConnection
                             );

    return out;
}

std::tuple<std::string,std::string> Tray::requestUserAndPassphrase(const std::string& path) {
    std::string user, pass;
    bool ok;

    QMetaObject::invokeMethod(
        this,
    [&]() {
        QString title = tr("Credentials for:\n%1")
                        .arg(QString::fromStdString(path));
        QString u = QInputDialog::getText(
                        this,
                        title,
                        tr("Username:"),
                        QLineEdit::Normal,
                        QString(),
                        &ok
                    );
        if(!ok) {
            return;
        }

        user = u.toStdString();
    },
    Qt::BlockingQueuedConnection
    );

    if(!ok || user.empty()) {
        return { "canceled", "canceled" };
    }

    QMetaObject::invokeMethod(
        this,
    [&]() {
        QString title = tr("Credentials for:\n%1")
                        .arg(QString::fromStdString(path));
        QString p = QInputDialog::getText(
                        this,
                        title,
                        tr("Password for %1:").arg(QString::fromStdString(user)),
                        QLineEdit::Password,
                        QString(),
                        &ok
                    );
        if(!ok) {
            return;
        }

        pass = p.toStdString();
    },
    Qt::BlockingQueuedConnection
    );

    if(!ok || pass.empty()) {
        return { "canceled", "canceled" };
    }

    return { user, pass };
}

void Tray::makeAgent() {
    agent_ui ui;

    ui.request_password = [this](const std::string& path) -> std::string {
        return this->requestPassphrase(path);
    };

    ui.request_user_and_password = [this](const std::string& path) -> std::tuple<std::string,std::string> {
        return this->requestUserAndPassphrase(path);
    };

    this->manager.register_agent(std::move(ui));
}

void Tray::updateIconTheme() {
    auto setting = settings.value(ICON_THEME_SETTING, ICON_THEME_AUTO).toString();

    if(setting == ICON_THEME_SYSTEM) {
        iconTheme = IconTheme::System;
        return;
    }

    if(setting == ICON_THEME_DARK) {
        iconTheme = IconTheme::Dark;
        return;
    }

    if(setting == ICON_THEME_LIGHT) {
        iconTheme = IconTheme::Light;
        return;
    }

    iconTheme =
        this->palette().window().color().value() < this->palette().windowText().color().value()
        ? IconTheme::Dark : IconTheme::Light;
}

void Tray::connectedHandler(network n, QIcon icon){
    auto connected = this->cur_device.get_connected_network().has_value();

    if(connected){
        QMetaObject::invokeMethod(this, [this, n, icon](){
            trayIcon->setIcon(icon);

            if(settings.value(SHOW_NOTIFICATIONS_SETTING, true).toBool()){
                trayIcon->showMessage(
                    tr("Connected to %1").arg(n.name),
                    tr("Type: %1").arg(n.type),
                    QSystemTrayIcon::Information,
                    3000
                );
            }
        }, Qt::QueuedConnection);
        return;
    }

    if(n.type == "8021x"){
        QMetaObject::invokeMethod(this, [=, this]() {
            Utils::networkConfigure(n.type, this);
        });
    }
}

QString Tray::iconNameForStrength(network::strength_type st) const {
    switch (st) {
        case network::strength_type::EXCELLENT: return "network-wireless-100";
        case network::strength_type::GOOD:      return "network-wireless-80";
        case network::strength_type::FAIR:      return "network-wireless-60";
        case network::strength_type::WEAK:      return "network-wireless-40";
        case network::strength_type::POOR:      return "network-wireless-20";
    }
    return "network-wireless-0";
}

QIcon Tray::iconForStrength(network::strength_type st) {
    if (iconTheme == IconTheme::System) {
        return QIcon::fromTheme(iconNameForStrength(st));
    }

    bool isDarkMode = iconTheme == IconTheme::Dark;
    QPixmap pixmap;
    switch (st) {
        case network::strength_type::EXCELLENT:
            pixmap = Utils::getIcon(isDarkMode ? EXCELLENT_ICON_PATH : DARK_EXCELLENT_ICON_PATH); break;
        case network::strength_type::GOOD:
            pixmap = Utils::getIcon(isDarkMode ? GOOD_ICON_PATH : DARK_GOOD_ICON_PATH); break;
        case network::strength_type::FAIR:
            pixmap = Utils::getIcon(isDarkMode ? FAIR_ICON_PATH : DARK_FAIR_ICON_PATH); break;
        case network::strength_type::WEAK:
            pixmap = Utils::getIcon(isDarkMode ? WEAK_ICON_PATH : DARK_WEAK_ICON_PATH); break;
        case network::strength_type::POOR:
            pixmap = Utils::getIcon(isDarkMode ? POOR_ICON_PATH : DARK_POOR_ICON_PATH); break;
    }
    return QIcon(pixmap);
}

void Tray::addNetwork(network n) {
    auto action = networksMenu->addAction(n.name.c_str());

    QIcon icon = iconForStrength(n.strength());

    action->setIcon(icon);

    if (n.connected) {
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, this, [=] {
            action->setChecked(true);
        });
        trayIconMenu->setIcon(icon);
        return;
    }

    connect(action, &QAction::triggered, this, [=, this] {
        saved_proxy = this->cur_device.connect(n, [=, this](std::optional<sdbus::Error> e){
            connectedHandler(n, icon); 
        });
        //needs to be saved so the callback is invoked later on
    });
}

void Tray::processConnectedNetwork(network n) {
    addNetwork(n);

    QAction* disconnectAction = new QAction(tr("&Disconnect"), this);

    connect(disconnectAction, &QAction::triggered, this, [this, n] {
        this->cur_device.disconnect();

        switch (iconTheme) {
            case IconTheme::System:
                trayIcon->setIcon(QIcon::fromTheme("network-wireless-off"));
                break;
            case IconTheme::Dark:
                trayIcon->setIcon(QIcon(Utils::getIcon(DISCONNECTED_ICON_PATH)));
                break;
            case IconTheme::Light:
                trayIcon->setIcon(QIcon(Utils::getIcon(DARK_DISCONNECTED_ICON_PATH)));
                break;
        }

        if(settings.value(SHOW_NOTIFICATIONS_SETTING, true).toBool()){
            trayIcon->showMessage(
                tr("Disconnected from %1").arg(n.name),
                "",
                QSystemTrayIcon::Information,
                3000
            );
        }
    });

    networksMenu->addAction(disconnectAction);

    QAction* availableLabel = new QAction(tr("&Available"), this);
    availableLabel->setEnabled(false);
    networksMenu->addAction(availableLabel);
}

void Tray::updateEnabledTray(bool powered){
    enabledAdapterAction->setChecked(powered);

    networksMenu->setEnabled(powered);
    scanAction->setEnabled(powered);

    switch (iconTheme) {
        case IconTheme::System:
            trayIcon->setIcon(QIcon::fromTheme(powered ? "network-wireless-off" : "network-wireless-disconnected"));
            break;
        case IconTheme::Dark:
            trayIcon->setIcon(QIcon(Utils::getIcon(powered ? DISCONNECTED_ICON_PATH : FAILURE_ICON_PATH)));
            break;
        case IconTheme::Light:
            trayIcon->setIcon(QIcon(Utils::getIcon(powered ? DARK_DISCONNECTED_ICON_PATH : DARK_FAILURE_ICON_PATH)));
            break;
    }
}

void Tray::refreshTray(bool should_scan) {
    networksMenu->clear();
    
    bool powered = cur_adapter.get_powered();

    updateEnabledTray(powered);

    if(!powered){
        return;
    }

    if(should_scan) {
        this->cur_device.scan();
    }

    auto inetworks = this->cur_device.get_networks();

    std::sort(inetworks.begin(), inetworks.end(), [](network &a, network &b){
        return a.signal > b.signal;
    });

    auto size = inetworks.size();

    for(size_t i = 0; i < size; ++i) {
        auto network = inetworks[i];

        if(!network.connected) {
            continue;
        }

        processConnectedNetwork(network);
        trayIcon->setIcon(iconForStrength(network.strength()));

        inetworks.erase(inetworks.begin() + i);

        break;
    }

    if(size == inetworks.size()){
        switch (iconTheme) {
            case IconTheme::System:
                trayIcon->setIcon(QIcon::fromTheme("network-wireless-off"));
                break;
            case IconTheme::Dark:
                trayIcon->setIcon(QIcon(Utils::getIcon(DISCONNECTED_ICON_PATH)));
                break;
            case IconTheme::Light:
                trayIcon->setIcon(QIcon(Utils::getIcon(DARK_DISCONNECTED_ICON_PATH)));
                break;
        }
    }

    for(auto n: inetworks) {
        addNetwork(n);
    }
}

void Tray::setVisible(bool visible) {
    QDialog::setVisible(false);
}

void Tray::createItems() {
    enabledAdapterAction = new QAction(tr("&Enabled"), this);
    enabledAdapterAction->setCheckable(true);

    connect(enabledAdapterAction, &QAction::triggered, this, [this]{
        auto checked = enabledAdapterAction->isChecked();
        cur_adapter.set_powered(checked);
        updateEnabledTray(checked);
    });

    networksMenu = new QMenu(tr("&Networks"), this);
    connect(networksMenu, &QMenu::aboutToShow, this, [this] {
        try {
            this->refreshTray(!settings.value(AVOID_SCANS_SETTING, false).toBool());
        } catch(...) {
            instantiateDevice(); //try to recover
        }
    });

    manageAction = new QAction(tr("&Manage"), this);
    connect(manageAction, &QAction::triggered, this, [this]() {
        mwindow->show();        
    });

    scanAction = new QAction(tr("&Scan"), this);
    connect(scanAction, &QAction::triggered, this, [this]() {
        try {
            this->refreshTray(true);
        } catch(...) {
            instantiateDevice();
        }
    });


    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, this, &QCoreApplication::quit);
}

void Tray::fillMenu() {
    trayIconMenu = new QMenu(this);

    trayIconMenu->addAction(enabledAdapterAction);

    trayIconMenu->addMenu(networksMenu);
    trayIconMenu->addAction(scanAction);
    trayIconMenu->addMenu(createIconThemeMenu());

    trayIconMenu->addSeparator();
    trayIconMenu->addAction(manageAction);
    trayIconMenu->addAction(quitAction);

    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->show();
}

QMenu *Tray::createIconThemeMenu() {
    auto menu = new QMenu(tr("&Icon Theme"), this);
    auto group = new QActionGroup(menu);
    group->setExclusive(true);

    auto currentTheme = settings.value(ICON_THEME_SETTING, ICON_THEME_AUTO).toString();
    if(currentTheme != ICON_THEME_DARK && currentTheme != ICON_THEME_LIGHT && currentTheme != ICON_THEME_SYSTEM) {
        currentTheme = ICON_THEME_AUTO;
    }

    auto addThemeAction = [this, menu, group, currentTheme](const QString &label, const char *value) {
        auto action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(currentTheme == value);
        group->addAction(action);

        connect(action, &QAction::triggered, this, [this, value] {
            settings.setValue(ICON_THEME_SETTING, value);
            updateIconTheme();

            try {
                refreshTray(false);
            } catch(...) {
                instantiateDevice();
            }
        });
    };

    addThemeAction(tr("&Auto"), ICON_THEME_AUTO);
    addThemeAction(tr("&Dark Panel"), ICON_THEME_DARK);
    addThemeAction(tr("&Light Panel"), ICON_THEME_LIGHT);
    addThemeAction(tr("&System Theme"), ICON_THEME_SYSTEM);

    return menu;
}

void Tray::createManageWindow(){
    mwindow = new ManageWindow(manager, this);
}
