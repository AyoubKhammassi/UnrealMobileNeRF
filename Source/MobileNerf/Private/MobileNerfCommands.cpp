// Copyright Epic Games, Inc. All Rights Reserved.

#include "MobileNerfCommands.h"

#define LOCTEXT_NAMESPACE "FMobileNerfModule"

void FMobileNerfCommands::RegisterCommands()
{
	UI_COMMAND(Import360, "Import 360° MobileNeRF Scene", "Import a 360° MobileNeRF from a folder containing all the necessary files.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(ImportForwardFacing, "Import Forward-Facing MobileNeRF Scene", "Import a Forward-Facing MobileNeRF from a folder containing all the necessary files.", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
