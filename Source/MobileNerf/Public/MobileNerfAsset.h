// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MobileNerfAsset.generated.h"


/*
* For deserializing the MLP from the json file
* 
public partial class Mlp {
    [JsonProperty("0_weights")]
    public double[][] _0Weights;

    [JsonProperty("1_weights")]
    public double[][] _1Weights;

    [JsonProperty("2_weights")]
    public double[][] _2Weights;

    [JsonProperty("0_bias")]
    public double[] _0Bias;

    [JsonProperty("1_bias")]
    public double[] _1Bias;

    [JsonProperty("2_bias")]
    public double[] _2Bias;

    [JsonProperty("obj_num")]
    public int ObjNum;
}
*/
USTRUCT()
struct FArrayWrapper
{
    GENERATED_BODY()

        UPROPERTY()
        TArray<double> Values;
};


USTRUCT()
struct FMLPJson
{
	GENERATED_BODY()
public:
    UPROPERTY(Transient)
        TArray<float> WeightsData0;

    UPROPERTY(Transient)
        TArray<float> WeightsData1;

    UPROPERTY(Transient)
        TArray<float> WeightsData2;

    UPROPERTY()
        TArray<float> Bias_0;

    UPROPERTY()
        TArray<float> Bias_1;

    UPROPERTY()
        TArray<float> Bias_2;

	UPROPERTY()
		int obj_num;
};

/**
 * 
 */
UCLASS()
class MOBILENERF_API UMobileNerfAsset : public UObject
{
    GENERATED_BODY()
public:

    UMobileNerfAsset(const FObjectInitializer& ObjectInitializer);
	/* UObject Interface */
	virtual void Serialize(FArchive& Ar) override;

    UPROPERTY(VisibleAnywhere, Category = SceneType)
        bool bIsForwardFacing;

    UPROPERTY(VisibleAnywhere, Category = WeightsTextures)
        UTexture2D* Weights0_Tex;

    UPROPERTY(VisibleAnywhere, Category = WeightsTextures)
        UTexture2D* Weights1_Tex;

    UPROPERTY(VisibleAnywhere, Category = WeightsTextures)
        UTexture2D* Weights2_Tex;

    UPROPERTY(VisibleAnywhere, Category = Biases)
        TArray<float> Bias0;
    UPROPERTY(VisibleAnywhere, Category = Biases)
        TArray<float> Bias1;
    UPROPERTY(VisibleAnywhere, Category = Biases)
        TArray<float> Bias2;


    UPROPERTY(VisibleAnywhere, Category = General)
        uint16 NumObjects;

    UPROPERTY(VisibleAnywhere, Category = Meshes)
        TArray<UStaticMesh*> Meshes;

    UPROPERTY(VisibleAnywhere, Category = Meshes)
        UStaticMesh* CombinedMesh;

    UPROPERTY(VisibleAnywhere, Category = Textures)
        TArray<UTexture2D*> Textures;

    UPROPERTY(VisibleAnywhere, Category = Materials)
        UMaterial* MobileNerfMaterial;

    UPROPERTY()
        TArray<class UMaterialInstanceConstant*> MaterialInstances;
};
