#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <memory>

#include "core/ScanSummary.h"
#include "DatabaseManager.h"
#include "FileMonitor.h"
#include "ScanWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void addDirectory();
    void removeSelectedDirectory();
    void scanOnce();
    void clearHistory();
    void exportReport();
    void onStatusFilterChanged(int index);
    void onSearchTextChanged(const QString &text);
    void openSelectedFile(const QModelIndex &index);
    void onHistoryFilterChanged(int index);
    void onHistorySearchChanged(const QString &text);
    void showExclusionsDialog();
    void showFaqDialog();
    void triggerMonitoringTick();
    void toggleMonitoring();
    void showFromTray();
    void pauseOrResumeMonitoring();
    void startMonitoring();
    void stopMonitoring();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class ScanTrigger { Manual, Scheduled };

    void setupUi();
    void setupTrayIcon();
    void populateCurrentRecords();
    void reloadHistory();
    void setupModel();
    void appendResults(const QVector<FileRecordEntry> &results);
    void rebuildTable();
    QString readableStatus(const QString &raw) const;
    int statusValue(const QString &status) const;
    core::ScanSummary calculateSummary(const QVector<FileRecordEntry> &results) const;
    void updateStatusBar();
    void appendLogMessage(const QString &message);
    void loadMonitoredDirsFromSettings();
    void saveMonitoredDirsToSettings();
    void loadExcludeRulesFromSettings();
    void saveExcludeRulesToSettings();
    void loadScanOptions();
    void saveScanOptions();
    void saveMonitoringState();
    void beginScan(ScanTrigger trigger);
    void updateMonitoringUi();
    void updateActionAvailability();
    void ensureDefaultSettings();
    QString defaultDatabasePath() const;
    void scheduleNextScan();
    void showSummaryNotification(const core::ScanSummary &summary);
    void rescanSingleFile(const QString &path);
    QString statusDisplayText(const QString &status) const;
    QColor statusColor(const QString &status) const;
    QString formatPermissionInfo(const FileRecordEntry &rec) const;
    void handleScanFinished(const QVector<FileRecordEntry> &results);
    void handleScanError(const QString &message);
    void handleScanProgress(int current, int total);
    void handleScanFile(const QString &path);
    void updateProgressLabel(int current, int total);
    void configureFileTableHeaders();
    void configureHistoryTableHeaders();

    QString m_databasePath;
    QSettings m_settings;
    DatabaseManager m_databaseManager;
    FileMonitor m_fileMonitor;
    QByteArray m_hmacKey;
    QThread *m_scanThread = nullptr;
    ScanWorker *m_scanWorker = nullptr;
    bool m_scanInProgress = false;
    bool m_monitoringEnabled = false;
    bool m_forceExit = false;
    QVector<FileRecordEntry> m_allResults;
    QDateTime m_lastScan;
    QVector<ExcludeRule> m_excludeRules;
    QTimer *m_scanTimer = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_trayScanAction = nullptr;
    QAction *m_trayPauseAction = nullptr;
    QAction *m_monitoringToggleAction = nullptr;

    QListWidget *m_dirList;
    QTableView *m_tableView;
    QTableView *m_historyView;
    QPlainTextEdit *m_logView;
    QComboBox *m_statusFilter;
    QLineEdit *m_searchEdit;
    QComboBox *m_historyStatusFilter;
    QLineEdit *m_historySearchEdit;
    QLabel *m_lastScanLabel;
    QLabel *m_statsLabel;
    QLabel *m_progressLabel;
    QAction *m_scanAction;
    QAction *m_addDirAction;
    QAction *m_removeDirAction;
    QAction *m_exportAction;
    QAction *m_clearHistoryAction;
    QAction *m_faqAction;
    bool m_recursiveOption{true};
    bool m_followSymlinksOption{false};
    int m_maxDepthOption{20};
    QSpinBox *m_intervalSpin;
    QStandardItemModel *m_tableModel;
    QSortFilterProxyModel *m_proxyModel;
    QStandardItemModel *m_historyModel;
    QSortFilterProxyModel *m_historyProxy;
};

#endif // MAINWINDOW_H
