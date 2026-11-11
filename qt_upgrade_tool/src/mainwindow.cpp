#include "mainwindow.h"

#include "hexloader.h"
#include "protocol.h"
#include "transport_can.h"
#include "transport_net.h"
#include "transport_serial.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {

/* 分区表：与固件 common/fw_config.h 保持一致，改固件分区时这里同步改 */
const TargetInfo kTargets[] = {
    { "应用程序 APP",        0x0800C000u, 96u * 1024u, Target::App   },
    { "恢复出厂 RESET",      0x08006000u, 24u * 1024u, Target::Reset },
    { "引导程序 BOOT",       0x08000000u, 24u * 1024u, Target::Boot  },
};

QString addrText(quint32 addr)
{
    return QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')).toUpper().replace(QStringLiteral("0X"),
                                                                               QStringLiteral("0x"));
}

/* 扫描 /sys/class/net 中 type=280 的接口（ARPHRD_CAN） */
QStringList canInterfaces()
{
    QStringList out;
    QDir net(QStringLiteral("/sys/class/net"));
    if (!net.exists()) {
        return out;
    }
    const QStringList names = net.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        QFile f(net.filePath(name + QStringLiteral("/type")));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const int type = f.readAll().trimmed().toInt();
        if (type == 280) {
            out << name;
        }
        f.close();
    }
    if (out.isEmpty()) {
        out << QStringLiteral("can0");
    }
    return out;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();

    m_session = new UpgradeSession(this);
    connect(m_session, &UpgradeSession::log, this, &MainWindow::onSessionLog);
    connect(m_session, &UpgradeSession::progress, this, &MainWindow::onSessionProgress);
    connect(m_session, &UpgradeSession::finished, this, &MainWindow::onSessionFinished);

    onRefreshPorts();
    refreshCanInterfaces();
    onTargetChanged();
    setUiBusy(false);

    log(QStringLiteral("就绪：选择链路 → 连接 → 选择目标分区与固件 → 开始升级"));
}

MainWindow::~MainWindow()
{
    if (m_transport) {
        m_transport->close();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_session && m_session->isRunning()) {
        m_session->abort(QStringLiteral("窗口关闭"));
    }
    if (m_transport) {
        m_transport->close();
    }
    event->accept();
}

/* ------------------------------- 界面搭建 ------------------------------- */

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("STM32 固件升级工具  ·  UART / CAN / LoRa"));
    resize(1060, 720);

    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(buildTransportGroup());
    left->addWidget(buildTargetGroup());
    left->addWidget(buildFileGroup());
    left->addWidget(buildActionGroup());
    left->addStretch(1);

    auto *leftWrap = new QWidget;
    leftWrap->setLayout(left);
    leftWrap->setFixedWidth(430);

    root->addWidget(leftWrap);
    root->addWidget(buildLogGroup(), 1);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("未连接"));
}

QWidget *MainWindow::buildTransportGroup()
{
    auto *box = new QGroupBox(QStringLiteral("1. 升级方式"), this);
    auto *v = new QVBoxLayout(box);

    m_modeCombo = new QComboBox(box);
    m_modeCombo->addItem(QStringLiteral("串口 UART"));
    m_modeCombo->addItem(QStringLiteral("CAN 总线"));
    m_modeCombo->addItem(QStringLiteral("LoRa 无线"));
    m_modeCombo->addItem(QStringLiteral("网络 NET（预留）"));
    v->addWidget(m_modeCombo);

    m_modeStack = new QStackedWidget(box);

    /* ---- 串口页 ---- */
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);

        auto *row = new QWidget(page);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        m_uartPort = new QComboBox(row);
        m_uartPort->setMinimumWidth(150);
        m_refreshUartBtn = new QPushButton(QStringLiteral("刷新"), row);
        m_refreshUartBtn->setFixedWidth(56);
        h->addWidget(m_uartPort, 1);
        h->addWidget(m_refreshUartBtn);
        form->addRow(QStringLiteral("串口"), row);

        m_uartBaud = new QComboBox(page);
        const QList<qint32> bauds = QSerialPortInfo::standardBaudRates();
        for (qint32 b : bauds) {
            if (b >= 9600 && b <= 921600) {
                m_uartBaud->addItem(QString::number(b), b);
            }
        }
        m_uartBaud->setCurrentText(QStringLiteral("115200"));
        form->addRow(QStringLiteral("波特率"), m_uartBaud);
        m_modeStack->addWidget(page);

        connect(m_refreshUartBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    }

    /* ---- CAN 页 ---- */
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);

        m_canIface = new QComboBox(page);
        m_canIface->setEditable(true);
        form->addRow(QStringLiteral("接口"), m_canIface);

        m_canBaud = new QComboBox(page);
        m_canBaud->addItem(QStringLiteral("125 kbit/s"), 125000);
        m_canBaud->addItem(QStringLiteral("250 kbit/s"), 250000);
        m_canBaud->addItem(QStringLiteral("500 kbit/s"), 500000);
        m_canBaud->addItem(QStringLiteral("1 Mbit/s"), 1000000);
        m_canBaud->setCurrentIndex(2);
        form->addRow(QStringLiteral("波特率(参考)"), m_canBaud);

        auto *idRow = new QWidget(page);
        auto *ih = new QHBoxLayout(idRow);
        ih->setContentsMargins(0, 0, 0, 0);
        m_canTxId = new QLineEdit(QStringLiteral("123"), idRow);
        m_canRxId = new QLineEdit(QStringLiteral("124"), idRow);
        ih->addWidget(new QLabel(QStringLiteral("TX")));
        ih->addWidget(m_canTxId);
        ih->addWidget(new QLabel(QStringLiteral("RX")));
        ih->addWidget(m_canRxId);
        form->addRow(QStringLiteral("帧 ID(十六进制)"), idRow);

        auto *hint = new QLabel(QStringLiteral("需先用 root 执行：\n"
                                               "sudo ip link set can0 up type can bitrate 500000"),
                                page);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#666; font-size:11px;"));
        form->addRow(hint);

        m_modeStack->addWidget(page);
    }

    /* ---- LoRa 页 ---- */
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);

        auto *row = new QWidget(page);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        m_loraPort = new QComboBox(row);
        m_loraPort->setMinimumWidth(150);
        auto *refresh = new QPushButton(QStringLiteral("刷新"), row);
        refresh->setFixedWidth(56);
        h->addWidget(m_loraPort, 1);
        h->addWidget(refresh);
        form->addRow(QStringLiteral("串口(接 LoRa)"), row);

        m_loraBaud = new QComboBox(page);
        m_loraBaud->addItems({ QStringLiteral("9600"), QStringLiteral("19200"),
                               QStringLiteral("38400"), QStringLiteral("57600"),
                               QStringLiteral("115200") });
        m_loraBaud->setCurrentText(QStringLiteral("9600"));
        form->addRow(QStringLiteral("波特率"), m_loraBaud);

        m_loraChunk = new QSpinBox(page);
        m_loraChunk->setRange(32, 512);
        m_loraChunk->setValue(128);
        m_loraChunk->setSuffix(QStringLiteral(" 字节/包"));
        form->addRow(QStringLiteral("分片长度"), m_loraChunk);

        auto *hint = new QLabel(QStringLiteral("LoRa 模块按透传模式使用：空中速率决定实际耗时，"
                                               "建议分片 128 字节以内并降低波特率"),
                                page);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#666; font-size:11px;"));
        form->addRow(hint);

        m_modeStack->addWidget(page);
        connect(refresh, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    }

    /* ---- 网络页（预留） ---- */
    {
        auto *page = new QWidget;
        auto *v2 = new QVBoxLayout(page);
        v2->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(QStringLiteral("网络升级通道预留。\n\n"
                                                "接入 WiFi 模块后，在此实现 TCP / MQTT 客户端：\n"
                                                "帧格式与 UART 完全一致，仅传输层不同。"),
                                 page);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#777;"));
        v2->addWidget(label);
        m_modeStack->addWidget(page);
    }

    v->addWidget(m_modeStack);

    connect(m_modeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);

    return box;
}

QWidget *MainWindow::buildTargetGroup()
{
    auto *box = new QGroupBox(QStringLiteral("2. 升级目标分区"), this);
    auto *v = new QVBoxLayout(box);

    m_rbApp   = new QRadioButton(QStringLiteral("应用程序 APP（运行区）"), box);
    m_rbReset = new QRadioButton(QStringLiteral("恢复出厂 RESET"), box);
    m_rbBoot  = new QRadioButton(QStringLiteral("引导程序 BOOT"), box);
    m_rbApp->setChecked(true);

    v->addWidget(m_rbApp);
    v->addWidget(m_rbReset);
    v->addWidget(m_rbBoot);

    m_targetAddrLabel = new QLabel(box);
    m_targetAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_targetSizeLabel = new QLabel(box);
    m_targetSizeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_targetAddrLabel->setStyleSheet(QStringLiteral("font-family:monospace; color:#1a5fb4;"));
    m_targetSizeLabel->setStyleSheet(QStringLiteral("font-family:monospace; color:#555;"));
    v->addWidget(m_targetAddrLabel);
    v->addWidget(m_targetSizeLabel);

    connect(m_rbApp,   &QRadioButton::toggled, this, &MainWindow::onTargetChanged);
    connect(m_rbReset, &QRadioButton::toggled, this, &MainWindow::onTargetChanged);
    connect(m_rbBoot,  &QRadioButton::toggled, this, &MainWindow::onTargetChanged);

    return box;
}

QWidget *MainWindow::buildFileGroup()
{
    auto *box = new QGroupBox(QStringLiteral("3. 固件文件"), this);
    auto *form = new QFormLayout(box);

    auto *row = new QWidget(box);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    m_filePath = new QLineEdit(row);
    m_filePath->setReadOnly(true);
    m_filePath->setPlaceholderText(QStringLiteral("选择 .bin 固件"));
    auto *browse = new QPushButton(QStringLiteral("浏览"), row);
    browse->setFixedWidth(56);
    h->addWidget(m_filePath, 1);
    h->addWidget(browse);
    form->addRow(QStringLiteral("文件"), row);

    m_fileInfo = new QLabel(box);
    m_fileInfo->setWordWrap(true);
    form->addRow(m_fileInfo);

    m_versionEdit = new QLineEdit(QStringLiteral("1.1"), box);
    m_versionEdit->setToolTip(QStringLiteral("写入设备参数的版本号，格式 主.次"));
    form->addRow(QStringLiteral("版本号"), m_versionEdit);

    m_chunkSpin = new QSpinBox(box);
    m_chunkSpin->setRange(64, 1024);
    m_chunkSpin->setValue(1024);
    m_chunkSpin->setSuffix(QStringLiteral(" 字节/包"));
    form->addRow(QStringLiteral("分片长度"), m_chunkSpin);

    m_retrySpin = new QSpinBox(box);
    m_retrySpin->setRange(1, 20);
    m_retrySpin->setValue(5);
    m_retrySpin->setSuffix(QStringLiteral(" 次"));
    form->addRow(QStringLiteral("失败重传"), m_retrySpin);

    connect(browse, &QPushButton::clicked, this, &MainWindow::onBrowseFile);
    return box;
}

QWidget *MainWindow::buildActionGroup()
{
    auto *box = new QGroupBox(QStringLiteral("4. 操作"), this);
    auto *v = new QVBoxLayout(box);

    auto *row = new QWidget(box);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    m_connectBtn = new QPushButton(QStringLiteral("连接设备"), row);
    m_startBtn   = new QPushButton(QStringLiteral("开始升级"), row);
    m_abortBtn   = new QPushButton(QStringLiteral("中止"), row);
    m_startBtn->setDefault(true);
    h->addWidget(m_connectBtn);
    h->addWidget(m_startBtn, 1);
    h->addWidget(m_abortBtn);
    v->addWidget(row);

    m_progress = new QProgressBar(box);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("%p%"));
    v->addWidget(m_progress);

    m_statusLabel = new QLabel(QStringLiteral("未连接"), box);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666;"));
    v->addWidget(m_statusLabel);

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_startBtn,   &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_abortBtn,   &QPushButton::clicked, this, &MainWindow::onAbortClicked);

    return box;
}

QWidget *MainWindow::buildLogGroup()
{
    auto *box = new QGroupBox(QStringLiteral("日志"), this);
    auto *v = new QVBoxLayout(box);

    m_log = new QPlainTextEdit(box);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(10);
    m_log->setFont(f);
    v->addWidget(m_log);

    auto *clear = new QPushButton(QStringLiteral("清空日志"), box);
    clear->setFixedWidth(90);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(clear);
    v->addLayout(row);
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);

    return box;
}

/* ------------------------------- 业务逻辑 ------------------------------- */

void MainWindow::onModeChanged(int index)
{
    m_modeStack->setCurrentIndex(index);

    const bool net = (index == 3);
    m_connectBtn->setEnabled(!net);

    if (m_transport && m_transport->isOpen()) {
        m_session->setTransport(nullptr);      /* 先断开引用，再释放对象 */
        m_transport->close();
        delete m_transport;
        m_transport = nullptr;
        m_connectBtn->setText(QStringLiteral("连接设备"));
        statusBar()->showMessage(QStringLiteral("未连接"));
        m_statusLabel->setText(QStringLiteral("未连接"));
    }

    if (net) {
        log(QStringLiteral("网络升级为预留通道，暂不可用"));
    }
}

void MainWindow::onRefreshPorts()
{
    const QString keepUart = m_uartPort->currentText();
    const QString keepLora = m_loraPort->currentText();

    m_uartPort->clear();
    m_loraPort->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString text = QStringLiteral("%1  (%2)").arg(info.portName(), info.description());
        m_uartPort->addItem(text, info.portName());
        m_loraPort->addItem(text, info.portName());
    }

    if (m_uartPort->count() == 0) {
        m_uartPort->addItem(QStringLiteral("未发现串口"), QString());
        m_loraPort->addItem(QStringLiteral("未发现串口"), QString());
        return;
    }

    const int iu = m_uartPort->findText(keepUart);
    if (iu >= 0) {
        m_uartPort->setCurrentIndex(iu);
    }
    const int il = m_loraPort->findText(keepLora);
    if (il >= 0) {
        m_loraPort->setCurrentIndex(il);
    }

    log(QStringLiteral("发现 %1 个串口").arg(ports.size()));
}

void MainWindow::refreshCanInterfaces()
{
    const QString keep = m_canIface->currentText();
    m_canIface->clear();
    m_canIface->addItems(canInterfaces());
    if (!keep.isEmpty()) {
        m_canIface->setCurrentText(keep);
    }
}

void MainWindow::onConnectClicked()
{
    if (m_transport && m_transport->isOpen()) {
        m_session->setTransport(nullptr);
        m_transport->close();
        delete m_transport;
        m_transport = nullptr;
        m_connectBtn->setText(QStringLiteral("连接设备"));
        m_statusLabel->setText(QStringLiteral("未连接"));
        statusBar()->showMessage(QStringLiteral("未连接"));
        log(QStringLiteral("已断开"));
        return;
    }

    const int mode = m_modeCombo->currentIndex();
    Transport *t = nullptr;

    if (mode == 0) {
        auto *s = new SerialTransport(this);
        s->setPortName(currentPortName());
        s->setBaudRate(currentBaudRate());
        t = s;
    } else if (mode == 1) {
        auto *c = new CanTransport(this);
        c->setInterface(m_canIface->currentText());
        bool ok1 = false, ok2 = false;
        const quint32 tx = m_canTxId->text().toUInt(&ok1, 16);
        const quint32 rx = m_canRxId->text().toUInt(&ok2, 16);
        c->setTxId(ok1 ? tx : 0x123);
        c->setRxId(ok2 ? rx : 0x124);
        t = c;
    } else if (mode == 2) {
        auto *s = new SerialTransport(this);
        s->setPortName(m_loraPort->currentData().toString());
        s->setBaudRate(m_loraBaud->currentText().toInt());
        t = s;
    } else {
        auto *n = new NetTransport(this);
        t = n;
    }

    QString err;
    if (!t->open(&err)) {
        log(QStringLiteral("连接失败：%1").arg(err));
        m_statusLabel->setText(QStringLiteral("连接失败"));
        delete t;
        return;
    }

    m_transport = t;
    m_session->setTransport(t);
    connect(t, &Transport::errorOccurred, this, &MainWindow::onTransportError);

    /* 原始接收日志：显示收到的原始字节（十六进制+ASCII）。
     * 会话运行期间也保持记录——设备只会回 ACK 和少量 ASCII 日志，
     * 数据量大的是上行方向，不会刷屏。 */
    connect(t, &Transport::bytesReceived, this, [this](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        QString hex;
        QString ascii;
        const int n = qMin<int>(data.size(), 24);
        for (int i = 0; i < n; ++i) {
            const quint8 b = static_cast<quint8>(data.at(i));
            hex += QString::number(b, 16).rightJustified(2, QLatin1Char('0')) + QLatin1Char(' ');
            ascii += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar::fromLatin1('.');
        }
        const QString more = (data.size() > 24) ? QStringLiteral("…") : QString();
        log(QStringLiteral("← 收到 %1 字节: %2%3 | %4").arg(data.size()).arg(hex, more, ascii));
    });

    log(QStringLiteral("已连接 %1").arg(t->description()));
    statusBar()->showMessage(QStringLiteral("已连接：%1").arg(t->description()));
    m_statusLabel->setText(QStringLiteral("已连接：%1").arg(t->description()));
    m_connectBtn->setText(QStringLiteral("断开连接"));
    m_progress->setValue(0);
}

void MainWindow::onBrowseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择固件"), QString(),
        QStringLiteral("固件镜像 (*.bin *.BIN *.hex *.HEX);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    loadFirmware(path);
}

bool MainWindow::loadFirmware(const QString &path)
{
    const QFileInfo info(path);
    const QString   suffix = info.suffix().toLower();
    QString         err;

    if (suffix == QLatin1String("hex")) {
        /* Intel HEX 是文本格式，体积约为固件的 2.8 倍，
         * 必须先解析成纯二进制再做容量校验 */
        quint32 base = 0;
        if (!hexloader::parseIntelHex(path, m_image, base, err)) {
            QMessageBox::warning(this, QStringLiteral("HEX 解析失败"),
                                 QStringLiteral("%1\n\n%2").arg(path, err));
            return false;
        }

        const auto &t = currentTarget();
        if (base != t.startAddr) {
            const QMessageBox::StandardButton r = QMessageBox::question(
                this, QStringLiteral("地址不匹配"),
                QStringLiteral("HEX 基地址 %1 与所选分区起始地址 %2 不一致。\n"
                               "继续使用会以 HEX 实际内容升级，是否继续？")
                    .arg(addrText(base), addrText(t.startAddr)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes) {
                m_image.clear();
                return false;
            }
        }
        m_imageBase = base;
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("错误"),
                                 QStringLiteral("无法打开文件：%1").arg(path));
            return false;
        }
        m_image = f.readAll();
        f.close();
        m_imageBase = currentTarget().startAddr;    /* bin 无地址信息，假定就是所选分区 */
    }

    if (m_image.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("固件文件为空"));
        return false;
    }

    m_imagePath = path;
    m_filePath->setText(path);
    m_filePath->setToolTip(path);
    onTargetChanged();      /* 刷新容量校验 */
    return true;
}

void MainWindow::onTargetChanged()
{
    updateTargetInfo();

    if (m_image.isEmpty()) {
        m_fileInfo->clear();
        return;
    }

    const auto &t = currentTarget();
    const int sz = m_image.size();
    const double pct = 100.0 * sz / static_cast<double>(t.size);
    const bool overflow = static_cast<quint32>(sz) > t.size;

    const bool isHex = m_imagePath.section(QLatin1Char('.'), -1).toLower() == QLatin1String("hex");
    const QString formatNote = isHex
        ? QStringLiteral("（HEX 已解析为二进制，基址 %1）").arg(addrText(m_imageBase))
        : QString();
    m_fileInfo->setText(QStringLiteral("文件 %1 字节 / 分区 %2 字节（占用 %3%）%4")
                            .arg(sz)
                            .arg(t.size)
                            .arg(QString::number(pct, 'f', 1))
                            .arg(formatNote));
    m_fileInfo->setStyleSheet(overflow ? QStringLiteral("color:#c01c28; font-weight:bold;")
                                       : QStringLiteral("color:#26a269;"));
}

void MainWindow::updateTargetInfo()
{
    const auto &t = currentTarget();
    m_targetAddrLabel->setText(QStringLiteral("起始地址  %1").arg(addrText(t.startAddr)));
    m_targetSizeLabel->setText(QStringLiteral("分区容量  %1 字节（%2 KB）   暂存区：W25Q64 外部 Flash")
                                   .arg(t.size)
                                   .arg(t.size / 1024));
}

void MainWindow::onStartClicked()
{
    if (!m_transport || !m_transport->isOpen()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先连接设备"));
        return;
    }
    if (m_image.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择固件文件"));
        return;
    }

    const auto &t = currentTarget();
    if (static_cast<quint32>(m_image.size()) > t.size) {
        QMessageBox::warning(this, QStringLiteral("固件过大"),
                             QStringLiteral("固件 %1 字节，超过 %2 分区容量 %3 字节")
                                 .arg(m_image.size())
                                 .arg(QString::fromUtf8(t.title))
                                 .arg(t.size));
        return;
    }

    const QString verText = m_versionEdit->text().trimmed();
    const QStringList parts = verText.split(QLatin1Char('.'));
    int major = 0, minor = 0;
    if (parts.size() >= 1) major = parts.at(0).toInt();
    if (parts.size() >= 2) minor = parts.at(1).toInt();
    const quint16 version = static_cast<quint16>(((major & 0xFF) << 8) | (minor & 0xFF));

    int chunk = m_chunkSpin->value();
    if (m_modeCombo->currentIndex() == 2) {
        chunk = m_loraChunk->value();
    }

    QString err;
    if (!m_session->start(m_image, t.target, version, chunk, m_retrySpin->value(), &err)) {
        QMessageBox::warning(this, QStringLiteral("无法开始"), err);
        return;
    }

    m_progress->setValue(0);
    setUiBusy(true);
    m_statusLabel->setText(QStringLiteral("正在升级：%1").arg(QString::fromUtf8(t.title)));
}

void MainWindow::onAbortClicked()
{
    if (m_session && m_session->isRunning()) {
        m_session->abort(QStringLiteral("用户点击中止"));
    }
}

void MainWindow::onSessionLog(const QString &line)
{
    log(line);
}

void MainWindow::onSessionProgress(qint64 sent, qint64 total)
{
    if (total <= 0) {
        return;
    }
    m_progress->setValue(static_cast<int>(sent * 100 / total));
    statusBar()->showMessage(QStringLiteral("已发送 %1 / %2 字节").arg(sent).arg(total));
}

void MainWindow::onSessionFinished(bool ok, const QString &message)
{
    setUiBusy(false);
    m_statusLabel->setText(ok ? QStringLiteral("升级完成，等待设备重启")
                              : QStringLiteral("升级失败：%1").arg(message));
    if (ok) {
        m_progress->setValue(100);
        QMessageBox::information(this, QStringLiteral("完成"),
                                 QStringLiteral("%1\n\n设备会自动重启，Bootloader 搬运完成后"
                                                "串口会打印新版本号。").arg(message));
    } else {
        QMessageBox::warning(this, QStringLiteral("失败"), message);
    }
}

void MainWindow::onTransportError(const QString &msg)
{
    log(QStringLiteral("链路错误：%1").arg(msg));
}

/* ------------------------------- 小工具 ------------------------------- */

void MainWindow::log(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
}

void MainWindow::setUiBusy(bool busy)
{
    m_connectBtn->setEnabled(!busy && m_modeCombo->currentIndex() != 3);
    m_startBtn->setEnabled(!busy);
    m_abortBtn->setEnabled(busy);
    m_modeCombo->setEnabled(!busy);
    m_filePath->setEnabled(!busy);
    m_rbApp->setEnabled(!busy);
    m_rbReset->setEnabled(!busy);
    m_rbBoot->setEnabled(!busy);
    m_chunkSpin->setEnabled(!busy);
    m_retrySpin->setEnabled(!busy);
    m_versionEdit->setEnabled(!busy);
}

QString MainWindow::currentPortName() const
{
    const QString data = m_uartPort->currentData().toString();
    return data.isEmpty() ? m_uartPort->currentText() : data;
}

int MainWindow::currentBaudRate() const
{
    const int baud = m_uartBaud->currentData().toInt();
    return baud > 0 ? baud : m_uartBaud->currentText().toInt();
}

int MainWindow::currentTargetIndex() const
{
    if (m_rbReset->isChecked()) {
        return 1;
    }
    if (m_rbBoot->isChecked()) {
        return 2;
    }
    return 0;
}

TargetInfo MainWindow::currentTarget() const
{
    const int idx = currentTargetIndex();
    if (idx >= 0 && idx <= 2) {
        return kTargets[idx];
    }
    return kTargets[0];
}
