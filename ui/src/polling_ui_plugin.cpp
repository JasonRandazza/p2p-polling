#include "polling_ui_plugin.h"
#include "logos_api.h"
#include "logos_sdk.h"
#include "polling_core_api.h"
#include <QDebug>

PollingUiPlugin::PollingUiPlugin(QObject* parent)
    : PollingUiSimpleSource(parent)
{
    setStatus("Ready");
}

PollingUiPlugin::~PollingUiPlugin()
{
    delete m_logos;
}

void PollingUiPlugin::initLogos(LogosAPI* api)
{
    if (m_logos) {
        delete m_logos;
        m_logos = nullptr;
    }

    m_logosAPI = api;
    if (m_logosAPI) {
        m_logos = new LogosModules(m_logosAPI);
    }

    setBackend(this);
    setStatus("Connected to polling_core");
    qDebug() << "PollingUiPlugin: initialized";
}
