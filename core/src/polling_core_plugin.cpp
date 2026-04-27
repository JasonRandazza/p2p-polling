#include "polling_core_plugin.h"
#include "logos_sdk.h"
#include <QDebug>

PollingCorePlugin::PollingCorePlugin(QObject* parent)
    : QObject(parent)
{
    m_votes.insert("Apples", 0);
    m_votes.insert("Bananas", 0);
    m_votes.insert("Oranges", 0);
}

PollingCorePlugin::~PollingCorePlugin()
{
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

    m_votes[option] = m_votes.value(option).toInt() + 1;

    QVariantMap result = snapshot();
    result.insert("ok", true);
    result.insert("selected", option);

    emit eventResponse("voteSubmitted", QVariantList() << option << result);
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
    return QStringLiteral("Polling core is tracking %1 local votes.").arg(total);
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
    return result;
}

bool PollingCorePlugin::isValidOption(const QString& option) const
{
    return m_votes.contains(option);
}
