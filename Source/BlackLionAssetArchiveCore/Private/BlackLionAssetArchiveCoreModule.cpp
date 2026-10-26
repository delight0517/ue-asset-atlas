#include "Modules/ModuleManager.h"

class FBlackLionAssetArchiveCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBlackLionAssetArchiveCoreModule, BlackLionAssetArchiveCore)
