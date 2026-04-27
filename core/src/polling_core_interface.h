#ifndef POLLING_CORE_INTERFACE_H
#define POLLING_CORE_INTERFACE_H

#include <QString>
#include <QVariantMap>
#include "interface.h"

class PollingCoreInterface : public PluginInterface
{
public:
    virtual ~PollingCoreInterface() = default;

    Q_INVOKABLE virtual QVariantMap submitVote(const QString& option) = 0;
    Q_INVOKABLE virtual QVariantMap getVoteCounts() = 0;
    Q_INVOKABLE virtual QString getStatus() = 0;
};

#define PollingCoreInterface_iid "org.logos.PollingCoreInterface"
Q_DECLARE_INTERFACE(PollingCoreInterface, PollingCoreInterface_iid)

#endif // POLLING_CORE_INTERFACE_H
