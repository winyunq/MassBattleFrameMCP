#include "MassBattleUnitSource.h"
#include "MassBattleUnitMCPApi.h"
#include "MassBattleATTDataAsset.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassBattleSourceExistingExportTest, "MassBattle.MCP.SourceExistingExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassBattleSourceExistingExportTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = TEXT("/Game/__MCPExistingExportTest/BP_Source");
	const FString UnitPath = TEXT("/Game/__MCPExistingExportTest/DA_Unit");
	const FString OtherPath = TEXT("/Game/__MCPExistingExportTest/OtherAsset");
	if (LoadObject<UObject>(nullptr, *SourcePath) || LoadObject<UObject>(nullptr, *UnitPath)
		|| LoadObject<UObject>(nullptr, *OtherPath))
	{
		AddError(TEXT("Test fixture already exists."));
		return false;
	}
	UMassBattleATTDataAsset* VatFixture = NewObject<UMassBattleATTDataAsset>(
		CreatePackage(TEXT("/Game/__MCPExistingExportTest/VAT")), TEXT("VAT"), RF_Public | RF_Standalone);
	UMassBattleUnitMCPApi::MCP_UnitCreate(TEXT("{\"asset_type\":\"source_actor\",\"unit_path\":\"/Game/__MCPExistingExportTest/BP_Source\",\"export_path\":\"/Game/__MCPExistingExportTest/DA_Unit\",\"vat_data_asset\":\"/Game/__MCPExistingExportTest/VAT.VAT\"}"), false);
	AMassBattleUnitSource* Source = AMassBattleUnitSource::Resolve(LoadObject<UObject>(nullptr, *SourcePath));
	if (!TestNotNull(TEXT("Source created"), Source)) return false;
	TestTrue(TEXT("Source retains the selected VAT data"), Source->VATData == VatFixture);
	TestEqual(TEXT("VAT driver is configured"), Source->VATDriver.ComponentProperty, FName(TEXT("SourceModel")));
	TestNotNull(TEXT("Skeletal source component is created"), CastChecked<UBlueprint>(Source->GetSourceAsset())->SimpleConstructionScript->FindSCSNode(TEXT("SourceModel")));
	Source->VATData = nullptr;
	UMassBattleAgentConfigDataAsset* Existing = NewObject<UMassBattleAgentConfigDataAsset>(
		CreatePackage(*UnitPath), TEXT("DA_Unit"), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Existing);
	UTexture2D* Other = NewObject<UTexture2D>(CreatePackage(*OtherPath), TEXT("OtherAsset"), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Other);
	ON_SCOPE_EXIT
	{
		Source->ExportedUnit = nullptr;
		ObjectTools::DeleteObjects({ Source->GetSourceAsset(), Existing, Other, VatFixture }, false);
	};

	Existing->StyleType.Index = 17;
	Source->UnitData->StyleType.Index = 22;
	const FString Result = UMassBattleUnitMCPApi::MCP_UnitMergeUpdate(SourcePath, TEXT("{\"update_export\":true,\"rebake_vat\":false}"), false);
	TSharedPtr<FJsonObject> Report;
	const bool bParsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result), Report);
	if (!TestTrue(TEXT("Export succeeds"), bParsed && Report.IsValid() && Report->GetBoolField(TEXT("success")))) return false;
	TestFalse(TEXT("Parameter export does not report a VAT rebake"), Report->GetBoolField(TEXT("vat_rebaked")));
	TestTrue(TEXT("Existing unit object is retained"), Source->ExportedUnit == Existing);
	TestEqual(TEXT("Source data updates existing unit"), Existing->StyleType.Index, 22);
	Existing->Scaling.Scale = 7.0f;
	Source->UnitData->StyleType.Index = 23;
	UMassBattleUnitMCPApi::UpdateSource(Source, false);
	TestEqual(TEXT("Subsequent source change is exported"), Existing->StyleType.Index, 23);
	TestEqual(TEXT("Independent output edit is preserved"), Existing->Scaling.Scale, 7.0f);
	Source->VATData = NewObject<UMassBattleATTDataAsset>();
	const FString MissingDriver = UMassBattleUnitMCPApi::UpdateSource(Source, false);
	TestTrue(TEXT("VAT export requires the selected driver"), MissingDriver.Contains(TEXT("VAT driver")));
	TestTrue(TEXT("Rejected VAT export preserves the output"), Source->ExportedUnit == Existing);
	Source->VATData = nullptr;

	Source->ExportPath = OtherPath;
	const FString Rejected = UMassBattleUnitMCPApi::UpdateSource(Source, false);
	TestTrue(TEXT("Different asset type is rejected"), Rejected.Contains(TEXT("different type")));
	TestTrue(TEXT("Failed export preserves the existing association"), Source->ExportedUnit == Existing);
	TestTrue(TEXT("Other asset survives"), LoadObject<UObject>(nullptr, *OtherPath) == Other);
	return !HasAnyErrors();
}
#endif
