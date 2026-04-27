#ifndef POLLING_CORE_PLUGIN_H
#define POLLING_CORE_PLUGIN_H

#include <cstddef>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include "polling_core_interface.h"
#include "logos_api.h"

class LogosModules;

class PollingCorePlugin : public QObject, public PollingCoreInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PollingCoreInterface_iid FILE "metadata.json")
    Q_INTERFACES(PollingCoreInterface PluginInterface)

public:
    explicit PollingCorePlugin(QObject* parent = nullptr);
    ~PollingCorePlugin() override;

    QString name() const override { return "polling_core"; }
    QString version() const override { return "0.1.0"; }

    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);
    Q_INVOKABLE QVariantMap submitVote(const QString& option) override;
    Q_INVOKABLE QVariantMap getVoteCounts() override;
    Q_INVOKABLE QString getStatus() override;

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    static void deliveryCreateCallback(int ret, const char* msg, size_t len, void* userData);
    static void deliveryStartCallback(int ret, const char* msg, size_t len, void* userData);
    static void deliverySubscribeCallback(int ret, const char* msg, size_t len, void* userData);
    static void deliveryEventCallback(int ret, const char* msg, size_t len, void* userData);
    static void deliveryOperationCallback(int ret, const char* msg, size_t len, void* userData);

    QVariantMap snapshot() const;
    bool isValidOption(const QString& option) const;
    void initializeDelivery();
    void startDeliveryNode();
    void subscribeDeliveryTopic();
    void shutdownDelivery();
    void handleDeliveryOperation(const QString& operation, int ret, const QByteArray& message);
    void broadcastVote(const QString& option, const QString& voteId);
    void processDeliveryEvent(const QByteArray& eventJson);
    void processRemoteVote(const QByteArray& payload);
    void recordVote(const QString& option, const QString& source, const QString& voteId);
    void updateNetworkStatus(const QString& status, bool ready);
    void loadVoteCounts();
    void saveVoteCounts();

    LogosModules* m_logos = nullptr;
    QVariantMap m_votes;
    void* m_delivery = nullptr;
    QSet<QString> m_seenVoteIds;
    QString m_instanceId;
    QString m_networkStatus = QStringLiteral("Logos Delivery not started");
    bool m_networkReady = false;
};

#endif // POLLING_CORE_PLUGIN_H
