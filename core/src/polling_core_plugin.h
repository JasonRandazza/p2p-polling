#ifndef POLLING_CORE_PLUGIN_H
#define POLLING_CORE_PLUGIN_H

#include <QObject>
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
    QVariantMap snapshot() const;
    bool isValidOption(const QString& option) const;

    LogosAPI* m_logosAPI = nullptr;
    LogosModules* m_logos = nullptr;
    QVariantMap m_votes;
};

#endif // POLLING_CORE_PLUGIN_H
