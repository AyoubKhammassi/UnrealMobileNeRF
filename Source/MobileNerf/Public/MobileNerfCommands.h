// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "MobileNerfStyle.h"

class FMobileNerfCommands : public TCommands<FMobileNerfCommands>
{
public:

	FMobileNerfCommands()
		: TCommands<FMobileNerfCommands>(TEXT("MobileNerf"), NSLOCTEXT("Contexts", "MobileNerf", "MobileNerf Plugin"), NAME_None, FMobileNerfStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > ImportForwardFacing;
	TSharedPtr< FUICommandInfo > Import360;
};
