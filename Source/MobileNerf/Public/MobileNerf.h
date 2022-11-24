// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"



class FToolBarBuilder;
class FMenuBuilder;

class FMobileNerfModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	void AddMobileNerfMenu(FToolBarBuilder& Builder);
	
	TSharedRef<SWidget> FillComboButton(TSharedPtr<class FUICommandList> Commands);

	void ImportMobileNerfFromDisk(bool bIsForwardFacing);
	bool TryImportMobileNerf(FString InPath, bool bIsForwardFacing, bool bOptimize, FString& OutMessage);
	
private:
	void RegisterMenus();


private:
	TSharedPtr<class FUICommandList> PluginCommands;
};
