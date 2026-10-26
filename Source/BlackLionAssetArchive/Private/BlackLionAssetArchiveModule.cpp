#include "Modules/ModuleManager.h"

class FBlackLionAssetArchiveModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBlackLionAssetArchiveModule, BlackLionAssetArchive)
