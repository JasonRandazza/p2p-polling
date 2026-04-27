#include "polling_core_plugin.h"
#include "logos_sdk.h"
#include "liblogosdelivery.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QUuid>

namespace {
constexpr const char* kPollTopic = "/logos/tutorial/polling/1/vote/proto";

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
}

PollingCorePlugin::~PollingCorePlugin()
{
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
    return QStringLiteral("Polling core is tracking %1 votes. %2")
        .arg(total)
        .arg(m_networkStatus);
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
    if (!m_delivery || !m_networkReady) {
        return;
    }

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
        updateNetworkStatus(QStringLiteral("Logos Delivery send request failed"), false);
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

    if (sender == m_instanceId || voteId.isEmpty() || m_seenVoteIds.contains(voteId) || !isValidOption(option)) {
        return;
    }

    recordVote(option, QStringLiteral("network"), voteId);
}

void PollingCorePlugin::recordVote(const QString& option, const QString& source, const QString& voteId)
{
    m_seenVoteIds.insert(voteId);
    m_votes[option] = m_votes.value(option).toInt() + 1;

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
