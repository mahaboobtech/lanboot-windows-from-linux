#include "mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Create central widget and layout
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    // Create network interface section
    QHBoxLayout *networkLayout = new QHBoxLayout();
    QLabel *interfaceLabel = new QLabel("Network Interface:", this);
    interfaceCombo = new QComboBox(this);
    ipAddressLabel = new QLabel("IP Address: Not Selected", this);
    
    networkLayout->addWidget(interfaceLabel);
    networkLayout->addWidget(interfaceCombo);
    networkLayout->addWidget(ipAddressLabel);
    networkLayout->addStretch();
    
    // Create ISO file selection section
    QHBoxLayout *isoLayout = new QHBoxLayout();
    selectIsoButton = new QPushButton("Select ISO File", this);
    isoPathLabel = new QLabel("No ISO file selected", this);
    
    isoLayout->addWidget(selectIsoButton);
    isoLayout->addWidget(isoPathLabel);
    isoLayout->addStretch();
    
    // Create text edit for output
    outputText = new QTextEdit(this);
    outputText->setReadOnly(true);
    outputText->setMinimumSize(600, 400);
    
    // Create start button
    startButton = new QPushButton("Setup PXE Server", this);
    
    // Add widgets to layout
    mainLayout->addLayout(networkLayout);
    mainLayout->addLayout(isoLayout);
    mainLayout->addWidget(outputText);
    mainLayout->addWidget(startButton);
    
    // Set central widget
    setCentralWidget(centralWidget);
    
    // Create process
    process = new QProcess(this);
    
    // Connect signals and slots
    connect(startButton, &QPushButton::clicked, this, &MainWindow::startPXESetup);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::readOutput);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::readError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::processFinished);
    connect(interfaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateInterfaceInfo);
    connect(selectIsoButton, &QPushButton::clicked, this, &MainWindow::selectIsoFile);
    
    // Populate network interfaces
    populateInterfaces();
    
    // Set window properties
    setWindowTitle("PXE Server Setup");
    resize(800, 600);
}

MainWindow::~MainWindow()
{
    if (process->state() == QProcess::Running) {
        process->terminate();
        process->waitForFinished();
    }
}

QString MainWindow::getNetworkPrefix(const QString &ipAddress)
{
    QRegularExpression re("^(\\d+\\.\\d+\\.\\d+)\\.\\d+$");
    QRegularExpressionMatch match = re.match(ipAddress);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return "192.168.1"; // Default fallback
}

void MainWindow::createPXEConfig(const QString &interface, const QString &ipAddress)
{
    QString prefix = getNetworkPrefix(ipAddress);
    QString config = QString(
        "port=0\n"
        "interface=%1\n"
        "bind-interfaces\n"
        "dhcp-range=%2.100,%2.200,12h\n"
        "dhcp-boot=pxelinux.0\n"
        "enable-tftp\n"
        "tftp-root=/srv/tftp\n"
    ).arg(interface).arg(prefix);

    QFile file("/etc/dnsmasq.d/pxe.conf");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << config;
        file.close();
    }
}

void MainWindow::setupTFTPDirectory()
{
    auto runCommand = [this](const QStringList &args) {
        QProcess cmdProcess;
        cmdProcess.setProcessChannelMode(QProcess::MergedChannels);
        
        connect(&cmdProcess, &QProcess::readyReadStandardOutput, [&]() {
            outputText->append(cmdProcess.readAllStandardOutput());
            // Auto scroll to bottom
            QTextCursor cursor = outputText->textCursor();
            cursor.movePosition(QTextCursor::End);
            outputText->setTextCursor(cursor);
        });
        
        cmdProcess.start("sudo", args);
        cmdProcess.waitForFinished(-1);
        return cmdProcess.exitCode();
    };

    runCommand(QStringList() << "mkdir" << "-p" << "/srv/tftp");
    runCommand(QStringList() << "apt" << "install" << "-y" << "syslinux-common" << "pxelinux");
    runCommand(QStringList() << "cp" << "/usr/lib/syslinux/modules/bios/libutil.c32" << "/srv/tftp/");
    runCommand(QStringList() << "cp" << "/usr/lib/syslinux/modules/bios/menu.c32" << "/srv/tftp/");
    runCommand(QStringList() << "cp" << "/usr/lib/syslinux/memdisk" << "/srv/tftp/");
    runCommand(QStringList() << "cp" << "/usr/lib/PXELINUX/pxelinux.0" << "/srv/tftp/");
    runCommand(QStringList() << "cp" << "/usr/lib/syslinux/modules/bios/ldlinux.c32" << "/srv/tftp/");
}

void MainWindow::setupSambaShare()
{
    QString smbConfig = QString(
        "[install]\n"
        "path = /srv/samba/install\n"
        "read only = yes\n"
        "guest ok = yes\n"
    );

    QFile file("/etc/samba/smb.conf");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << "\n" << smbConfig;
        file.close();
    }

    QProcess::execute("sudo", QStringList() << "systemctl" << "restart" << "smbd");
    QProcess::execute("sudo", QStringList() << "systemctl" << "enable" << "smbd");
}

void MainWindow::createWinPEConfig()
{
    QProcess::execute("sudo", QStringList() << "mkdir" << "-p" << "/srv/tftp/pxelinux.cfg");
    
    QString defaultConfig = QString(
        "UI         menu.c32\n"
        "MENU TITLE Network Boot\n"
        "TIMEOUT    50\n\n"
        "LABEL      winpe\n"
        "MENU LABEL Boot Windows PE from network\n"
        "KERNEL     /memdisk\n"
        "INITRD     winpe.iso\n"
        "APPEND     iso raw\n\n"
        "LABEL      localboot\n"
        "MENU LABEL Boot from local disk\n"
        "LOCALBOOT  0\n"
    );

    QFile defaultFile("/srv/tftp/pxelinux.cfg/default");
    if (defaultFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&defaultFile);
        out << defaultConfig;
        defaultFile.close();
    }

    // Create winpe directory and start.cmd
    QProcess::execute("sudo", QStringList() << "mkdir" << "-p" << "/srv/tftp/winpe");
    
    QString startCmd = QString(
        "wpeinit\n"
        "net use Z: \\\\%1\\install\n"
        "dir\n"
        "Z:\\setup.exe\n"
    ).arg(ipAddressLabel->text().split(": ").last());

    QFile startFile("/srv/tftp/winpe/start.cmd");
    if (startFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&startFile);
        out << startCmd;
        startFile.close();
    }
}

void MainWindow::startPXESetup()
{
    if (selectedIsoPath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select an ISO file first!");
        return;
    }

    outputText->clear();
    startButton->setEnabled(false);
    
    // Get current interface and IP
    QString interface = interfaceCombo->currentText();
    QString ipAddress = ipAddressLabel->text().split(": ").last();
    
    // Function to run command and capture output
    auto runCommand = [this](const QStringList &args) {
        QProcess *cmdProcess = new QProcess(this);
        cmdProcess->setProcessChannelMode(QProcess::MergedChannels);
        
        // Set environment to disable output buffering
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONUNBUFFERED", "1");
        env.insert("PYTHONIOENCODING", "utf-8");
        cmdProcess->setProcessEnvironment(env);
        
        // Connect to readyRead signals for both stdout and stderr
        connect(cmdProcess, &QProcess::readyReadStandardOutput, [this, cmdProcess]() {
            QString output = cmdProcess->readAllStandardOutput();
            outputText->append(output);
            outputText->repaint();
            QApplication::processEvents();
        });
        
        connect(cmdProcess, &QProcess::readyReadStandardError, [this, cmdProcess]() {
            QString output = cmdProcess->readAllStandardError();
            outputText->append(output);
            outputText->repaint();
            QApplication::processEvents();
        });
        
        // Start the process
        cmdProcess->start("sudo", args);
        
        // Wait for the process to finish
        if (!cmdProcess->waitForFinished(-1)) {
            outputText->append("Command timed out!\n");
            cmdProcess->kill();
            delete cmdProcess;
            return -1;
        }
        
        int exitCode = cmdProcess->exitCode();
        delete cmdProcess;
        return exitCode;
    };

    // Install required packages
    outputText->append("Installing required packages...\n");
    if (runCommand(QStringList() << "apt" << "install" << "-y" << "dnsmasq" << "samba" << "genisoimage" << "wget" << "unzip" << "wimtools" << "rsync") != 0) {
        outputText->append("Failed to install packages!\n");
        startButton->setEnabled(true);
        return;
    }

    // Create temporary mount point and mount ISO
    outputText->append("\nSetting up temporary mount point...\n");
    if (runCommand(QStringList() << "mkdir" << "-p" << "/mnt/winiso") != 0) {
        outputText->append("Failed to create mount point!\n");
        startButton->setEnabled(true);
        return;
    }

    outputText->append("\nMounting Windows ISO...\n");
    if (runCommand(QStringList() << "mount" << "-o" << "loop,ro" << selectedIsoPath << "/mnt/winiso") != 0) {
        outputText->append("Failed to mount ISO!\n");
        startButton->setEnabled(true);
        return;
    }

    // Create Samba share directory and copy files
    outputText->append("\nCreating Samba share directory...\n");
    if (runCommand(QStringList() << "mkdir" << "-p" << "/srv/samba/install") != 0) {
        outputText->append("Failed to create Samba directory!\n");
        runCommand(QStringList() << "umount" << "/mnt/winiso");
        startButton->setEnabled(true);
        return;
    }

    outputText->append("\nCopying Windows installation files (this may take a while)...\n");
    if (runCommand(QStringList() << "rsync" << "-avh" << "--progress" << "/mnt/winiso/" << "/srv/samba/install/") != 0) {
        outputText->append("Failed to copy files!\n");
        runCommand(QStringList() << "umount" << "/mnt/winiso");
        startButton->setEnabled(true);
        return;
    }

    outputText->append("\nUnmounting ISO...\n");
    runCommand(QStringList() << "umount" << "/mnt/winiso");

    // Setup PXE configuration
    outputText->append("\nConfiguring PXE server...\n");
    createPXEConfig(interface, ipAddress);
    
    // Setup TFTP directory
    outputText->append("\nSetting up TFTP directory...\n");
    setupTFTPDirectory();
    
    // Setup Samba share
    outputText->append("\nConfiguring Samba share...\n");
    setupSambaShare();
    
    // Create WinPE configuration
    outputText->append("\nCreating WinPE configuration...\n");
    createWinPEConfig();
    
    // Create WinPE ISO
    outputText->append("\nCreating WinPE ISO...\n");
    if (runCommand(QStringList() << "mkwinpeimg" << "--iso" << "--windows-dir=/srv/samba/install" 
        << "--start-script=/srv/tftp/winpe/start.cmd" << "/tmp/winpe.iso") != 0) {
        outputText->append("Failed to create WinPE ISO!\n");
        startButton->setEnabled(true);
        return;
    }
    
    // Copy WinPE ISO to TFTP directory
    outputText->append("\nCopying WinPE ISO to TFTP directory...\n");
    runCommand(QStringList() << "cp" << "/tmp/winpe.iso" << "/srv/tftp/");
    
    // Restart services
    outputText->append("\nRestarting services...\n");
    runCommand(QStringList() << "systemctl" << "restart" << "dnsmasq");
    runCommand(QStringList() << "systemctl" << "enable" << "dnsmasq");
    
    // Allow TFTP port
    outputText->append("\nConfiguring firewall...\n");
    runCommand(QStringList() << "ufw" << "allow" << "69/udp");
    
    outputText->append("\nSetup completed. Checking dnsmasq status...\n");
    startButton->setEnabled(true);
    
    // Check dnsmasq status after a short delay
    QTimer::singleShot(2000, this, &MainWindow::checkDnsmasqStatus);
}

void MainWindow::selectIsoFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Select ISO File", "", "ISO Files (*.iso);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        selectedIsoPath = fileName;
        // Display only the filename, not the full path
        QFileInfo fileInfo(fileName);
        isoPathLabel->setText("Selected ISO: " + fileInfo.fileName());
    }
}

void MainWindow::populateInterfaces()
{
    interfaces = QNetworkInterface::allInterfaces();
    interfaceCombo->clear();
    
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.isValid() && interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            interfaceCombo->addItem(interface.humanReadableName());
        }
    }
}

QString MainWindow::getInterfaceIP(const QNetworkInterface &interface)
{
    QList<QNetworkAddressEntry> addresses = interface.addressEntries();
    for (const QNetworkAddressEntry &address : addresses) {
        if (address.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            return address.ip().toString();
        }
    }
    return "No IP Address";
}

void MainWindow::updateInterfaceInfo(int index)
{
    if (index >= 0 && index < interfaces.size()) {
        QString ip = getInterfaceIP(interfaces[index]);
        ipAddressLabel->setText("IP Address: " + ip);
    } else {
        ipAddressLabel->setText("IP Address: Not Selected");
    }
}

void MainWindow::readOutput()
{
    outputText->append(process->readAllStandardOutput());
    // Auto scroll to bottom
    QTextCursor cursor = outputText->textCursor();
    cursor.movePosition(QTextCursor::End);
    outputText->setTextCursor(cursor);
}

void MainWindow::readError()
{
    outputText->append(process->readAllStandardError());
    // Auto scroll to bottom
    QTextCursor cursor = outputText->textCursor();
    cursor.movePosition(QTextCursor::End);
    outputText->setTextCursor(cursor);
}

bool MainWindow::isDnsmasqRunning()
{
    QProcess checkProcess;
    checkProcess.start("systemctl", QStringList() << "is-active" << "dnsmasq");
    checkProcess.waitForFinished();
    return checkProcess.exitCode() == 0;
}

void MainWindow::checkDnsmasqStatus()
{
    if (isDnsmasqRunning()) {
        QMessageBox::information(this, "Success", "PXE Server setup completed successfully!\nDNSMASQ is running.");
    } else {
        QMessageBox::critical(this, "Error", "PXE Server setup completed but DNSMASQ is not running.\nPlease check the system logs for details.");
    }
}

void MainWindow::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    startButton->setEnabled(true);
    
    if (exitStatus == QProcess::CrashExit) {
        QMessageBox::critical(this, "Error", "The setup process crashed!");
    } else if (exitCode != 0) {
        QMessageBox::warning(this, "Warning", 
            QString("The setup process finished with exit code %1").arg(exitCode));
    } else {
        // Check dnsmasq status after a short delay to ensure it has time to start
        QTimer::singleShot(2000, this, &MainWindow::checkDnsmasqStatus);
    }
} 