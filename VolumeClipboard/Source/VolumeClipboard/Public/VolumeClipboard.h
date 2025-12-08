#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

class FVolumeClipboardModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// UI Command functions
	void OpenPluginWindow();

private:
	void RegisterMenus();
	
	// The core logic functions
	FReply OnExtractVolumesClicked();
	FReply OnCreateVolumesClicked();

	// Helpers
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	void SerializeActorProperties(AActor* Actor, TSharedPtr<FJsonObject>& OutJson);
	void RestoreActorProperties(AActor* Actor, TSharedPtr<FJsonObject> InJson);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
};