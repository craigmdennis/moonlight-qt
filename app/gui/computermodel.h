#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>
#include <QHash>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        DetailsRole,
        StatusLineRole,
        SleepingRole
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    Q_INVOKABLE void renameComputer(int computerIndex, QString name);

    // Apollo only: suspends the PC. The tile shows a spinner for a few seconds, then
    // Offline, and discovery cannot bring it back Online while the PC finishes suspending.
    Q_INVOKABLE void sleepComputer(int computerIndex);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);

    // Called back from worker tasks on the GUI thread
    void finishSleep(const QString& uuid, bool accepted);
    void applyActivity(const QString& uuid, const QString& line);

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);

private slots:
    void refreshActivity();

    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    void fetchActivity(NvComputer* computer);
    void notifyRow(const QString& uuid);
    void setActivityLine(const QString& uuid, const QString& line);
    void showNotice(const QString& uuid, const QString& text);

    // Keyed by computer UUID
    QHash<QString, QString> m_ActivityLines;
    QHash<QString, QDateTime> m_ActivityFetched;
    QHash<QString, QDateTime> m_SleepingUntil;
    QHash<QString, QDateTime> m_OfflineUntil;
    QHash<QString, QString> m_Notices;
    QTimer* m_ActivityTimer = nullptr;

    QVector<NvComputer*> m_Computers;
    ComputerManager* m_ComputerManager;
};
