#ifndef POLLING_UI_PLUGIN_H
#define POLLING_UI_PLUGIN_H

#include <QString>
#include <QVariantList>
#include "polling_ui_interface.h"
#include "LogosViewPluginBase.h"
#include "rep_polling_ui_source.h"

class LogosAPI;
class LogosModules;

class PollingUiPlugin : public PollingUiSimpleSource,
                        public PollingUiInterface,
                        public PollingUiViewPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PollingUiInterface_iid FILE "metadata.json")
    Q_INTERFACES(PollingUiInterface)

public:
    explicit PollingUiPlugin(QObject* parent = nullptr);
    ~PollingUiPlugin() override;

    QString name() const override { return "polling_ui"; }
    QString version() const override { return "0.1.0"; }

    Q_INVOKABLE void initLogos(LogosAPI* api);

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    LogosAPI* m_logosAPI = nullptr;
    LogosModules* m_logos = nullptr;
};

#endif // POLLING_UI_PLUGIN_H
