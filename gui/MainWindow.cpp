#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class FileFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FileFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setStatusFilterValue(int value) {
        if (m_statusFilterValue == value) {
            return;
        }
        m_statusFilterValue = value;
        invalidateFilter();
    }

    void setSearchTerm(const QString &text) {
        const QString normalized = text.trimmed();
        if (m_searchTerm == normalized) {
            return;
        }
        m_searchTerm = normalized;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        const QModelIndex statusIndex = sourceModel()->index(source_row, 1, source_parent);
        const int statusValue = sourceModel()->data(statusIndex, Qt::UserRole + 1).toInt();
        if (m_statusFilterValue != -1 && statusValue != m_statusFilterValue) {
            return false;
        }

        if (m_searchTerm.isEmpty()) {
            return true;
        }

        const QModelIndex pathIndex = sourceModel()->index(source_row, 0, source_parent);
        const QString pathText = sourceModel()->data(pathIndex, Qt::DisplayRole).toString();
        return pathText.contains(m_searchTerm, Qt::CaseInsensitive);
    }

private:
    int m_statusFilterValue = -1;
    QString m_searchTerm;
};

class HistoryFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit HistoryFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setStatusFilterValue(int value) {
        if (m_statusFilterValue == value) {
            return;
        }
        m_statusFilterValue = value;
        invalidateFilter();
    }

    void setSearchTerm(const QString &text) {
        const QString normalized = text.trimmed();
        if (m_searchTerm == normalized) {
            return;
        }
        m_searchTerm = normalized;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        const QModelIndex statusIndex = sourceModel()->index(source_row, 2, source_parent);
        const int newStatusValue = sourceModel()->data(statusIndex, Qt::UserRole + 1).toInt();
        if (m_statusFilterValue != -1 && newStatusValue != m_statusFilterValue) {
            return false;
        }

        if (m_searchTerm.isEmpty()) {
            return true;
        }

        const QModelIndex pathIndex = sourceModel()->index(source_row, 1, source_parent);
        const QString pathText = sourceModel()->data(pathIndex, Qt::DisplayRole).toString();
        return pathText.contains(m_searchTerm, Qt::CaseInsensitive);
    }

private:
    int m_statusFilterValue = -1;
    QString m_searchTerm;
};

QSettings createSettings()
{
    return QSettings(QSettings::IniFormat,
                     QSettings::UserScope,
                     QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
}

QString resolveDatabasePath()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString defaultPath = QDir(dataDir).filePath(QStringLiteral("integrity.db"));
    QSettings settings = createSettings();
    const QString path = settings.value(QStringLiteral("databasePath"), defaultPath).toString();
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    return path;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_databasePath(resolveDatabasePath()),
      m_settings(createSettings()),
      m_databaseManager(m_databasePath),
      m_fileMonitor(m_databaseManager),
      m_hmacKey(QByteArrayLiteral("gui-demo-key")),
      m_tableModel(nullptr),
      m_proxyModel(nullptr) {
    ensureDefaultSettings();
    if (m_settings.contains(QStringLiteral("monitoringEnabled"))) {
        m_monitoringEnabled = m_settings.value(QStringLiteral("monitoringEnabled"), false).toBool();
    }
    m_databaseManager.setHmacKey(m_hmacKey);
    if (!m_databaseManager.initialize()) {
        QMessageBox::critical(this, tr("Database Error"), tr("Failed to initialize SQLite database."));
    }

    setupModel();
    setupUi();
    loadMonitoredDirsFromSettings();
    loadExcludeRulesFromSettings();
    loadScanOptions();
    m_fileMonitor.setExcludeRules(m_excludeRules);
    populateCurrentRecords();
    reloadHistory();

    setupTrayIcon();

    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    m_scanTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_scanTimer, &QTimer::timeout, this, &MainWindow::triggerMonitoringTick);
    updateMonitoringUi();
}

MainWindow::~MainWindow() {
    saveMonitoredDirsToSettings();
    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
    }
}

void MainWindow::setupModel() {
    m_tableModel = new QStandardItemModel(this);
    m_tableModel->setHorizontalHeaderLabels({tr("Путь"), tr("Статус"), tr("Владелец/права"), tr("Текущий хеш"), tr("Предыдущий хеш"), tr("Обновлено")});

    m_proxyModel = new FileFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_tableModel);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_historyModel = new QStandardItemModel(this);
    m_historyModel->setHorizontalHeaderLabels({tr("Время"), tr("Файл"), tr("Новый статус"), tr("Старый статус"), tr("Комментарий")});

    m_historyProxy = new HistoryFilterProxyModel(this);
    m_historyProxy->setSourceModel(m_historyModel);
    m_historyProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
}

void MainWindow::setupUi() {
    setWindowTitle(tr("File Integrity Monitor"));
    resize(1200, 700);

    auto *toolbar = addToolBar(tr("Главная"));
    toolbar->setMovable(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(24, 24));

    m_scanAction = toolbar->addAction(QIcon::fromTheme("image-viewer-app-symbolic"), tr("Сканировать сейчас"));
    m_scanAction->setShortcut(Qt::Key_F5);
    m_scanAction->setShortcutVisibleInContextMenu(false);
    m_scanAction->setToolTip(tr("Одноразовое сканирование"));
    m_scanAction->setStatusTip(tr("Запустить проверку выбранных директорий"));
    m_monitoringToggleAction = toolbar->addAction(QIcon::fromTheme("scan-type-batch-symbolic"), QString());
    m_monitoringToggleAction->setCheckable(true);
    m_monitoringToggleAction->setChecked(m_monitoringEnabled);
    toolbar->addSeparator();
    toolbar->addSeparator(); // 
    m_addDirAction = toolbar->addAction(QIcon::fromTheme("folder-new-symbolic"), tr("Добавить директорию")); 
    m_addDirAction->setToolTip(tr("Добавить директорию"));
    m_addDirAction->setStatusTip(tr("Добавить новую директорию для мониторинга"));
    m_removeDirAction = toolbar->addAction(QIcon::fromTheme("edit-delete-symbolic"), tr("Удалить директорию"));
    m_removeDirAction->setToolTip(tr("Удалить директорию"));
    m_removeDirAction->setStatusTip(tr("Удалить выбранную директорию из списка"));
    toolbar->addSeparator();
    m_exportAction = toolbar->addAction(QIcon::fromTheme("aptdaemon-download-symbolic"), tr("Экспорт отчёта")); 
    m_exportAction->setToolTip(tr("Экспорт отчёта"));
    m_exportAction->setStatusTip(tr("Сохранить текущие результаты сканирования"));
    m_clearHistoryAction = toolbar->addAction(QIcon::fromTheme("edit-clear-all-symbolic"), tr("Очистить сканирования"));
    m_clearHistoryAction->setToolTip(tr("Очистить таблицу истории сканирований"));
    m_clearHistoryAction->setStatusTip(tr("Очистить таблицу истории сканирований"));
    toolbar->addSeparator();
    m_faqAction = toolbar->addAction(QIcon::fromTheme("gnome-help-symbolic"), tr("FAQ"));
    m_faqAction->setToolTip(tr("Справка"));
    m_faqAction->setStatusTip(tr("Открыть справку"));

    auto *fileMenu = menuBar()->addMenu(tr("Файл"));
    fileMenu->addAction(m_scanAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAction);
    fileMenu->addAction(tr("Очистить историю"), this, &MainWindow::clearHistory);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Выход"), this, &QWidget::close);

    auto *settingsMenu = menuBar()->addMenu(tr("Настройки"));
    settingsMenu->addAction(tr("Исключения..."), this, &MainWindow::showExclusionsDialog);

    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);

    auto *leftPanel = new QWidget(mainSplitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->addWidget(new QLabel(tr("Мониторинг"), leftPanel));

    m_dirList = new QListWidget(leftPanel);
    m_dirList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dirList->setAlternatingRowColors(true);
    m_dirList->setContextMenuPolicy(Qt::NoContextMenu);
    leftLayout->addWidget(new QLabel(tr("Отслеживаемые директории"), leftPanel));
    leftLayout->addWidget(m_dirList, 1);

    auto *hintLabel = new QLabel(tr("Указанные папки будут сканироваться на изменения."), leftPanel);
    hintLabel->setWordWrap(true);
    leftLayout->addWidget(hintLabel);

    auto *optionsBox = new QGroupBox(tr("Фоновый мониторинг"), leftPanel);
    auto *optionsLayout = new QFormLayout(optionsBox);
    optionsLayout->setLabelAlignment(Qt::AlignLeft);

    m_intervalSpin = new QSpinBox(optionsBox);
    m_intervalSpin->setRange(0, 86400);
    m_intervalSpin->setSpecialValueText(tr("Отключено"));
    m_intervalSpin->setSuffix(tr(" сек"));
    m_intervalSpin->setToolTip(tr("Интервал фонового сканирования"));
    optionsLayout->addRow(tr("Интервал"), m_intervalSpin);

    leftLayout->addWidget(optionsBox);

    auto *rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    auto *topRight = new QWidget(rightSplitter);
    auto *topLayout = new QVBoxLayout(topRight);

    auto *filtersLayout = new QHBoxLayout();
    filtersLayout->addWidget(new QLabel(tr("Показать:"), topRight));

    m_statusFilter = new QComboBox(topRight);
    m_statusFilter->addItem(tr("Все"), -1);
    m_statusFilter->addItem(tr("Без изменений"), 0);
    m_statusFilter->addItem(tr("Изменён"), 1);
    m_statusFilter->addItem(tr("Новый"), 2);
    m_statusFilter->addItem(tr("Удалён"), 3);
    m_statusFilter->addItem(tr("Ошибка"), 4);
    filtersLayout->addWidget(m_statusFilter);

    m_searchEdit = new QLineEdit(topRight);
    m_searchEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_searchEdit->setPlaceholderText(tr("Поиск по пути или имени файла..."));
    m_searchEdit->setStyleSheet(QStringLiteral("color: white;"));
    filtersLayout->addWidget(m_searchEdit, 1);
    filtersLayout->addStretch();

    topLayout->addLayout(filtersLayout);

    m_tableView = new QTableView(topRight);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->setContextMenuPolicy(Qt::NoContextMenu);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    configureFileTableHeaders();
    topLayout->addWidget(m_tableView, 1);

    m_logView = new QPlainTextEdit(rightSplitter);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(5000);
    m_logView->setPlaceholderText(tr("Здесь будут отображаться события и ошибки..."));
    m_logView->setStyleSheet(QStringLiteral("color: white;"));
    m_logView->setContextMenuPolicy(Qt::NoContextMenu);

    auto *historyPage = new QWidget(rightSplitter);
    auto *historyLayout = new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(4, 4, 4, 4);
    auto *historyFilters = new QHBoxLayout();
    m_historyStatusFilter = new QComboBox(historyPage);
    m_historyStatusFilter->addItem(tr("Все"), -1);
    m_historyStatusFilter->addItem(tr("Без изменений"), 0);
    m_historyStatusFilter->addItem(tr("Изменён"), 1);
    m_historyStatusFilter->addItem(tr("Новый"), 2);
    m_historyStatusFilter->addItem(tr("Удалён"), 3);
    m_historyStatusFilter->addItem(tr("Ошибка"), 4);
    historyFilters->addWidget(m_historyStatusFilter);
    m_historySearchEdit = new QLineEdit(historyPage);
    m_historySearchEdit->setPlaceholderText(tr("Поиск по пути..."));
    m_historySearchEdit->setStyleSheet(QStringLiteral("color: white;"));
    m_historySearchEdit->setContextMenuPolicy(Qt::NoContextMenu);
    historyFilters->addWidget(m_historySearchEdit, 1);
    historyLayout->addLayout(historyFilters);

    m_historyView = new QTableView(historyPage);
    m_historyView->setModel(m_historyProxy);
    m_historyView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyView->setAlternatingRowColors(true);
    m_historyView->setSortingEnabled(true);
    m_historyView->setContextMenuPolicy(Qt::NoContextMenu);
    configureHistoryTableHeaders();
    historyLayout->addWidget(m_historyView, 1);

    auto *bottomTabs = new QTabWidget(rightSplitter);
    bottomTabs->addTab(m_logView, tr("Лог"));
    bottomTabs->addTab(historyPage, tr("История"));

    rightSplitter->addWidget(topRight);
    rightSplitter->addWidget(bottomTabs);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);

    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);

    mainLayout->addWidget(mainSplitter);
    setCentralWidget(central);

    auto *status = statusBar();
    m_lastScanLabel = new QLabel(tr("Последняя проверка: —"), this);
    m_statsLabel = new QLabel(tr("Файлов: 0"), this);
    m_progressLabel = new QLabel(this);
    updateProgressLabel(0, 0);
    status->addWidget(m_lastScanLabel);
    status->addPermanentWidget(m_statsLabel);
    status->addPermanentWidget(m_progressLabel);

    connect(m_scanAction, &QAction::triggered, this, &MainWindow::scanOnce);
    connect(m_monitoringToggleAction, &QAction::triggered, this, &MainWindow::toggleMonitoring);
    connect(m_addDirAction, &QAction::triggered, this, &MainWindow::addDirectory);
    connect(m_removeDirAction, &QAction::triggered, this, &MainWindow::removeSelectedDirectory);
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportReport);
    connect(m_clearHistoryAction, &QAction::triggered, this, &MainWindow::clearHistory);
    connect(m_faqAction, &QAction::triggered, this, &MainWindow::showFaqDialog);

    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, &MainWindow::onStatusFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_historyStatusFilter, &QComboBox::currentIndexChanged, this, &MainWindow::onHistoryFilterChanged);
    connect(m_historySearchEdit, &QLineEdit::textChanged, this, &MainWindow::onHistorySearchChanged);
    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::openSelectedFile);
    connect(m_intervalSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::saveScanOptions);

    onStatusFilterChanged(m_statusFilter->currentIndex());
    onHistoryFilterChanged(m_historyStatusFilter->currentIndex());
}

void MainWindow::addDirectory() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Выбор директории"));
    if (path.isEmpty()) {
        return;
    }

    for (int i = 0; i < m_dirList->count(); ++i) {
        if (m_dirList->item(i)->text() == path) {
            QMessageBox::information(this, tr("Уже добавлено"), tr("Директория уже в списке."));
            return;
        }
    }

    m_dirList->addItem(path);
    saveMonitoredDirsToSettings();
    appendLogMessage(tr("Добавлена директория: %1").arg(path));
}

void MainWindow::removeSelectedDirectory() {
    auto *item = m_dirList->currentItem();
    if (!item) {
        return;
    }
    const QString removedPath = item->text();
    appendLogMessage(tr("Удалена директория: %1").arg(removedPath));
    delete m_dirList->takeItem(m_dirList->row(item));
    saveMonitoredDirsToSettings();
}

void MainWindow::showExclusionsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Исключения"));
    dialog.resize(520, 360);

    auto *layout = new QVBoxLayout(&dialog);

    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Тип"), tr("Паттерн")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);

    table->setRowCount(m_excludeRules.size());
    for (int i = 0; i < m_excludeRules.size(); ++i) {
        const auto &rule = m_excludeRules.at(i);
        auto *typeItem = new QTableWidgetItem(rule.type == ExcludeType::Path ? tr("Путь") : tr("Маска"));
        typeItem->setData(Qt::UserRole, static_cast<int>(rule.type));
        auto *patternItem = new QTableWidgetItem(rule.pattern);
        table->setItem(i, 0, typeItem);
        table->setItem(i, 1, patternItem);
    }

    auto *controlsLayout = new QHBoxLayout();
    auto *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("Путь"), static_cast<int>(ExcludeType::Path));
    typeCombo->addItem(tr("Маска (*.log)"), static_cast<int>(ExcludeType::Glob));

    auto *patternEdit = new QLineEdit(&dialog);
    patternEdit->setPlaceholderText(tr("/var/log или *.log"));
    patternEdit->setContextMenuPolicy(Qt::NoContextMenu);

    auto *addBtn = new QToolButton(&dialog);
    addBtn->setText(tr("Добавить"));

    auto *removeBtn = new QToolButton(&dialog);
    removeBtn->setText(tr("Удалить"));

    controlsLayout->addWidget(typeCombo);
    controlsLayout->addWidget(patternEdit, 1);
    controlsLayout->addWidget(addBtn);
    controlsLayout->addWidget(removeBtn);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

    table->setContextMenuPolicy(Qt::NoContextMenu);
    layout->addWidget(table);
    layout->addLayout(controlsLayout);
    layout->addWidget(buttonBox);

    connect(addBtn, &QToolButton::clicked, &dialog, [table, typeCombo, patternEdit]() {
        const QString pattern = patternEdit->text().trimmed();
        if (pattern.isEmpty()) {
            return;
        }

        const int row = table->rowCount();
        table->insertRow(row);
        auto *typeItem = new QTableWidgetItem(typeCombo->currentText());
        typeItem->setData(Qt::UserRole, typeCombo->currentData());
        auto *patternItem = new QTableWidgetItem(pattern);
        table->setItem(row, 0, typeItem);
        table->setItem(row, 1, patternItem);
        patternEdit->clear();
    });

    connect(removeBtn, &QToolButton::clicked, &dialog, [table]() {
        const int row = table->currentRow();
        if (row >= 0) {
            table->removeRow(row);
        }
    });

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<ExcludeRule> newRules;
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *typeItem = table->item(row, 0);
        auto *patternItem = table->item(row, 1);
        if (!typeItem || !patternItem) {
            continue;
        }
        const QString pattern = patternItem->text().trimmed();
        if (pattern.isEmpty()) {
            continue;
        }
        const auto type = static_cast<ExcludeType>(typeItem->data(Qt::UserRole).toInt());
        newRules.append(ExcludeRule{type, pattern});
    }

    m_excludeRules = newRules;
    saveExcludeRulesToSettings();
    m_fileMonitor.setExcludeRules(m_excludeRules);
    appendLogMessage(tr("Обновлены исключения (%1 правил)").arg(m_excludeRules.size()));
}

void MainWindow::showFaqDialog() {
    const QString faqText = tr(
        "<b>File Integrity Monitor</b><br><br>"
        "Программа предназначена для контроля целостности файлов и каталогов.<br>"
        "Она позволяет выявлять изменения содержимого файлов и их свойств.<br><br>"
        "<b>Как пользоваться:</b><br>"
        "1. Добавьте директории через кнопку на панели или меню \"Файл\".<br>"
        "2. Нажмите \"Сканировать\" (F5), чтобы выполнить проверку файлов.<br>"
        "3. Ознакомьтесь с результатами в таблице состояния файлов.<br>"
        "4. Используйте фильтры и поиск для анализа изменений.<br>"
        "5. Историю сканирований можно просматривать или очистить при необходимости.<br><br>"
        "Программа работает локально и не изменяет файлы на диске."
        );

    QMessageBox::information(this, tr("FAQ"), faqText);
}
void MainWindow::scanOnce() { beginScan(ScanTrigger::Manual); }

void MainWindow::beginScan(ScanTrigger trigger) {
    if (m_scanInProgress) {
        return;
    }

    const bool triggeredByTimer = trigger == ScanTrigger::Scheduled;

    if (m_dirList->count() == 0) {
        if (triggeredByTimer) {
            appendLogMessage(tr("Пропуск фонового сканирования: нет директорий"));
            scheduleNextScan();
        } else {
            QMessageBox::information(this, tr("Нет директорий"), tr("Добавьте хотя бы одну директорию для сканирования."));
        }
        return;
    }

    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
        m_scanThread->deleteLater();
        m_scanThread = nullptr;
        m_scanWorker = nullptr;
    }

    m_scanThread = new QThread(this);
    m_scanWorker = new ScanWorker(m_databasePath,
                                  m_hmacKey,
                                  m_excludeRules,
                                  m_recursiveOption,
                                  m_followSymlinksOption,
                                  m_maxDepthOption);
    m_scanWorker->moveToThread(m_scanThread);

    connect(m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);
    connect(m_scanWorker, &ScanWorker::scanFinished, this, &MainWindow::handleScanFinished);
    connect(m_scanWorker, &ScanWorker::scanError, this, &MainWindow::handleScanError);
    connect(m_scanWorker, &ScanWorker::progressChanged, this, &MainWindow::handleScanProgress);
    connect(m_scanWorker, &ScanWorker::fileProcessed, this, &MainWindow::handleScanFile);

    m_scanThread->start();

    m_scanInProgress = true;
    updateActionAvailability();
    updateProgressLabel(0, 0);
    statusBar()->showMessage(triggeredByTimer ? tr("Фоновое сканирование...") : tr("Сканирование..."));
    m_lastScan = QDateTime::currentDateTime();

    QStringList dirs;
    for (int i = 0; i < m_dirList->count(); ++i) {
        dirs << m_dirList->item(i)->text();
    }

    QMetaObject::invokeMethod(m_scanWorker, "startScan", Qt::QueuedConnection, Q_ARG(QStringList, dirs));
}

void MainWindow::clearHistory() {
    if (QMessageBox::question(this, tr("Очистить историю"), tr("Удалить все записи в базе?")) != QMessageBox::Yes) {
        return;
    }

    if (!m_databaseManager.clearAllRecords()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось очистить баз."));
        return;
    }

    m_allResults.clear();
    m_tableModel->removeRows(0, m_tableModel->rowCount());
    m_historyModel->removeRows(0, m_historyModel->rowCount());
    m_lastScan = {};
    appendLogMessage(tr("История очищена"));
    m_statsLabel->setText(tr("Файлов: 0"));
    m_lastScanLabel->setText(tr("Последняя проверка: —"));
    updateStatusBar();
}

void MainWindow::exportReport() {
    QString selectedFilter;
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Сохранить отчёт"),
        QString(),
        tr("CSV файлы (*.csv);;JSON файлы (*.json);;Все файлы (*.*)"),
        &selectedFilter);

    if (filePath.isEmpty()) {
        return;
    }

    enum class ExportFormat { Csv, Json };
    ExportFormat format = ExportFormat::Csv;
    if (filePath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive) || selectedFilter.contains(QLatin1String("JSON"))) {
        format = ExportFormat::Json;
    }

    const QAbstractItemModel *model = m_tableView->model();
    QJsonArray jsonArray;
    QStringList csvLines;
    csvLines << QStringLiteral("\"Path\";\"Status\";\"Size\";\"Permissions\";\"Hash\";\"LastCheck\"");

    const int rowCount = model->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        QModelIndex proxyIndex = model->index(row, 0);
        QModelIndex sourceIndex = proxyIndex;
        if (auto *proxy = qobject_cast<const QSortFilterProxyModel *>(model)) {
            sourceIndex = proxy->mapToSource(proxyIndex);
        }

        const QString path = m_tableModel->item(sourceIndex.row(), 0)->text();
        const QString statusText = m_tableModel->item(sourceIndex.row(), 1)->text();
        const auto it = std::find_if(m_allResults.cbegin(), m_allResults.cend(), [&path](const FileRecordEntry &entry) {
            return entry.metadata.path == path;
        });
        if (it == m_allResults.cend()) {
            continue;
        }

        const QString sizeText = QString::number(it->metadata.size);
        const QString hash = it->metadata.hash;
        const QString permissions = formatPermissionInfo(*it);
        const QString lastCheck = it->updatedAt.toLocalTime().toString(Qt::ISODate);

        if (format == ExportFormat::Csv) {
            auto quote = [](const QString &value) {
                QString escaped = value;
                escaped.replace('"', "\"\"");
                return QStringLiteral("\"%1\"").arg(escaped);
            };
            csvLines << QStringList{quote(path), quote(statusText), quote(sizeText), quote(permissions), quote(hash), quote(lastCheck)}.join(QLatin1Char(';'));
        } else {
            QJsonObject obj;
            obj.insert(QStringLiteral("path"), path);
            obj.insert(QStringLiteral("status"), statusText);
            obj.insert(QStringLiteral("size"), static_cast<qint64>(it->metadata.size));
            obj.insert(QStringLiteral("permissions"), permissions);
            obj.insert(QStringLiteral("hash"), hash);
            obj.insert(QStringLiteral("lastCheck"), lastCheck);
            jsonArray.append(obj);
        }
    }

    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось открыть файл для записи."));
        return;
    }

    if (format == ExportFormat::Csv) {
        QTextStream stream(&outFile);
        for (const auto &line : csvLines) {
            stream << line << '\n';
        }
    } else {
        const QJsonDocument doc(jsonArray);
        outFile.write(doc.toJson(QJsonDocument::Indented));
    }

    appendLogMessage(tr("Отчёт успешно сохранён в %1").arg(filePath));
    statusBar()->showMessage(tr("Отчёт экспортирован: %1").arg(filePath), 5000);
}

void MainWindow::onStatusFilterChanged(int index) {
    const int value = m_statusFilter->itemData(index).toInt();
    static_cast<FileFilterProxyModel *>(m_proxyModel)->setStatusFilterValue(value);
}

void MainWindow::onSearchTextChanged(const QString &text) {
    static_cast<FileFilterProxyModel *>(m_proxyModel)->setSearchTerm(text);
}

void MainWindow::onHistoryFilterChanged(int index) {
    const int value = m_historyStatusFilter->itemData(index).toInt();
    static_cast<HistoryFilterProxyModel *>(m_historyProxy)->setStatusFilterValue(value);
}

void MainWindow::onHistorySearchChanged(const QString &text) {
    static_cast<HistoryFilterProxyModel *>(m_historyProxy)->setSearchTerm(text);
}

void MainWindow::openSelectedFile(const QModelIndex &index) {
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    const QString path = m_tableModel->item(sourceIndex.row(), 0)->text();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::populateCurrentRecords() {
    const auto records = m_databaseManager.fetchAllRecords();
    m_allResults = records;
    rebuildTable();
    updateStatusBar();
}

void MainWindow::appendResults(const QVector<FileRecordEntry> &results) {
    for (const auto &rec : results) {
        auto it = std::find_if(m_allResults.begin(), m_allResults.end(), [&rec](const FileRecordEntry &existing) {
            return existing.metadata.path == rec.metadata.path;
        });

        if (it != m_allResults.end()) {
            *it = rec;
        } else {
            m_allResults.append(rec);
        }
    }
}

void MainWindow::reloadHistory() {
    m_historyModel->removeRows(0, m_historyModel->rowCount());
    const auto history = m_databaseManager.fetchHistory();

    for (const auto &item : history) {
        QList<QStandardItem *> items;
        auto *timeItem = new QStandardItem(item.scanTime.toLocalTime().toString(Qt::ISODate));
        auto *pathItem = new QStandardItem(item.filePath);

        const auto statusFromCode = [this](int code) {
            switch (code) {
            case 0:
                return QStringLiteral("Ok");
            case 1:
                return QStringLiteral("Changed");
            case 2:
                return QStringLiteral("New");
            case 3:
                return QStringLiteral("Deleted");
            case 4:
                return QStringLiteral("Error");
            default:
                return QStringLiteral("Ok");
            }
        };

        const QString newStatusRaw = statusFromCode(item.newStatus);
        const QString oldStatusRaw = item.oldStatus >= 0 ? statusFromCode(item.oldStatus) : QString();

        auto *newStatusItem = new QStandardItem(statusDisplayText(newStatusRaw));
        newStatusItem->setData(item.newStatus, Qt::UserRole + 1);
        auto *oldStatusItem = new QStandardItem(oldStatusRaw.isEmpty() ? tr("—") : statusDisplayText(oldStatusRaw));
        auto *commentItem = new QStandardItem(item.comment.isEmpty() ? tr("—") : item.comment);

        const QColor color = statusColor(newStatusRaw);
        for (auto *itemPtr : {timeItem, pathItem, newStatusItem, oldStatusItem, commentItem}) {
            itemPtr->setData(color, Qt::ForegroundRole);
        }

        items << timeItem << pathItem << newStatusItem << oldStatusItem << commentItem;
        m_historyModel->appendRow(items);
    }
}

void MainWindow::rebuildTable() {
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (const auto &rec : m_allResults) {
        const QString status = readableStatus(rec.status);
        QList<QStandardItem *> items;

        auto *pathItem = new QStandardItem(rec.metadata.path);
        pathItem->setData(rec.metadata.path, Qt::UserRole);
        items << pathItem;

        QString statusText = statusDisplayText(status);
        const QString detail = rec.errorReason.isEmpty() ? rec.metadata.errorReason : rec.errorReason;
        if (status == QLatin1String("Error") && detail.contains(QStringLiteral("Недостаточно прав"), Qt::CaseInsensitive)) {
            statusText = tr("Недостаточно прав");
        }

        auto *statusItem = new QStandardItem(statusText);
        statusItem->setData(statusValue(status), Qt::UserRole + 1);
        const QColor color = statusColor(status);
        statusItem->setData(color, Qt::ForegroundRole);
        pathItem->setData(color, Qt::ForegroundRole);
        items << statusItem;

        auto *permissionItem = new QStandardItem(formatPermissionInfo(rec));
        permissionItem->setData(color, Qt::ForegroundRole);
        items << permissionItem;

        items << new QStandardItem(rec.metadata.hash);
        const QString previousHash = rec.previousHash.isEmpty() ? m_databaseManager.fetchHash(rec.metadata.path) : rec.previousHash;
        items << new QStandardItem(previousHash.isEmpty() ? QStringLiteral("—") : previousHash);
        items << new QStandardItem(rec.updatedAt.toLocalTime().toString(Qt::ISODate));
        for (auto *item : items) {
            item->setData(color, Qt::ForegroundRole);
        }

        m_tableModel->appendRow(items);
    }
}

QString MainWindow::readableStatus(const QString &raw) const {
    if (raw.isEmpty()) {
        return QStringLiteral("Ok");
    }
    return raw;
}

int MainWindow::statusValue(const QString &status) const {
    if (status == QLatin1String("Changed")) {
        return 1;
    }
    if (status == QLatin1String("New")) {
        return 2;
    }
    if (status == QLatin1String("Deleted")) {
        return 3;
    }
    if (status == QLatin1String("Error")) {
        return 4;
    }
    return 0;
}

core::ScanSummary MainWindow::calculateSummary(const QVector<FileRecordEntry> &results) const {
    core::ScanSummary summary;
    summary.totalFiles = results.size();

    for (const auto &rec : results) {
        const QString status = readableStatus(rec.status);
        if (status == QLatin1String("Error")) {
            ++summary.errorCount;
        } else if (status == QLatin1String("Deleted")) {
            ++summary.deletedCount;
        } else if (status == QLatin1String("Changed")) {
            ++summary.changedCount;
        } else if (status == QLatin1String("New")) {
            ++summary.newCount;
        }
    }

    return summary;
}

void MainWindow::updateStatusBar() {
    int changedCount = 0;
    int newCount = 0;
    int deletedCount = 0;
    int errorCount = 0;
    for (const auto &rec : m_allResults) {
        const QString status = readableStatus(rec.status);
        if (status == QLatin1String("Error")) {
            ++errorCount;
        } else if (status == QLatin1String("Deleted")) {
            ++deletedCount;
        } else if (status == QLatin1String("Changed")) {
            ++changedCount;
        } else if (status == QLatin1String("New")) {
            ++newCount;
        }
    }

    m_statsLabel->setText(tr("Файлов: %1 | Изменено: %2 | Новые: %3 | Ошибки: %4 | Удалено: %5")
                              .arg(m_allResults.size())
                              .arg(changedCount)
                              .arg(newCount)
                              .arg(errorCount)
                              .arg(deletedCount));
    if (m_lastScan.isValid()) {
        m_lastScanLabel->setText(tr("Последняя проверка: %1").arg(m_lastScan.toString(Qt::ISODate)));
    } else {
        m_lastScanLabel->setText(tr("Последняя проверка: —"));
    }
}

void MainWindow::appendLogMessage(const QString &message) {
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void MainWindow::loadMonitoredDirsFromSettings() {
    const QStringList dirs = m_settings.value(QStringLiteral("monitoredDirectories")).toStringList();

    m_dirList->clear();
    for (const auto &dir : dirs) {
        if (dir.isEmpty()) {
            continue;
        }
        m_dirList->addItem(dir);
    }
}

void MainWindow::saveMonitoredDirsToSettings() {
    QStringList dirs;
    for (int i = 0; i < m_dirList->count(); ++i) {
        dirs << m_dirList->item(i)->text();
    }
    m_settings.setValue(QStringLiteral("monitoredDirectories"), dirs);
}

void MainWindow::loadExcludeRulesFromSettings() {
    m_excludeRules.clear();
    const QStringList rules = m_settings.value(QStringLiteral("excludeRules")).toStringList();

    for (const auto &entry : rules) {
        if (entry.startsWith(QStringLiteral("path:"))) {
            ExcludeRule rule{ExcludeType::Path, entry.mid(5)};
            m_excludeRules.append(rule);
        } else if (entry.startsWith(QStringLiteral("glob:"))) {
            ExcludeRule rule{ExcludeType::Glob, entry.mid(5)};
            m_excludeRules.append(rule);
        }
    }
}

void MainWindow::saveExcludeRulesToSettings() {
    QStringList rules;
    for (const auto &rule : m_excludeRules) {
        const QString prefix = rule.type == ExcludeType::Path ? QStringLiteral("path:") : QStringLiteral("glob:");
        rules << prefix + rule.pattern;
    }

    m_settings.setValue(QStringLiteral("excludeRules"), rules);
}

void MainWindow::loadScanOptions() {
    m_intervalSpin->setValue(m_settings.value(QStringLiteral("intervalSeconds"), 300).toInt());
    m_recursiveOption = m_settings.value(QStringLiteral("recursive"), true).toBool();
    m_followSymlinksOption = m_settings.value(QStringLiteral("followSymlinks"), false).toBool();
    m_maxDepthOption = m_settings.value(QStringLiteral("maxDepth"), 20).toInt();
    if (m_settings.contains(QStringLiteral("monitoringEnabled"))) {
        m_monitoringEnabled = m_settings.value(QStringLiteral("monitoringEnabled"), false).toBool();
    }
    updateMonitoringUi();
}

void MainWindow::saveScanOptions() {
    m_settings.setValue(QStringLiteral("intervalSeconds"), m_intervalSpin->value());
    m_settings.setValue(QStringLiteral("recursive"), m_recursiveOption);
    m_settings.setValue(QStringLiteral("followSymlinks"), m_followSymlinksOption);
    m_settings.setValue(QStringLiteral("maxDepth"), m_maxDepthOption);
    m_settings.sync();
    scheduleNextScan();
}

void MainWindow::saveMonitoringState() {
    m_settings.setValue(QStringLiteral("monitoringEnabled"), m_monitoringEnabled);
    m_settings.sync();
}

void MainWindow::ensureDefaultSettings() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    if (!m_settings.contains(QStringLiteral("databasePath"))) {
        m_settings.setValue(QStringLiteral("databasePath"), defaultDatabasePath());
    }
    if (!m_settings.contains(QStringLiteral("intervalSeconds"))) {
        m_settings.setValue(QStringLiteral("intervalSeconds"), 300);
    }
    if (!m_settings.contains(QStringLiteral("recursive"))) {
        m_settings.setValue(QStringLiteral("recursive"), true);
    }
    if (!m_settings.contains(QStringLiteral("followSymlinks"))) {
        m_settings.setValue(QStringLiteral("followSymlinks"), false);
    }
    if (!m_settings.contains(QStringLiteral("maxDepth"))) {
        m_settings.setValue(QStringLiteral("maxDepth"), 20);
    }
    if (!m_settings.contains(QStringLiteral("monitoringEnabled"))) {
        m_settings.setValue(QStringLiteral("monitoringEnabled"), false);
    }
    m_settings.sync();
}

QString MainWindow::defaultDatabasePath() const {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return QDir(dataDir).filePath(QStringLiteral("integrity.db"));
}

void MainWindow::scheduleNextScan() {
    if (!m_scanTimer) {
        return;
    }
    m_scanTimer->stop();
    if (!m_monitoringEnabled || m_intervalSpin->value() <= 0 || m_scanInProgress) {
        return;
    }
    m_scanTimer->start(m_intervalSpin->value() * 1000);
    if (m_trayPauseAction) {
        m_trayPauseAction->setText(tr("Пауза мониторинга"));
    }
}

void MainWindow::updateMonitoringUi() {
    const QString label = m_monitoringEnabled ? tr("Мониторинг: Включен") : tr("Мониторинг: Выключен");
    if (m_monitoringToggleAction) {
        m_monitoringToggleAction->setText(label);
        m_monitoringToggleAction->setChecked(m_monitoringEnabled);
    }
    if (m_trayPauseAction) {
        m_trayPauseAction->setText(m_monitoringEnabled ? tr("Пауза мониторинга") : tr("Возобновить мониторинг"));
    }
}

void MainWindow::updateActionAvailability() {
    const bool idle = !m_scanInProgress;
    if (m_scanAction) {
        m_scanAction->setEnabled(idle);
    }
    if (m_monitoringToggleAction) {
        m_monitoringToggleAction->setEnabled(idle);
    }
    if (m_trayScanAction) {
        m_trayScanAction->setEnabled(idle);
    }
}

void MainWindow::rescanSingleFile(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Файл не найден"), tr("Невозможно пересканировать: файл не существует."));
        return;
    }

    const auto results = m_fileMonitor.scanDirectory(info.absolutePath(), false, false, 1);
    const int total = results.size();
    for (int index = 0; index < total; ++index) {
        updateProgressLabel(index + 1, total);
    }

    auto it = std::find_if(results.cbegin(), results.cend(), [&path](const FileRecordEntry &rec) {
        return rec.metadata.path == path;
    });
    if (it != results.cend()) {
        appendResults({*it});
        rebuildTable();
        reloadHistory();
        updateStatusBar();
        appendLogMessage(tr("Пересканирован файл: %1").arg(path));
    }
}

QString MainWindow::statusDisplayText(const QString &status) const {
    if (status == QLatin1String("Error")) {
        return tr("Ошибка");
    }
    if (status == QLatin1String("Changed")) {
        return tr("Изменён");
    }
    if (status == QLatin1String("New")) {
        return tr("Новый");
    }
    if (status == QLatin1String("Deleted")) {
        return tr("Удалён");
    }
    return tr("Без изменений");
}

QColor MainWindow::statusColor(const QString &status) const {
    if (status == QLatin1String("Ok")) {
        return QColor(QStringLiteral("#4CAF50"));
    }
    if (status == QLatin1String("Changed")) {
        return QColor(QStringLiteral("#FF9800"));
    }
    if (status == QLatin1String("New")) {
        return QColor(QStringLiteral("#2196F3"));
    }
    if (status == QLatin1String("Deleted")) {
        return QColor(QStringLiteral("#607D8B"));
    }
    if (status == QLatin1String("Error")) {
        return QColor(QStringLiteral("#F44336"));
    }
    return palette().color(QPalette::Text);
}

QString MainWindow::formatPermissionInfo(const FileRecordEntry &rec) const {
    const QString owner = rec.metadata.owner.isEmpty() ? QString::number(rec.metadata.uid) : rec.metadata.owner;
    const QString group = rec.metadata.groupName.isEmpty() ? QString::number(rec.metadata.gid) : rec.metadata.groupName;
    const auto perms = static_cast<QFile::Permissions>(static_cast<uint>(rec.metadata.permissions));

    auto flag = [&perms](QFileDevice::Permission p, QChar ch) {
        return perms.testFlag(p) ? ch : QChar('-');
    };

    QString permString;
    permString.append(flag(QFile::ReadOwner, 'r'));
    permString.append(flag(QFile::WriteOwner, 'w'));
    permString.append(flag(QFile::ExeOwner, 'x'));
    permString.append(flag(QFile::ReadGroup, 'r'));
    permString.append(flag(QFile::WriteGroup, 'w'));
    permString.append(flag(QFile::ExeGroup, 'x'));
    permString.append(flag(QFile::ReadOther, 'r'));
    permString.append(flag(QFile::WriteOther, 'w'));
    permString.append(flag(QFile::ExeOther, 'x'));

    return QStringLiteral("%1:%2 %3").arg(owner, group, permString);
}

void MainWindow::handleScanFinished(const QVector<FileRecordEntry> &results) {
    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
        m_scanThread->deleteLater();
        m_scanThread = nullptr;
        m_scanWorker = nullptr;
    }

    m_scanInProgress = false;
    updateActionAvailability();

    appendResults(results);
    rebuildTable();
    reloadHistory();
    updateStatusBar();

    const auto summary = calculateSummary(results);
    statusBar()->showMessage(tr("Сканирование завершено: %1").arg(m_statsLabel->text()), 5000);
    appendLogMessage(tr("Скан завершён. Изменено: %1, новые: %2, удалено: %3, ошибки: %4")
                         .arg(summary.changedCount)
                         .arg(summary.newCount)
                         .arg(summary.deletedCount)
                         .arg(summary.errorCount));
    showSummaryNotification(summary);
    scheduleNextScan();
}

void MainWindow::handleScanError(const QString &message) {
    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
        m_scanThread->deleteLater();
        m_scanThread = nullptr;
        m_scanWorker = nullptr;
    }

    m_scanInProgress = false;
    updateActionAvailability();
    statusBar()->showMessage(tr("Ошибка сканирования"), 5000);
    QMessageBox::warning(this, tr("Ошибка сканирования"), message);
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(tr("Ошибка сканирования"), message, QSystemTrayIcon::Warning);
    }
    scheduleNextScan();
}

void MainWindow::handleScanProgress(int current, int total) {
    updateProgressLabel(current, total);
}

void MainWindow::updateProgressLabel(int current, int total) {
    const int safeTotal = total > 0 ? total : 0;
    const int clampedCurrent = safeTotal > 0 ? std::max(0, std::min(current, safeTotal)) : std::max(0, current);
    const int percent = safeTotal > 0 ? static_cast<int>((static_cast<double>(clampedCurrent) / safeTotal) * 100.0) : 0;
    m_progressLabel->setText(tr("Обработано: %1 / %2 (%3%)").arg(clampedCurrent).arg(safeTotal).arg(percent));
}

void MainWindow::handleScanFile(const QString &path) {
    appendLogMessage(tr("Обработан файл: %1").arg(path));
}

void MainWindow::configureFileTableHeaders() {
    auto *header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);

    const int columnCount = m_tableModel ? m_tableModel->columnCount() : 0;
    for (int i = 0; i < columnCount; ++i) {
        if (i == 0) {
            header->setSectionResizeMode(i, QHeaderView::Stretch);
        } else {
            header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
    }
}

void MainWindow::configureHistoryTableHeaders() {
    auto *header = m_historyView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);

    const int columnCount = m_historyModel ? m_historyModel->columnCount() : 0;
    for (int i = 0; i < columnCount; ++i) {
        if (i == 1) {
            header->setSectionResizeMode(i, QHeaderView::Stretch);
        } else {
            header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
    }
}

void MainWindow::triggerMonitoringTick() {
    if (!m_monitoringEnabled) {
        return;
    }
    if (m_scanInProgress) {
        scheduleNextScan();
        return;
    }
    appendLogMessage(tr("Фоновое сканирование запущено"));
    beginScan(ScanTrigger::Scheduled);
}

void MainWindow::toggleMonitoring() {
    if (m_monitoringEnabled) {
        stopMonitoring();
    } else {
        startMonitoring();
    }
}

void MainWindow::startMonitoring() {
    if (m_monitoringEnabled) {
        return;
    }
    m_monitoringEnabled = true;
    saveMonitoringState();
    updateMonitoringUi();
    scheduleNextScan();
}

void MainWindow::stopMonitoring() {
    if (!m_monitoringEnabled) {
        return;
    }
    m_monitoringEnabled = false;
    if (m_scanTimer) {
        m_scanTimer->stop();
    }
    saveMonitoringState();
    updateMonitoringUi();
}

void MainWindow::pauseOrResumeMonitoring() {
    if (m_monitoringEnabled) {
        stopMonitoring();
        appendLogMessage(tr("Фоновый мониторинг приостановлен"));
    } else {
        startMonitoring();
        appendLogMessage(tr("Фоновый мониторинг возобновлён"));
    }
}

void MainWindow::setupTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("security-medium")), this);
    m_trayIcon->setToolTip(tr("File Integrity Monitor"));

    auto *showAction = new QAction(tr("Открыть"), this);
    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);

    m_trayScanAction = new QAction(tr("Запустить сканирование сейчас"), this);
    connect(m_trayScanAction, &QAction::triggered, this, &MainWindow::scanOnce);

    m_trayPauseAction = new QAction(this);
    connect(m_trayPauseAction, &QAction::triggered, this, &MainWindow::pauseOrResumeMonitoring);
    m_trayPauseAction->setText(m_monitoringEnabled ? tr("Пауза мониторинга") : tr("Возобновить мониторинг"));

    auto *quitAction = new QAction(tr("Выход"), this);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_forceExit = true;
        QApplication::quit();
    });

    auto *trayMenu = new QMenu(this);
    trayMenu->addAction(showAction);
    trayMenu->addAction(m_trayScanAction);
    trayMenu->addAction(m_trayPauseAction);
    trayMenu->addSeparator();
    trayMenu->addAction(quitAction);
    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_forceExit || !m_trayIcon) {
        QMainWindow::closeEvent(event);
        return;
    }

    event->ignore();
    hide();
    if (m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(tr("File Integrity Monitor"),
                                tr("Приложение продолжает работать в фоновом режиме."),
                                QSystemTrayIcon::Information,
                                3000);
    }
}

void MainWindow::showFromTray() {
    show();
    raise();
    activateWindow();
}

void MainWindow::showSummaryNotification(const core::ScanSummary &summary) {
    if (!m_trayIcon || !m_trayIcon->isVisible()) {
        return;
    }

    if (summary.changedCount > 0 || summary.newCount > 0 || summary.deletedCount > 0) {
        const auto totalChanges = summary.changedCount + summary.newCount + summary.deletedCount;
        m_trayIcon->showMessage(tr("Обнаружены изменения"),
                                tr("Обнаружены изменения: %1 файлов").arg(totalChanges),
                                QSystemTrayIcon::Information,
                                4000);
    }

    if (summary.errorCount > 0) {
        m_trayIcon->showMessage(tr("Ошибки доступа"),
                                tr("Ошибки доступа: %1").arg(summary.errorCount),
                                QSystemTrayIcon::Warning,
                                4000);
    }
}
