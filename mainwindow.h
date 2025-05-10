#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QNetworkInterface>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDir>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void startPXESetup();
    void readOutput();
    void readError();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void updateInterfaceInfo(int index);
    void selectIsoFile();
    void checkDnsmasqStatus();

private:
    QTextEdit *outputText;
    QPushButton *startButton;
    QProcess *process;
    QComboBox *interfaceCombo;
    QLabel *ipAddressLabel;
    QList<QNetworkInterface> interfaces;
    
    // ISO file selection components
    QPushButton *selectIsoButton;
    QLabel *isoPathLabel;
    QString selectedIsoPath;
    
    void populateInterfaces();
    QString getInterfaceIP(const QNetworkInterface &interface);
    QString getNetworkPrefix(const QString &ipAddress);
    void createPXEConfig(const QString &interface, const QString &ipAddress);
    void setupTFTPDirectory();
    void setupSambaShare();
    void createWinPEConfig();
    bool isDnsmasqRunning();
};

#endif // MAINWINDOW_H 