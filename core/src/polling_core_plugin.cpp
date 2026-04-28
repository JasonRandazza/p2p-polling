#include "polling_core_plugin.h"
#include "logos_sdk.h"
#include "liblogosdelivery.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

namespace {
constexpr const char* kPollTopic = "/logos-polling/1/votes/proto";

QByteArray jsonString(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray payloadBytesFromJson(const QJsonValue& value)
{
    if (value.isString()) {
        return QByteArray::fromBase64(value.toString().toUtf8());
    }

    if (!value.isArray()) {
        return {};
    }

    QByteArray bytes;
    const QJsonArray array = value.toArray();
    bytes.reserve(array.size());
    for (const QJsonValue& byteValue : array) {
        bytes.append(static_cast<char>(byteValue.toInt() & 0xff));
    }
    return bytes;
}

void ignoreDeliveryCallback(int, const char*, size_t, void*) {}
}

PollingCorePlugin::PollingCorePlugin(QObject* parent)
    : QObject(parent)
{
    m_votes.insert("Apples", 0);
    m_votes.insert("Bananas", 0);
    m_votes.insert("Oranges", 0);
    m_instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    loadVoteState();
}

PollingCorePlugin::~PollingCorePlugin()
{
    stopVoteBridge();
    shutdownDelivery();
    delete m_logos;
}

void PollingCorePlugin::initLogos(LogosAPI* api)
{
    if (m_logos) {
        delete m_logos;
        m_logos = nullptr;
    }

    logosAPI = api;
    if (logosAPI) {
        m_logos = new LogosModules(logosAPI);
    }

    initializeDelivery();
    startVoteBridge();

    qDebug() << "PollingCorePlugin: initialized";
}

QVariantMap PollingCorePlugin::submitVote(const QString& option)
{
    if (!isValidOption(option)) {
        QVariantMap error = snapshot();
        error.insert("ok", false);
        error.insert("error", QStringLiteral("Unknown poll option: %1").arg(option));
        return error;
    }

    const QString voteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    recordVote(option, QStringLiteral("local"), voteId);
    broadcastVote(option, voteId);

    QVariantMap result = snapshot();
    result.insert("ok", true);
    result.insert("selected", option);
    return result;
}

QVariantMap PollingCorePlugin::getVoteCounts()
{
    QVariantMap result = snapshot();
    result.insert("ok", true);
    return result;
}

QString PollingCorePlugin::getStatus()
{
    const int total = m_votes.value("Apples").toInt()
        + m_votes.value("Bananas").toInt()
        + m_votes.value("Oranges").toInt();
    return QStringLiteral("Polling core is tracking %1 votes. Delivery: %2. Blockchain: %3")
        .arg(total)
        .arg(m_networkStatus)
        .arg(m_chainStatus);
}

void PollingCorePlugin::deliveryCreateCallback(int ret, const char* msg, size_t len, void* userData)
{
    auto* plugin = static_cast<PollingCorePlugin*>(userData);
    if (!plugin) {
        return;
    }

    const QByteArray message = (msg && len > 0)
        ? QByteArray(msg, static_cast<qsizetype>(len))
        : QByteArray();

    QMetaObject::invokeMethod(plugin, [plugin, ret, message]() {
        plugin->handleDeliveryOperation(QStringLiteral("create node"), ret, message);
        if (ret == RET_OK) {
            plugin->startDeliveryNode();
        }
    }, Qt::QueuedConnection);
}

void PollingCorePlugin::deliveryStartCallback(int ret, const char* msg, size_t len, void* userData)
{
    auto* plugin = static_cast<PollingCorePlugin*>(userData);
    if (!plugin) {
        return;
    }

    const QByteArray message = (msg && len > 0)
        ? QByteArray(msg, static_cast<qsizetype>(len))
        : QByteArray();

    QMetaObject::invokeMethod(plugin, [plugin, ret, message]() {
        plugin->handleDeliveryOperation(QStringLiteral("start node"), ret, message);
        if (ret == RET_OK) {
            plugin->subscribeDeliveryTopic();
        }
    }, Qt::QueuedConnection);
}

void PollingCorePlugin::deliverySubscribeCallback(int ret, const char* msg, size_t len, void* userData)
{
    auto* plugin = static_cast<PollingCorePlugin*>(userData);
    if (!plugin) {
        return;
    }

    const QByteArray message = (msg && len > 0)
        ? QByteArray(msg, static_cast<qsizetype>(len))
        : QByteArray();

    QMetaObject::invokeMethod(plugin, [plugin, ret, message]() {
        plugin->handleDeliveryOperation(QStringLiteral("subscribe"), ret, message);
        if (ret == RET_OK) {
            plugin->updateNetworkStatus(QStringLiteral("Connected to Logos Delivery topic %1").arg(QString::fromUtf8(kPollTopic)), true);
        }
    }, Qt::QueuedConnection);
}

void PollingCorePlugin::deliveryEventCallback(int ret, const char* msg, size_t len, void* userData)
{
    auto* plugin = static_cast<PollingCorePlugin*>(userData);
    if (!plugin) {
        return;
    }

    const QByteArray message = (msg && len > 0)
        ? QByteArray(msg, static_cast<qsizetype>(len))
        : QByteArray();

    QMetaObject::invokeMethod(plugin, [plugin, ret, message]() {
        if (ret != RET_OK) {
            plugin->handleDeliveryOperation(QStringLiteral("event"), ret, message);
            return;
        }

        if (message.isEmpty()) {
            return;
        }

        plugin->processDeliveryEvent(message);
    }, Qt::QueuedConnection);
}

void PollingCorePlugin::deliveryOperationCallback(int ret, const char* msg, size_t len, void* userData)
{
    auto* plugin = static_cast<PollingCorePlugin*>(userData);
    if (!plugin) {
        return;
    }

    const QByteArray message = (msg && len > 0)
        ? QByteArray(msg, static_cast<qsizetype>(len))
        : QByteArray();

    QMetaObject::invokeMethod(plugin, [plugin, ret, message]() {
        plugin->handleDeliveryOperation(QStringLiteral("operation"), ret, message);
    }, Qt::QueuedConnection);
}

QVariantMap PollingCorePlugin::snapshot() const
{
    QVariantMap counts;
    counts.insert("Apples", m_votes.value("Apples").toInt());
    counts.insert("Bananas", m_votes.value("Bananas").toInt());
    counts.insert("Oranges", m_votes.value("Oranges").toInt());

    QVariantMap result;
    result.insert("counts", counts);
    result.insert("total", counts.value("Apples").toInt()
        + counts.value("Bananas").toInt()
        + counts.value("Oranges").toInt());
    result.insert("networkReady", m_networkReady);
    result.insert("networkStatus", m_networkStatus);
    result.insert("deliveryReady", m_networkReady);
    result.insert("deliveryStatus", m_networkStatus);
    result.insert("chainReady", m_chainReady);
    result.insert("chainStatus", m_chainStatus);
    return result;
}

bool PollingCorePlugin::isValidOption(const QString& option) const
{
    return m_votes.contains(option);
}

void PollingCorePlugin::initializeDelivery()
{
    if (m_delivery) {
        return;
    }

    const quint32 portsShift = QRandomGenerator::global()->bounded(1000, 24000);
    const QJsonObject config {
        { "logLevel", "INFO" },
        { "mode", "Core" },
        { "preset", "logos.dev" },
        { "portsShift", static_cast<int>(portsShift) }
    };

    const QByteArray configJson = jsonString(config);
    m_delivery = logosdelivery_create_node(configJson.constData(), deliveryCreateCallback, this);
    if (!m_delivery) {
        updateNetworkStatus(QStringLiteral("Logos Delivery failed to create a node"), false);
        return;
    }

    logosdelivery_set_event_callback(m_delivery, deliveryEventCallback, this);
    updateNetworkStatus(QStringLiteral("Starting Logos Delivery..."), false);
}

void PollingCorePlugin::startDeliveryNode()
{
    if (!m_delivery) {
        return;
    }

    const int startResult = logosdelivery_start_node(m_delivery, deliveryStartCallback, this);
    if (startResult != RET_OK) {
        updateNetworkStatus(QStringLiteral("Logos Delivery start request failed"), false);
    }
}

void PollingCorePlugin::subscribeDeliveryTopic()
{
    if (!m_delivery) {
        return;
    }

    const int subscribeResult = logosdelivery_subscribe(m_delivery, deliverySubscribeCallback, this, kPollTopic);
    if (subscribeResult != RET_OK) {
        updateNetworkStatus(QStringLiteral("Logos Delivery subscribe request failed"), false);
    }
}

void PollingCorePlugin::shutdownDelivery()
{
    if (!m_delivery) {
        return;
    }

    logosdelivery_unsubscribe(m_delivery, ignoreDeliveryCallback, reinterpret_cast<void*>(1), kPollTopic);
    logosdelivery_stop_node(m_delivery, ignoreDeliveryCallback, reinterpret_cast<void*>(1));
    logosdelivery_destroy(m_delivery, ignoreDeliveryCallback, reinterpret_cast<void*>(1));
    m_delivery = nullptr;
}

void PollingCorePlugin::handleDeliveryOperation(const QString& operation, int ret, const QByteArray& message)
{
    if (ret == RET_OK) {
        qDebug() << "PollingCorePlugin: Logos Delivery" << operation << "ok";
        return;
    }

    const QString error = message.isEmpty()
        ? QStringLiteral("Logos Delivery %1 failed").arg(operation)
        : QString::fromUtf8(message);
    updateNetworkStatus(error, false);
    qWarning() << "PollingCorePlugin:" << error;
}

void PollingCorePlugin::broadcastVote(const QString& option, const QString& voteId)
{
    // Waku P2P broadcast (fast, low-latency)
    if (m_delivery && m_networkReady) {
        qInfo() << "PollingCorePlugin: broadcasting Waku vote" << voteId << "for" << option;

        const QJsonObject vote {
            { "type", "vote" },
            { "id", voteId },
            { "sender", m_instanceId },
            { "option", option }
        };

        const QJsonObject envelope {
            { "contentTopic", QString::fromUtf8(kPollTopic) },
            { "payload", QString::fromUtf8(jsonString(vote).toBase64()) },
            { "ephemeral", false }
        };

        const QByteArray messageJson = jsonString(envelope);
        const int sendResult = logosdelivery_send(m_delivery, deliveryOperationCallback, this, messageJson.constData());
        if (sendResult != RET_OK) {
            qWarning() << "PollingCorePlugin: Waku vote send request failed" << voteId << "for" << option;
            updateNetworkStatus(QStringLiteral("Logos Delivery send request failed"), false);
        }
    } else {
        qInfo() << "PollingCorePlugin: skipped Waku vote broadcast" << voteId << "for" << option
                << "- deliveryReady=" << m_networkReady;
    }

    // Blockchain inscription (permanent, on-chain record)
    if (m_voteBridge && m_voteBridge->state() == QProcess::Running) {
        qInfo() << "PollingCorePlugin: publishing blockchain vote" << voteId << "for" << option;

        const QJsonObject cmd {
            { "cmd", "publish" },
            { "option", option },
            { "vote_id", voteId },
            { "sender", m_instanceId }
        };
        writeVoteBridge(jsonString(cmd) + '\n');
    } else {
        qInfo() << "PollingCorePlugin: skipped blockchain vote publish" << voteId << "for" << option
                << "- vote-bridge not running";
    }
}

void PollingCorePlugin::processDeliveryEvent(const QByteArray& eventJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(eventJson);
    if (!document.isObject()) {
        return;
    }

    const QJsonObject event = document.object();
    const QString eventType = event.value("eventType").toString();

    if (eventType == "message_received") {
        const QJsonObject message = event.value("message").toObject();
        if (message.value("contentTopic").toString() != QString::fromUtf8(kPollTopic)) {
            return;
        }

        processRemoteVote(payloadBytesFromJson(message.value("payload")));
        return;
    }

    if (eventType == "message_sent" || eventType == "message_propagated") {
        updateNetworkStatus(QStringLiteral("Logos Delivery active on %1").arg(QString::fromUtf8(kPollTopic)), true);
        return;
    }

    if (eventType == "connection_status_change") {
        const QString status = event.value("connectionStatus").toString(QStringLiteral("connection changed"));
        updateNetworkStatus(QStringLiteral("Logos Delivery %1").arg(status), true);
        return;
    }

    if (eventType == "message_error") {
        updateNetworkStatus(event.value("error").toString(QStringLiteral("Logos Delivery message error")), false);
    }
}

void PollingCorePlugin::processRemoteVote(const QByteArray& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return;
    }

    const QJsonObject vote = document.object();
    if (vote.value("type").toString() != "vote") {
        return;
    }

    const QString sender = vote.value("sender").toString();
    const QString voteId = vote.value("id").toString();
    const QString option = vote.value("option").toString();

    if (sender == m_instanceId) {
        qInfo() << "PollingCorePlugin: ignored own Waku vote" << voteId << "for" << option;
        return;
    }

    if (voteId.isEmpty()) {
        qWarning() << "PollingCorePlugin: ignored Waku vote with missing id for" << option;
        return;
    }

    if (m_seenVoteIds.contains(voteId)) {
        qInfo() << "PollingCorePlugin: ignored duplicate Waku vote" << voteId << "for" << option;
        return;
    }

    if (!isValidOption(option)) {
        qWarning() << "PollingCorePlugin: ignored Waku vote" << voteId << "with invalid option" << option;
        return;
    }

    qInfo() << "PollingCorePlugin: received Waku vote" << voteId << "for" << option << "from" << sender;
    recordVote(option, QStringLiteral("network"), voteId);
}

void PollingCorePlugin::recordVote(const QString& option, const QString& source, const QString& voteId)
{
    m_seenVoteIds.insert(voteId);
    m_votes[option] = m_votes.value(option).toInt() + 1;
    saveVoteState();

    qInfo() << "PollingCorePlugin: recorded vote" << voteId << "for" << option
            << "source" << source << "count" << m_votes.value(option).toInt();

    QVariantMap result = snapshot();
    result.insert("ok", true);
    result.insert("selected", option);
    result.insert("source", source);

    emit eventResponse("voteSubmitted", QVariantList() << option << result);
}

void PollingCorePlugin::updateNetworkStatus(const QString& status, bool ready)
{
    m_networkStatus = status;
    m_networkReady = ready;

    emit eventResponse("networkStatusChanged", QVariantList() << snapshot());
}

void PollingCorePlugin::updateChainStatus(const QString& status, bool ready)
{
    m_chainStatus = status;
    m_chainReady = ready;

    emit eventResponse("networkStatusChanged", QVariantList() << snapshot());
}

void PollingCorePlugin::startVoteBridge()
{
    // Search for the vote-bridge binary alongside the installed module files.
    // LogosBasecampDev installs everything to the same module directory,
    // so vote-bridge lives next to polling_core_plugin.so when bundled.
    const QStringList candidates = {
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.local/share/Logos/LogosBasecampDev/modules/polling_core/vote-bridge"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.local/share/Logos/modules/polling_core/vote-bridge"),
        QStringLiteral("vote-bridge"), // fallback: PATH
    };

    QString bridgePath;
    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            bridgePath = candidate;
            break;
        }
    }

    if (bridgePath.isEmpty()) {
        updateChainStatus(QStringLiteral("Blockchain inscriptions disabled: vote-bridge binary not found"), false);
        qDebug() << "PollingCorePlugin: vote-bridge binary not found - blockchain inscriptions disabled";
        return;
    }

    m_voteBridge = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Data directory for key + checkpoint persistence (survives lgpm reinstalls)
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.local/share/Logos/polling_core");
    env.insert(QStringLiteral("VOTE_DATA_DIR"), dataDir);
    const QString nodeUrl = env.value(QStringLiteral("VOTE_NODE_URL"), QStringLiteral("http://localhost:8080"));
    m_voteBridge->setProcessEnvironment(env);

    connect(m_voteBridge, &QProcess::readyReadStandardOutput,
            this, [this]() { processVoteBridgeOutput(); });

    connect(m_voteBridge, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
                qWarning() << "PollingCorePlugin: vote-bridge error" << error;
                updateChainStatus(QStringLiteral("Blockchain bridge error: %1").arg(m_voteBridge->errorString()), false);
            });

    connect(m_voteBridge, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                qWarning() << "PollingCorePlugin: vote-bridge exited with code" << code;
                updateChainStatus(QStringLiteral("Blockchain bridge stopped with exit code %1").arg(code), false);
                m_voteBridge->deleteLater();
                m_voteBridge = nullptr;
            });

    m_voteBridge->start(bridgePath, {});
    if (!m_voteBridge->waitForStarted(5000)) {
        qWarning() << "PollingCorePlugin: vote-bridge failed to start";
        updateChainStatus(QStringLiteral("Blockchain bridge failed to start: %1").arg(m_voteBridge->errorString()), false);
        delete m_voteBridge;
        m_voteBridge = nullptr;
        return;
    }

    updateChainStatus(QStringLiteral("Connecting to Logos blockchain at %1...").arg(nodeUrl), false);
    qDebug() << "PollingCorePlugin: vote-bridge started -" << bridgePath;
}

void PollingCorePlugin::stopVoteBridge()
{
    if (!m_voteBridge) {
        return;
    }
    m_voteBridge->terminate();
    if (!m_voteBridge->waitForFinished(3000)) {
        m_voteBridge->kill();
    }
    delete m_voteBridge;
    m_voteBridge = nullptr;
}

void PollingCorePlugin::writeVoteBridge(const QByteArray& jsonLine)
{
    if (m_voteBridge && m_voteBridge->state() == QProcess::Running) {
        m_voteBridge->write(jsonLine);
    }
}

void PollingCorePlugin::processVoteBridgeOutput()
{
    while (m_voteBridge && m_voteBridge->canReadLine()) {
        const QByteArray line = m_voteBridge->readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString event = obj.value("event").toString();

        if (event == QLatin1String("ready")) {
            qDebug() << "PollingCorePlugin: vote-bridge ready - blockchain inscriptions active";
            updateChainStatus(
                QStringLiteral("Connected to Logos blockchain channel logos:yolo:polling"), true);

        } else if (event == QLatin1String("vote")) {
            const QString option  = obj.value("option").toString();
            const QString voteId  = obj.value("vote_id").toString();
            const QString sender  = obj.value("sender").toString();

            // Reuse the same deduplication as the Waku path.
            // The vote payload on-chain carries the same UUID, so a vote
            // delivered by both Waku and the blockchain is only counted once.
            if (sender == m_instanceId) {
                qInfo() << "PollingCorePlugin: ignored own blockchain vote" << voteId << "for" << option;
                continue;
            }

            if (voteId.isEmpty()) {
                qWarning() << "PollingCorePlugin: ignored blockchain vote with missing id for" << option;
                continue;
            }

            if (m_seenVoteIds.contains(voteId)) {
                qInfo() << "PollingCorePlugin: ignored duplicate blockchain vote" << voteId << "for" << option;
                continue;
            }

            if (!isValidOption(option)) {
                qWarning() << "PollingCorePlugin: ignored blockchain vote" << voteId << "with invalid option" << option;
                continue;
            }

            qInfo() << "PollingCorePlugin: received blockchain vote" << voteId << "for" << option << "from" << sender;
            recordVote(option, QStringLiteral("network"), voteId);

        } else if (event == QLatin1String("error")) {
            const QString message = obj.value("message").toString();
            qWarning() << "PollingCorePlugin: vote-bridge error:" << message;
            updateChainStatus(QStringLiteral("Blockchain bridge error: %1").arg(message), false);
        }
    }
}

void PollingCorePlugin::loadVoteState()
{
    QSettings settings(QStringLiteral("Logos"), QStringLiteral("polling_core"));
    for (auto it = m_votes.begin(); it != m_votes.end(); ++it) {
        it.value() = settings.value(it.key(), 0).toInt();
    }

    const QStringList seenVoteIds = settings.value(QStringLiteral("seenVoteIds")).toStringList();
    for (const QString& voteId : seenVoteIds) {
        if (!voteId.isEmpty()) {
            m_seenVoteIds.insert(voteId);
        }
    }

    qDebug() << "PollingCorePlugin: loaded vote state from storage - seen vote ids" << m_seenVoteIds.size();
}

void PollingCorePlugin::saveVoteState()
{
    QSettings settings(QStringLiteral("Logos"), QStringLiteral("polling_core"));
    for (auto it = m_votes.constBegin(); it != m_votes.constEnd(); ++it) {
        settings.setValue(it.key(), it.value().toInt());
    }

    QStringList seenVoteIds;
    seenVoteIds.reserve(m_seenVoteIds.size());
    for (const QString& voteId : m_seenVoteIds) {
        seenVoteIds.append(voteId);
    }
    settings.setValue(QStringLiteral("seenVoteIds"), seenVoteIds);
}
