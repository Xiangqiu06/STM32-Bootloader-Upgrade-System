#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "upgradesession.h"

#include <QByteArray>
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class Transport;

/* 目标分区描述 */
struct TargetInfo {
    const char *title;
    quint32     startAddr;
    quint32     size;
    Target      target;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onModeChanged(int index);
    void onRefreshPorts();
    void onConnectClicked();
    void onBrowseFile();
    void onTargetChanged();
    void onStartClicked();
    void onAbortClicked();
    void onSessionLog(const QString &line);
    void onSessionProgress(qint64 sent, qint64 total);
    void onSessionFinished(bool ok, const QString &message);
    void onTransportError(const QString &msg);

private:
    QWidget *buildTransportGroup();
    QWidget *buildTargetGroup();
    QWidget *buildFileGroup();
    QWidget *buildActionGroup();
    QWidget *buildLogGroup();

    void    setupUi();
    void    refreshCanInterfaces();
    void    log(const QString &line);
    void    setUiBusy(bool busy);
    QString currentPortName() const;
    int     currentBaudRate() const;
    TargetInfo currentTarget() const;
    int     currentTargetIndex() const;
    bool    loadFirmware(const QString &path);
    void    updateTargetInfo();

    /* 链路参数 */
    QComboBox   *m_modeCombo = nullptr;
    QStackedWidget *m_modeStack = nullptr;

    QComboBox   *m_uartPort = nullptr;
    QComboBox   *m_uartBaud = nullptr;
    QPushButton *m_refreshUartBtn = nullptr;

    QComboBox   *m_canIface = nullptr;
    QLineEdit   *m_canTxId = nullptr;
    QLineEdit   *m_canRxId = nullptr;
    QComboBox   *m_canBaud = nullptr;

    QComboBox   *m_loraPort = nullptr;
    QComboBox   *m_loraBaud = nullptr;
    QSpinBox    *m_loraChunk = nullptr;

    /* 目标分区 */
    QRadioButton *m_rbApp = nullptr;
    QRadioButton *m_rbReset = nullptr;
    QRadioButton *m_rbBoot = nullptr;
    QLabel       *m_targetAddrLabel = nullptr;
    QLabel       *m_targetSizeLabel = nullptr;

    /* 固件 */
    QLineEdit   *m_filePath = nullptr;
    QLabel      *m_fileInfo = nullptr;
    QLineEdit   *m_versionEdit = nullptr;
    QSpinBox    *m_chunkSpin = nullptr;
    QSpinBox    *m_retrySpin = nullptr;

    /* 动作与状态 */
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_abortBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;

    Transport      *m_transport = nullptr;
    UpgradeSession *m_session = nullptr;
    QByteArray      m_image;
    QString         m_imagePath;
    quint32         m_imageBase = 0;   /* hex 解析出的实际基地址（bin 时 = 所选分区起始） */
};

#endif // MAINWINDOW_H
