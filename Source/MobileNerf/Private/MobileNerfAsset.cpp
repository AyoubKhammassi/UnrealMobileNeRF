// Fill out your copyright notice in the Description page of Project Settings.


#include "MobileNerfAsset.h"
#include "UObject/ConstructorHelpers.h"


UMobileNerfAsset::UMobileNerfAsset(const FObjectInitializer& ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UMaterial> MaterialFinder(TEXT("/MobileNerf/Materials/MobileNerf.MobileNerf"));
	if (MaterialFinder.Object != NULL)
	{
		MobileNerfMaterial = (UMaterial*)MaterialFinder.Object;
		UE_LOG(LogTemp, Log, TEXT("Found MobileNerf Template Material!"));

	}
}

void UMobileNerfAsset::Serialize(FArchive& Ar)
{
	Ar << Weights0_Tex;
	Ar << Weights1_Tex;
	Ar << Weights2_Tex;
	Ar << Bias0;
	Ar << Bias1;
	Ar << Bias2;
	Ar << NumObjects;
	Ar << Meshes;
	Ar << Textures;
	Ar << MobileNerfMaterial;
	Ar << bIsForwardFacing;
	Ar << CombinedMesh;
}
