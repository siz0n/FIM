#ifndef NOTIFIER_H
#define NOTIFIER_H

#include <memory>
#include <vector>

struct ScanSummary {
    int totalFiles = 0;
    int modifiedCount = 0;
    int deletedCount = 0;
    int signatureErrorCount = 0;
    int newCount = 0;
    int metaChangedCount = 0;
    int permissionChangedCount = 0;
    int ownerChangedCount = 0;
};

class INotificationSink {
public:
    virtual ~INotificationSink() = default;
    virtual void notify(const ScanSummary &summary) = 0;
};

class TrayNotifier;
class EmailNotifier;
class TelegramNotifier;
class SyslogNotifier;
class QSystemTrayIcon;

std::unique_ptr<INotificationSink> makeTrayNotifier(class QSystemTrayIcon *icon);
std::unique_ptr<INotificationSink> makeEmailNotifier();
std::unique_ptr<INotificationSink> makeTelegramNotifier();
std::unique_ptr<INotificationSink> makeSyslogNotifier();

#endif // NOTIFIER_H
