#include "Notifier.h"

#include <QDebug>
#include <QIcon>
#include <QSystemTrayIcon>

class TrayNotifier : public INotificationSink {
public:
    explicit TrayNotifier(QSystemTrayIcon *icon)
        : m_icon(icon) {}

    void notify(const ScanSummary &summary) override {
        if (!m_icon) {
            return;
        }

        QString title = QObject::tr("Проблемы целостности файлов");
        QString body = QObject::tr("Изменено: %1, удалено: %2, нарушена подпись: %3, метаданные: %4")
                            .arg(summary.modifiedCount)
                            .arg(summary.deletedCount)
                            .arg(summary.signatureErrorCount)
                            .arg(summary.metaChangedCount + summary.permissionChangedCount + summary.ownerChangedCount);

        if (summary.modifiedCount == 0 && summary.deletedCount == 0 && summary.signatureErrorCount == 0
            && summary.metaChangedCount == 0 && summary.permissionChangedCount == 0 && summary.ownerChangedCount == 0) {
            title = QObject::tr("Сканирование завершено");
            body = QObject::tr("Проблем не обнаружено");
        }

        m_icon->showMessage(title, body, QIcon(), 5000);
    }

private:
    QSystemTrayIcon *m_icon = nullptr;
};

class EmailNotifier : public INotificationSink {
public:
    void notify(const ScanSummary &summary) override {
#ifdef QT_DEBUG
        qDebug() << "TODO: send email notification" << summary.totalFiles;
#else
        Q_UNUSED(summary);
#endif
    }
};

class TelegramNotifier : public INotificationSink {
public:
    void notify(const ScanSummary &summary) override {
#ifdef QT_DEBUG
        qDebug() << "TODO: send telegram notification" << summary.totalFiles;
#else
        Q_UNUSED(summary);
#endif
    }
};

class SyslogNotifier : public INotificationSink {
public:
    void notify(const ScanSummary &summary) override {
#ifdef QT_DEBUG
        qDebug() << "TODO: send syslog notification" << summary.totalFiles;
#else
        Q_UNUSED(summary);
#endif
    }
};

std::unique_ptr<INotificationSink> makeTrayNotifier(QSystemTrayIcon *icon) {
    return std::make_unique<TrayNotifier>(icon);
}

std::unique_ptr<INotificationSink> makeEmailNotifier() {
    return std::make_unique<EmailNotifier>();
}

std::unique_ptr<INotificationSink> makeTelegramNotifier() {
    return std::make_unique<TelegramNotifier>();
}

std::unique_ptr<INotificationSink> makeSyslogNotifier() {
    return std::make_unique<SyslogNotifier>();
}
