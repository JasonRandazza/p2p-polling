#ifndef POLLING_UI_INTERFACE_H
#define POLLING_UI_INTERFACE_H

#include "interface.h"

class PollingUiInterface : public PluginInterface
{
public:
    virtual ~PollingUiInterface() = default;
};

#define PollingUiInterface_iid "org.logos.PollingUiInterface"
Q_DECLARE_INTERFACE(PollingUiInterface, PollingUiInterface_iid)

#endif // POLLING_UI_INTERFACE_H
