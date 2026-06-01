#ifndef TRAY_HPP
#define TRAY_HPP

#include "iwd.hpp"

#include "adapter.hpp"
#include "device.hpp"

#include "manage_window.hpp"

#include <QSystemTrayIcon>
#include <QIcon>

#include <QDialog>
#include <sdbus-c++/IProxy.h>

QT_BEGIN_NAMESPACE
class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QEventLoop;
QT_END_NAMESPACE

class Tray : public QDialog {
    Q_OBJECT

  public:
    Tray(iwd &manager);

    enum class IconTheme {
        Light,
        Dark,
        System
    };

    void setVisible(bool visible) override;

  private slots:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);

  private:
    iwd &manager;

    IconTheme iconTheme = IconTheme::Light;

    adapter cur_adapter;
    device cur_device;

    void createTray();
    void instantiateDevice();
    void addNetwork(network n);

    void processConnectedNetwork(network n);
    void updateEnabledTray(bool);
    void refreshTray(bool);
    void makeAgent();
    void updateIconTheme();

    std::string requestPassphrase(const std::string& path);
    std::tuple<std::string,std::string> requestUserAndPassphrase(const std::string& path);

    void createItems();
    void fillMenu();
    QMenu *createIconThemeMenu();
    void createManageWindow();

    void connectedHandler(network n, QIcon icon);

    QIcon iconForStrength(network::strength_type st);
    QString iconNameForStrength(network::strength_type st) const;

    std::unique_ptr<sdbus::IProxy> saved_proxy;
    QMenu *networksMenu = NULL;

    QAction *enabledAdapterAction;
    QAction *scanAction;
    QAction *manageAction;
    QAction *quitAction;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;

    ManageWindow *mwindow;

    QSettings settings;
};

#endif
