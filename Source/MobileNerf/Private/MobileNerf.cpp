// Copyright Epic Games, Inc. All Rights Reserved.

#include "MobileNerf.h"
#include "MobileNerfStyle.h"
#include "MobileNerfCommands.h"
#include "MobileNerfAsset.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "LevelEditor.h"

#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "HAL/FileManagerGeneric.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "AssetToolsModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"

#include "Engine/StaticMeshActor.h"
#include "EditorLevelLibrary.h"

static const FName MobileNerfTabName("MobileNerf");
static const char* ShaderTemplate = R"(
VIEWDIR_MANIP
half intermediate_one[NUM_CHANNELS_ONE] = { BIAS_LIST_ZERO };
int i = 0;
int j = 0;

for (j = 0; j < NUM_CHANNELS_ZERO; ++j) {
	half input_value = 0.0;
	if (j < 4) {
		input_value =
			(j == 0) ? f0.r : (
				(j == 1) ? f0.g : (
					(j == 2) ? f0.b : f0.a));
	}
	else if (j < 8) {
		input_value =
			(j == 4) ? f1.r : (
				(j == 5) ? f1.g : (
					(j == 6) ? f1.b : f1.a));
	}
	else {
		input_value =
			(j == 8) ? viewdir.r : (
				(j == 9) ? viewdir.g : viewdir.b);
	}
	for (i = 0; i < NUM_CHANNELS_ONE; ++i) {
		intermediate_one[i] += input_value * weightsZero.Load(int3(j, i, 0)).x;
	}
}

half intermediate_two[NUM_CHANNELS_TWO] = { BIAS_LIST_ONE };

for (j = 0; j < NUM_CHANNELS_ONE; ++j) {
	if (intermediate_one[j] <= 0.0) {
		continue;
	}
	for (i = 0; i < NUM_CHANNELS_TWO; ++i) {
		intermediate_two[i] += intermediate_one[j] * weightsOne.Load(int3(j, i, 0)).x;
	}
}

half result[NUM_CHANNELS_THREE] = { BIAS_LIST_TWO };

for (j = 0; j < NUM_CHANNELS_TWO; ++j) {
	if (intermediate_two[j] <= 0.0) {
		continue;
	}
	for (i = 0; i < NUM_CHANNELS_THREE; ++i) {
		result[i] += intermediate_two[j] * weightsTwo.Load(int3(j, i, 0)).x;
	}
}
for (i = 0; i < NUM_CHANNELS_THREE; ++i) {
	result[i] = 1.0 / (1.0 + exp(-result[i]));
}
return half3(result[0] * viewdir.a + (1.0 - viewdir.a),
	result[1] * viewdir.a + (1.0 - viewdir.a),
	result[2] * viewdir.a + (1.0 - viewdir.a));
	)";

static const char* ShaderOptimizedTemplate = R"(
VIEWDIR_MANIP
half intermediate_one[NUM_CHANNELS_ONE] = { BIAS_LIST_ZERO };
int i = 0;
int j = 0;
half input_values[11] = {f0.r, f0.g, f0.b, f0.a, f1.r, f1.g, f1.b, f1.a, viewdir.r, viewdir.g, viewdir.b};
for (j = 0; j < NUM_CHANNELS_ZERO; ++j) {
		for (i = 0; i < NUM_CHANNELS_ONE; i=i+4) {
		half4 res = half4(intermediate_one[i], intermediate_one[i+1], intermediate_one[i+2], intermediate_one[i+3]);
        res += weightsZero.Load(int3(j, (i/4), 0)) * input_values[j];
		intermediate_one[i] = res.x;
		intermediate_one[i+1] = res.y;
		intermediate_one[i+2] = res.z;
		intermediate_one[i+3] = res.w;
	}
}

half intermediate_two[NUM_CHANNELS_TWO] = { BIAS_LIST_ONE };

for (j = 0; j < NUM_CHANNELS_ONE; ++j) {
	if (intermediate_one[j] <= 0.0) {
		continue;
	}
  	for (i = 0; i < NUM_CHANNELS_TWO; i=i+4) {
		half4 res = half4(intermediate_two[i], intermediate_two[i+1], intermediate_two[i+2], intermediate_two[i+3]);
		res = weightsOne.Load(int3(j, (i/4), 0)) * intermediate_one[j];
		intermediate_two[i] += res.x;
		intermediate_two[i+1] += res.y;
		intermediate_two[i+2] += res.z;
		intermediate_two[i+3] += res.w;
	}
}

half3 result = half3( BIAS_LIST_TWO );

for (j = 0; j < NUM_CHANNELS_TWO; ++j) {
	if (intermediate_two[j] <= 0.0) {
		continue;
	}
	result += intermediate_two[j] * weightsTwo.Load(int3(j, 0, 0)).xyz;
}

result = 1.0 / (1.0 + exp(-result));
result = result* viewdir.a + (1.0 - viewdir.a);
return result;
)";

#define LOCTEXT_NAMESPACE "FMobileNerfModule"

void FMobileNerfModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FMobileNerfStyle::Initialize();
	FMobileNerfStyle::ReloadTextures();

	FMobileNerfCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FMobileNerfCommands::Get().Import360,
		FExecuteAction::CreateRaw(this, &FMobileNerfModule::ImportMobileNerfFromDisk, false),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMobileNerfCommands::Get().ImportForwardFacing,
		FExecuteAction::CreateRaw(this, &FMobileNerfModule::ImportMobileNerfFromDisk, true),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMobileNerfModule::RegisterMenus));
}

void FMobileNerfModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FMobileNerfStyle::Shutdown();

	FMobileNerfCommands::Unregister();
}

void FMobileNerfModule::AddMobileNerfMenu(FMenuBarBuilder& Builder)
{
	Builder.AddPullDownMenu(
		FText::FromString("MobileNeRF"),
		FText::FromString("MobileNerf Plugin Menu"),
		FNewMenuDelegate::CreateRaw(this, &FMobileNerfModule::FillPulldownMenu),
		"MobileNeRF",
		FName(TEXT("MobileNerf Plugin Menu"))
	);
}

TSharedRef<SWidget> FMobileNerfModule::FillComboButton(TSharedPtr<class FUICommandList> Commands)
{
	FMenuBuilder MenuBuilder(true, Commands);

	MenuBuilder.AddMenuEntry(FMobileNerfCommands::Get().Import360);
	MenuBuilder.AddMenuEntry(FMobileNerfCommands::Get().ImportForwardFacing);

	return MenuBuilder.MakeWidget();
}

void FMobileNerfModule::FillPulldownMenu(FMenuBuilder& Builder)
{   
	Builder.BeginSection("Import Section", FText::FromString("Import MobileNeRF"));
	Builder.AddMenuEntry(FMobileNerfCommands::Get().Import360);
	Builder.AddMenuEntry(FMobileNerfCommands::Get().ImportForwardFacing);
	Builder.AddMenuSeparator(FName("Section_1"));
	Builder.EndSection();
}


//Parses a 2D json array and flattens it to a 1D float array
//returns the dimensions of the origianl array as an FIntPoint
FIntPoint Flatten2DJsonArray(const TSharedPtr<FJsonValue> JsonValue, TArray<float>& OutData)
{
	FIntPoint Dims = FIntPoint(0, 0);
	const TArray<TSharedPtr<FJsonValue>> JsonArray = JsonValue->AsArray();
	Dims.X = JsonArray.Num();
	//Get the Height so we can rearrange the elements properly
	if (JsonArray.Num())
	{
		Dims.Y = JsonArray[0]->AsArray().Num();
	}

	OutData.SetNum(Dims.X*Dims.Y);

	UINT16 index = 0;
	UINT16 i = 0;
	for (auto JsonArrayEntry : JsonArray)
	{
		const TArray<TSharedPtr<FJsonValue>> NestedJsonArray = JsonArrayEntry->AsArray();
		UINT16 j = 0;
		for (auto NestedJsonArrayEntry : NestedJsonArray)
		{
			OutData[j*Dims.X + i] = NestedJsonArrayEntry->AsNumber();
			j++;
		}
		i++;
	}
	return Dims;
}

UTexture2D* CreateWeightsTexture(FIntPoint Dims, TArray<float>& WeightsData, const FString& Name, const FString& RootPackageName, bool bPack = false)
{
	FString PackageName = RootPackageName + Name;
	UPackage* Package = CreatePackage(NULL, *PackageName);
	Package->FullyLoad();

	//Create the source data for the texture, we either use one float per color or pack 4 floats in one color.
	TArray<FFloat16Color> Colors;
	if (bPack)
	{
		Dims.Y = FMath::DivideAndRoundUp(Dims.Y, 4);
		Colors.SetNum(Dims.X * Dims.Y);

		for (uint16 i = 0; i < Dims.X; ++i)
		{
			for (uint16 j = 0; j < Dims.Y; ++j)
			{
				Colors[i + j * Dims.X].R = WeightsData[i + (j * 4) * Dims.X];
				Colors[i + j * Dims.X].G = WeightsData[i + (j * 4 + 1) * Dims.X];
				Colors[i + j * Dims.X].B = WeightsData[i + (j * 4 + 2) * Dims.X];
				if((i + (j * 4 + 3) * Dims.X) < WeightsData.Num())
					Colors[i + j * Dims.X].A = WeightsData[i + (j * 4 + 3) * Dims.X];
			}
		}
	}
	else
	{
		Colors.SetNum(WeightsData.Num());
		for (uint16 ui = 0; ui < Colors.Num(); ui++)
		{
			Colors[ui] = FFloat16Color();
			Colors[ui].R = WeightsData[ui];
		}
	}
	
	UTexture2D* Tex2D;
	Tex2D = NewObject<UTexture2D>(Package, FName(*Name), RF_Public | RF_Standalone | RF_MarkAsRootSet);
	Tex2D->AddToRoot();
	Tex2D->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	Tex2D->SRGB = 0;
	Tex2D->Filter = TextureFilter::TF_Nearest;
	Tex2D->AddressX = TextureAddress::TA_Clamp;
	Tex2D->AddressY = TextureAddress::TA_Clamp;
	Tex2D->CompressionSettings = bPack ? TextureCompressionSettings::TC_VectorDisplacementmap : TextureCompressionSettings::TC_HalfFloat;
	Tex2D->CompressionNone = 1;

	const uint8* PixelData = (const uint8*)Colors.GetData();
	Tex2D->Source.Init(Dims.X, Dims.Y, /*NumSlices=*/ 1, /*NumMips=*/ 1, TSF_RGBA16F, PixelData);
	Tex2D->UpdateResource();
	Tex2D->PostEditChange();

	FAssetRegistryModule::AssetCreated(Tex2D);
	Tex2D->MarkPackageDirty();
	return Tex2D;
}


FString BiasArrayToString(const TArray<float>& InArray)
{
	FString OutStr = "";
	for (float f : InArray)
	{
		OutStr.Appendf(TEXT("%f ,"), f);
	}
	OutStr.RemoveAt(OutStr.Len() - 1);
	return MoveTemp(OutStr);
}

void FMobileNerfModule::ImportMobileNerfFromDisk(bool bIsForwardFacing)
{
	//Get the window handle and open Directory dialog for the user
	void* ParentWindowWindowHandle = nullptr;
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
	if (MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid())
	{
		ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	FString OutFolderName;
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindowWindowHandle, LOCTEXT("MobileNerf Selection Dialog", "Load MobileNerf...").ToString(), "", OutFolderName))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Selected folder: %s"), *OutFolderName);

	//Ask the user whether to use optimizations for this scene or no
	FText DialogText = FText::Format(
		LOCTEXT("DialogText", "Starting MobileNeRF import from {0} \nScene Type: {1} \n\nEnable Shader optimizations and packed weight textures for this scene?"),
		FText::FromString(OutFolderName), bIsForwardFacing ? FText::FromString(TEXT("Forward-Facing")) : FText::FromString(TEXT("360°")));

	auto UserAnswer = FMessageDialog::Open(EAppMsgType::YesNo, DialogText);
	const bool bOptimize = (UserAnswer == EAppReturnType::Yes);


	UE_LOG(LogTemp, Log, TEXT("MobileNeRF scene type: %s"), bIsForwardFacing ? TEXT("Forward-Facing") : TEXT("360°"));
	UE_LOG(LogTemp, Log, TEXT("Enable optimizations: %s"), bOptimize ? TEXT("True") : TEXT("False"));
	FString OutMessage;

	if (TryImportMobileNerf(OutFolderName, bIsForwardFacing, bOptimize, OutMessage))
	{
		DialogText = FText::FromString(TEXT("MobileNeRF scene import finished successfully! \nMake sure to save all the newly created/imported assets."));
	}
	else
	{
		DialogText = FText::Format(
			LOCTEXT("DialogText", "MobileNeRF scene import failed! \nReason: {0} \nMake sure the specified folder contains an mlp.json and all the OBJs and PNGs."),
			FText::FromString(OutMessage));
	}
	UserAnswer = FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

bool FMobileNerfModule::TryImportMobileNerf(FString InPath, bool bIsForwardFacing, bool bOptimize, FString& OutMessage)
{
	int32 OutCharIndex = 0;
	if (!InPath.FindLastChar('/', OutCharIndex))
	{
		InPath.FindLastChar('\\', OutCharIndex);
	}
	const FString SceneName = InPath.Right(InPath.Len() - OutCharIndex - 1);
	UE_LOG(LogTemp, Log, TEXT("Scene Name is %s"), *SceneName);

	//Get all files' paths from the selected folder (json, objs and pngs)
	TArray<FString> OutFileNames;
	FFileManagerGeneric::Get().FindFiles(OutFileNames, *InPath, TEXT("json"));
	if (OutFileNames.Num() == 0)
	{
		OutMessage = TEXT("No json file found in the selected folder!");
		return false;
	}

	//Load MLP from the first json file we find in the directory
	FString FileData;
	FMLPJson MLPJsonData;
	FFileHelper::LoadFileToString(FileData, *(InPath + "/" + OutFileNames[0]));

	//Replace key names in the json string since we can't have variable names that start with a digit
	TMap<FString, FString> NewKeyNames;
	NewKeyNames.Add("0_bias", "Bias_0");
	NewKeyNames.Add("1_bias", "Bias_1");
	NewKeyNames.Add("2_bias", "Bias_2");
	for (auto& NameTuple : NewKeyNames)
		FileData = FileData.Replace(*NameTuple.Key, *NameTuple.Value);

	//This converter can't deserialize 2D arrays so it skips the weights
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(FileData, &MLPJsonData, 0, CPF_Transient))
	{
		OutMessage = TEXT("Deserializing biases in the json file failed!");
		return false;
	}

	//Handling the weights manually
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(FileData);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
	{
		OutMessage = TEXT("Deserializing weights in the json file failed!");
		return false;
	}

	const FIntPoint WeightsDims0 = Flatten2DJsonArray(JsonObject->TryGetField("0_weights"), MLPJsonData.WeightsData0);
	const FIntPoint WeightsDims1 = Flatten2DJsonArray(JsonObject->TryGetField("1_weights"), MLPJsonData.WeightsData1);
	const FIntPoint WeightsDims2 = Flatten2DJsonArray(JsonObject->TryGetField("2_weights"), MLPJsonData.WeightsData2);

	//Create the uassets
	const FString RootPackageName = TEXT("/Game/MobileNerfScenes/" + SceneName + "/");
	UPackage* RootPackage = CreatePackage(NULL, *RootPackageName);

	const FString MobileNerfAssetPackageName = FString::Printf(TEXT("%s%sMLP"), *RootPackageName, *SceneName);
	UPackage* MobileNerfAssetPackage = CreatePackage(NULL, *MobileNerfAssetPackageName);

	UMobileNerfAsset* NewMobileNerfAsset = NewObject<UMobileNerfAsset>(MobileNerfAssetPackage, UMobileNerfAsset::StaticClass(), *(SceneName + "MLP"), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);

	if (NewMobileNerfAsset != NULL)
	{
		NewMobileNerfAsset->Bias0 = MoveTemp(MLPJsonData.Bias_0);
		NewMobileNerfAsset->Bias1 = MoveTemp(MLPJsonData.Bias_1);
		NewMobileNerfAsset->Bias2 = MoveTemp(MLPJsonData.Bias_2);
		NewMobileNerfAsset->NumObjects = MLPJsonData.obj_num;

		//Create the textures
		NewMobileNerfAsset->Weights0_Tex = CreateWeightsTexture(WeightsDims0, MLPJsonData.WeightsData0, *(SceneName + "Weights0"), RootPackageName, bOptimize);
		NewMobileNerfAsset->Weights1_Tex = CreateWeightsTexture(WeightsDims1, MLPJsonData.WeightsData1, *(SceneName + "Weights1"), RootPackageName, bOptimize);
		NewMobileNerfAsset->Weights2_Tex = CreateWeightsTexture(WeightsDims2, MLPJsonData.WeightsData2, *(SceneName + "Weights2"), RootPackageName, bOptimize);

		//Scene type
		NewMobileNerfAsset->bIsForwardFacing = bIsForwardFacing;

		FAssetRegistryModule::AssetCreated(NewMobileNerfAsset);
		NewMobileNerfAsset->MarkPackageDirty();


		UE_LOG(LogTemp, Log, TEXT("MobileNerf Asset is created!"));
	}
	else
	{
		OutMessage = TEXT("Creating the MobileNeRF asset failed!");
		return false;
	}


	//Load OBJs
	OutFileNames.Empty();
	FFileManagerGeneric::Get().FindFiles(OutFileNames, *InPath, TEXT("obj"));
	if (OutFileNames.Num() == 0)
	{
		OutMessage = TEXT("No obj files found in the selected folder!");
		return false;
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>(UFbxFactory::StaticClass(), FName("FBXFactory"), RF_NoFlags);

	UE_LOG(LogTemp, Log, TEXT("Importing %d OBJ files"), OutFileNames.Num());
	for (auto& ObjName : OutFileNames)
	{
		bool bCanceled = false;
		UObject* ImportedObject = FbxFactory->ImportObject(FbxFactory->ResolveSupportedClass(),
			RootPackage, *ObjName, RF_Public | RF_Standalone, InPath + "/" + ObjName, nullptr, bCanceled);
		if (bCanceled)
		{
			OutMessage = TEXT("User canceled OBJ import!");
			return false;
		}

		UE_LOG(LogTemp, Log, TEXT("%s imported successfully!"), *ObjName);

		auto ImportedMesh = Cast<UStaticMesh>(ImportedObject);
		if (IsValid(ImportedMesh))
		{
			ImportedMesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs = true;
			ImportedMesh->GetSourceModel(0).BuildSettings.BuildScale3D = FVector(1000.0f);
			ImportedMesh->PostEditChange();
			NewMobileNerfAsset->Meshes.Add(ImportedMesh);
		}
	}


	//Load PNGs
	OutFileNames.Empty();
	FFileManagerGeneric::Get().FindFiles(OutFileNames, *InPath, TEXT("png"));
	if (OutFileNames.Num() == 0)
	{
		OutMessage = TEXT("No png files found in the selected folder!");
		return false;
	}

	//set the filenames to the full path so we can batch import them  using the assettools module
	for (auto& File : OutFileNames)
		File = InPath + "/" + File;

	UE_LOG(LogTemp, Log, TEXT("Importing %d PNG files"), OutFileNames.Num());

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	TArray<UObject*> ImportedObjects = AssetToolsModule.Get().ImportAssets(OutFileNames, RootPackageName);

	for (auto ImportedObject : ImportedObjects)
	{
		UTexture2D* Texture = Cast<UTexture2D>(ImportedObject);
		Texture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
		Texture->SRGB = 0;
		Texture->Filter = TextureFilter::TF_Nearest;
		Texture->AddressX = TextureAddress::TA_Wrap;
		Texture->AddressY = TextureAddress::TA_Wrap;
		Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		Texture->CompressionNone = 1;
		Texture->PostEditChange();
		NewMobileNerfAsset->Textures.AddUnique(Texture);
	}

	UE_LOG(LogTemp, Log, TEXT("Imported %d PNG files"), NewMobileNerfAsset->Textures.Num());


	//Make a copy of the template material from the plugin folder
	UPackage* MaterialBasePackage = CreatePackage(NULL, *(RootPackageName + "MobileNerfBaseMaterial"));
	auto MaterialBase = DuplicateObject(NewMobileNerfAsset->MobileNerfMaterial, MaterialBasePackage, *(SceneName + "BaseMaterial"));

	//Set the weight textures
	MaterialBase->SetTextureParameterValueEditorOnly("Weights0", NewMobileNerfAsset->Weights0_Tex);
	MaterialBase->SetTextureParameterValueEditorOnly("Weights1", NewMobileNerfAsset->Weights1_Tex);
	MaterialBase->SetTextureParameterValueEditorOnly("Weights2", NewMobileNerfAsset->Weights2_Tex);

	//Find the custom node
	UMaterialExpressionCustom* CustomNodeExp = NULL;
	for (auto ExpressionPtr : MaterialBase->GetEditorOnlyData()->ExpressionCollection.Expressions)
	{
		if (ExpressionPtr->IsA(UMaterialExpressionCustom::StaticClass()))
		{
			UE_LOG(LogTemp, Error, TEXT("Found the Custom Node in MobileNerf Base Material!"));
			CustomNodeExp = Cast<UMaterialExpressionCustom>(ExpressionPtr);
			break;
		}

	}

	if (!IsValid(CustomNodeExp))
	{
		OutMessage = TEXT("Can't find Custom Node in MobileNerf base material!");
		return false;
	}

	const FString ViewDirManipulationStr = bIsForwardFacing ? "viewdir.yz = viewdir.zy;" : "viewdir.y = -viewdir.y;";
	//Which code template to use
	CustomNodeExp->Code = bOptimize ? ShaderOptimizedTemplate : ShaderTemplate;
	//Viewdir axis manipulation depending on the scene type
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("VIEWDIR_MANIP"), *ViewDirManipulationStr);
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("NUM_CHANNELS_ZERO"), *FString::FromInt(WeightsDims0.X));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("NUM_CHANNELS_ONE"), *FString::FromInt(NewMobileNerfAsset->Bias0.Num()));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("NUM_CHANNELS_TWO"), *FString::FromInt(NewMobileNerfAsset->Bias1.Num()));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("NUM_CHANNELS_THREE"), *FString::FromInt(NewMobileNerfAsset->Bias2.Num()));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("BIAS_LIST_ZERO"), *BiasArrayToString(NewMobileNerfAsset->Bias0));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("BIAS_LIST_ONE"), *BiasArrayToString(NewMobileNerfAsset->Bias1));
	CustomNodeExp->Code = CustomNodeExp->Code.Replace(TEXT("BIAS_LIST_TWO"), *BiasArrayToString(NewMobileNerfAsset->Bias2));

	CustomNodeExp->PostEditChange();

	FAssetRegistryModule::AssetCreated(MaterialBase);
	MaterialBase->MarkPackageDirty();

	//Set the new material in the MobileNerf asset
	NewMobileNerfAsset->MobileNerfMaterial = MaterialBase;

	//Create material instances for each object
	const FString MaterialNameBase = FString("MobileNerf") + SceneName;
	UMaterialInstanceConstantFactoryNew* MaterialInstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
	MaterialInstanceFactory->InitialParent = MaterialBase;

	NewMobileNerfAsset->MaterialInstances.SetNum(NewMobileNerfAsset->NumObjects);

	for (uint8 i = 0; i < NewMobileNerfAsset->NumObjects; i++)
	{
		const FString ShapeName = FString::Printf(TEXT("shape%d"), i);
		const FString MaterialInstName = MaterialNameBase + ShapeName;

		//Create the package for this material instance
		UPackage* MaterialInstancePackage = CreatePackage(*(RootPackageName + MaterialInstName));

		//Create the material instance constant and set the diffuse textures
		NewMobileNerfAsset->MaterialInstances[i] = Cast<UMaterialInstanceConstant>(MaterialInstanceFactory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(),
			MaterialInstancePackage, *MaterialInstName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, NULL, GWarn));

		NewMobileNerfAsset->MaterialInstances[i]->SetTextureParameterValueEditorOnly(FMaterialParameterInfo("Diffuse0"), NewMobileNerfAsset->Textures[2 * i]);
		NewMobileNerfAsset->MaterialInstances[i]->SetTextureParameterValueEditorOnly(FMaterialParameterInfo("Diffuse1"), NewMobileNerfAsset->Textures[2 * i + 1]);
		NewMobileNerfAsset->MaterialInstances[i]->PostEditChange();

		//Set the new material instance to be used by its shapes
		for (auto mesh : NewMobileNerfAsset->Meshes)
		{
			if (mesh->GetFName().ToString().Contains(ShapeName))
				mesh->SetMaterial(0, NewMobileNerfAsset->MaterialInstances[i]);
		}
	}

	//Merge all static meshes
	TArray<AStaticMeshActor*> ActorsToMerge;
	ActorsToMerge.SetNum(NewMobileNerfAsset->Meshes.Num());
	for (uint16 i = 0; i < NewMobileNerfAsset->Meshes.Num(); ++i)
	{
		ActorsToMerge[i] = GEditor->GetEditorWorldContext().World()->SpawnActor<AStaticMeshActor>(FVector(0.0f), FRotator(0.0f, 0.0f, 90.0f));
		ActorsToMerge[i]->FindComponentByClass<UStaticMeshComponent>()->SetStaticMesh(NewMobileNerfAsset->Meshes[i]);
	}

	FMergeStaticMeshActorsOptions MergeOptions;
	MergeOptions.MeshMergingSettings.bMergeEquivalentMaterials = 1;
	MergeOptions.bDestroySourceActors = true;
	MergeOptions.bSpawnMergedActor = true;
	MergeOptions.BasePackageName = RootPackageName + SceneName + "Combined";
	MergeOptions.bSpawnMergedActor = true;
	AStaticMeshActor* OutMergedActor;
	UEditorLevelLibrary::MergeStaticMeshActors(ActorsToMerge, MergeOptions, OutMergedActor);
	if (IsValid(OutMergedActor))
	{
		NewMobileNerfAsset->CombinedMesh = OutMergedActor->FindComponentByClass<UStaticMeshComponent>()->GetStaticMesh();
		NewMobileNerfAsset->CombinedMesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs = true;
		NewMobileNerfAsset->CombinedMesh->PostEditChange();
		OutMergedActor->Destroy();
	}
	//All assets are created/imported
	return true;
}


void FMobileNerfModule::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		TSharedPtr<FExtender> NewToolbarExtender = MakeShareable(new FExtender);
		NewToolbarExtender->AddMenuBarExtension("Build",
			EExtensionHook::After,
			PluginCommands,
			FMenuBarExtensionDelegate::CreateRaw(this, &FMobileNerfModule::AddMobileNerfMenu));


		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewToolbarExtender);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMobileNerfModule, MobileNerf)