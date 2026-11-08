#include "computermodel.h"

#include "backend/nvhttp.h"

#include <QThreadPool>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QJsonObject>
#include <QPointer>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

// How long the spinner shows after the PC accepts the sleep command, and how long the
// tile then stays Offline while the PC finishes suspending and still answers discovery.
// Same values as the Apple TV app.
static const int SLEEP_SPINNER_MS = 4000;
static const int SLEEP_OFFLINE_HOLD_MS = 20000;
static const int ACTIVITY_REFRESH_MS = 60000;

static QString relativeWhen(qint64 unixSeconds)
{
    qint64 delta = QDateTime::currentSecsSinceEpoch() - unixSeconds;
    if (delta < 90) return QObject::tr("just now");
    if (delta < 3600) return QObject::tr("%n minute(s) ago", "", int(delta / 60));
    if (delta < 86400) return QObject::tr("%n hour(s) ago", "", int(delta / 3600));
    if (delta < 2 * 86400) return QObject::tr("yesterday");
    return QObject::tr("%n day(s) ago", "", int(delta / 86400));
}

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);

    m_Computers = m_ComputerManager->getComputers();

    m_ActivityTimer = new QTimer(this);
    connect(m_ActivityTimer, &QTimer::timeout, this, &ComputerModel::refreshActivity);
    m_ActivityTimer->start(ACTIVITY_REFRESH_MS);
    refreshActivity();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        // A PC that was just told to sleep keeps answering discovery for a while
        if (m_OfflineUntil.value(computer->uuid) > QDateTime::currentDateTime()) {
            return false;
        }
        return computer->state == NvComputer::CS_ONLINE;
    case BackdropRole: {
        QString path = m_Backdrops.value(computer->uuid);
        return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
    }
    case SleepingRole:
        return m_SleepingUntil.value(computer->uuid) > QDateTime::currentDateTime();
    case StatusLineRole: {
        if (m_SleepingUntil.value(computer->uuid) > QDateTime::currentDateTime()) {
            return tr("Sleeping…");
        }
        if (m_Notices.contains(computer->uuid)) {
            return m_Notices.value(computer->uuid);
        }
        return m_ActivityLines.value(computer->uuid);
    }
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        return tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[DetailsRole] = "details";
    names[StatusLineRole] = "statusLine";
    names[SleepingRole] = "sleeping";
    names[BackdropRole] = "backdrop";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(m_Computers[computerIndex]);
    QThreadPool::globalInstance()->start(wakeTask);
}

class DeferredSleepHostTask : public QRunnable
{
public:
    DeferredSleepHostTask(ComputerModel* model, QString uuid, NvAddress address, uint16_t httpsPort,
                          QSslCertificate cert, bool useTrueUid)
        : m_Model(model), m_Uuid(uuid), m_Address(address), m_HttpsPort(httpsPort),
          m_Cert(cert), m_UseTrueUid(useTrueUid) {}

    void run()
    {
        bool ok = true;
        try {
            NvHTTP http(m_Address, m_HttpsPort, m_Cert, m_UseTrueUid);
            http.sleepHost();
        } catch (...) {
            ok = false;
        }

        QPointer<ComputerModel> model = m_Model;
        QString uuid = m_Uuid;
        if (model) {
            QMetaObject::invokeMethod(model.data(), [model, uuid, ok]() {
                if (model) {
                    model->finishSleep(uuid, ok);
                }
            });
        }
    }

private:
    ComputerModel* m_Model;
    QString m_Uuid;
    NvAddress m_Address;
    uint16_t m_HttpsPort;
    QSslCertificate m_Cert;
    bool m_UseTrueUid;
};

void ComputerModel::sleepComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];
    QString uuid;
    NvAddress address;
    uint16_t httpsPort;
    QSslCertificate cert;
    bool useTrueUid;
    {
        QReadLocker lock(&computer->lock);
        // Never interrupt someone else's stream
        if (computer->state != NvComputer::CS_ONLINE || computer->currentGameId != 0) {
            return;
        }
        uuid = computer->uuid;
        address = computer->activeAddress;
        httpsPort = computer->activeHttpsPort;
        cert = computer->serverCert;
        useTrueUid = !computer->isNvidiaServerSoftware;
    }

    // Spinner while the request is in flight
    m_SleepingUntil[uuid] = QDateTime::currentDateTime().addMSecs(60000);
    m_Notices.remove(uuid);
    emit dataChanged(createIndex(computerIndex, 0), createIndex(computerIndex, 0));

    QThreadPool::globalInstance()->start(
        new DeferredSleepHostTask(this, uuid, address, httpsPort, cert, useTrueUid));
}

void ComputerModel::finishSleep(const QString& uuid, bool accepted)
{
    if (!accepted) {
        m_SleepingUntil.remove(uuid);
        showNotice(uuid, tr("Couldn’t put the PC to sleep · Apollo may not allow this device"));
        return;
    }

    // Keep the spinner up briefly so the action reads as happening, then show Offline and
    // hold it there while the PC finishes suspending
    QDateTime now = QDateTime::currentDateTime();
    m_SleepingUntil[uuid] = now.addMSecs(SLEEP_SPINNER_MS);
    m_OfflineUntil[uuid] = now.addMSecs(SLEEP_OFFLINE_HOLD_MS);
    m_ActivityLines.remove(uuid);
    notifyRow(uuid);
    QTimer::singleShot(SLEEP_SPINNER_MS + 50, this, [this, uuid]() { notifyRow(uuid); });
    QTimer::singleShot(SLEEP_OFFLINE_HOLD_MS + 50, this, [this, uuid]() { notifyRow(uuid); });
}

void ComputerModel::notifyRow(const QString& uuid)
{
    for (int i = 0; i < m_Computers.count(); i++) {
        if (m_Computers[i]->uuid == uuid) {
            emit dataChanged(createIndex(i, 0), createIndex(i, 0));
            return;
        }
    }
}

void ComputerModel::showNotice(const QString& uuid, const QString& text)
{
    m_Notices[uuid] = text;
    notifyRow(uuid);
    QTimer::singleShot(8000, this, [this, uuid, text]() {
        if (m_Notices.value(uuid) == text) {
            m_Notices.remove(uuid);
            notifyRow(uuid);
        }
    });
}

// Saves the PC's wallpaper under the cache folder and returns its path, or an empty string.
// The bytes are decoded as an image first, so only a real image of sane size is ever kept.
static QString fetchWallpaper(NvHTTP& http, const QString& uuid)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/backdrops";
    QDir().mkpath(dir);
    // The UUID comes from the host, so keep only characters safe in a file name
    QString safe = uuid;
    safe.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    QString path = dir + "/" + safe + ".jpg";

    QFileInfo info(path);
    if (info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < 3600) {
        return path;
    }

    try {
        QByteArray bytes = http.getWallpaper();
        QImage image;
        if (!image.loadFromData(bytes) || image.width() > 8192 || image.height() > 8192) {
            return info.exists() ? path : QString();
        }
        // Downscale: the backdrop is dimmed and blurred behind the grid
        if (image.width() > 1920) {
            image = image.scaledToWidth(1920, Qt::SmoothTransformation);
        }
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && image.save(&file, "JPG", 80) && file.commit()) {
            return path;
        }
    } catch (...) {
        // No Apollo, or no permission: no backdrop
    }
    return info.exists() ? path : QString();
}

class DeferredActivityTask : public QRunnable
{
public:
    DeferredActivityTask(ComputerModel* model, QString uuid, NvAddress address, uint16_t httpsPort,
                         QSslCertificate cert, bool useTrueUid)
        : m_Model(model), m_Uuid(uuid), m_Address(address), m_HttpsPort(httpsPort),
          m_Cert(cert), m_UseTrueUid(useTrueUid) {}

    void run()
    {
        QString line;
        QString backdropPath;
        try {
            NvHTTP http(m_Address, m_HttpsPort, m_Cert, m_UseTrueUid);
            backdropPath = fetchWallpaper(http, m_Uuid);
            QJsonObject o = QJsonDocument::fromJson(http.getSteamActivity().toUtf8()).object();
            if (o.value("sharing").toBool(true) == false) {
                // The host withholds activity from this device
            }
            else if (o.contains("steamInstalled") && !o.value("steamInstalled").toBool()) {
                line = QObject::tr("No Steam library on this PC");
            }
            else if (o.value("runningName").isString()) {
                line = QObject::tr("Currently playing %1").arg(o.value("runningName").toString());
            }
            else if (o.value("lastPlayed").isObject()) {
                QJsonObject g = o.value("lastPlayed").toObject();
                line = QObject::tr("Last played %1 · %2").arg(g.value("name").toString(),
                                                           relativeWhen(g.value("at").toVariant().toLongLong()));
            }
        } catch (...) {
            // A host without the Apollo fork answers 404 with XML: no line
        }

        QPointer<ComputerModel> model = m_Model;
        QString uuid = m_Uuid;
        if (model) {
            QMetaObject::invokeMethod(model.data(), [model, uuid, line, backdropPath]() {
                if (model) {
                    model->applyActivity(uuid, line, backdropPath);
                }
            });
        }
    }

private:
    ComputerModel* m_Model;
    QString m_Uuid;
    NvAddress m_Address;
    uint16_t m_HttpsPort;
    QSslCertificate m_Cert;
    bool m_UseTrueUid;
};

void ComputerModel::applyActivity(const QString& uuid, const QString& line, const QString& backdropPath)
{
    bool changed = false;
    if (m_ActivityLines.value(uuid) != line) {
        m_ActivityLines[uuid] = line;
        changed = true;
    }
    if (m_Backdrops.value(uuid) != backdropPath) {
        m_Backdrops[uuid] = backdropPath;
        changed = true;
    }
    if (changed) {
        notifyRow(uuid);
    }
}

void ComputerModel::refreshActivity()
{
    for (NvComputer* computer : m_Computers) {
        fetchActivity(computer);
    }
}

void ComputerModel::fetchActivity(NvComputer* computer)
{
    QString uuid;
    NvAddress address;
    uint16_t httpsPort;
    QSslCertificate cert;
    bool useTrueUid;
    {
        QReadLocker lock(&computer->lock);
        if (computer->state != NvComputer::CS_ONLINE || computer->pairState != NvComputer::PS_PAIRED ||
                computer->isNvidiaServerSoftware) {
            return;
        }
        uuid = computer->uuid;
        address = computer->activeAddress;
        httpsPort = computer->activeHttpsPort;
        cert = computer->serverCert;
        useTrueUid = true;
    }

    // At most one request per 30 s per computer, however often its state changes
    QDateTime now = QDateTime::currentDateTime();
    if (m_ActivityFetched.value(uuid).addSecs(30) > now) {
        return;
    }
    m_ActivityFetched[uuid] = now;

    QThreadPool::globalInstance()->start(
        new DeferredActivityTask(this, uuid, address, httpsPort, cert, useTrueUid));
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->pairHost(m_Computers[computerIndex], pin);
}

void ComputerModel::handlePairingCompleted(NvComputer*, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Reset the model if the structural layout of the list has changed
    if (m_Computers != newComputerList) {
        beginResetModel();
        m_Computers = newComputerList;
        endResetModel();
    }
    else {
        // Let the view know that this specific computer changed
        int index = m_Computers.indexOf(computer);
        emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }

    fetchActivity(computer);
}

#include "computermodel.moc"
