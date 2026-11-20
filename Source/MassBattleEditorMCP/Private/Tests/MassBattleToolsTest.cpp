#include "MassBattleUnitEditorMCPApi.h"
#include "Editor.h"
#include "EditorUtilityWidget.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassBattleToolsTest, "MassBattle.MCP.MassBattleTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassBattleToolsTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/MassBattleEditorMCP/ActorToMassBattleUnitEditor/MassBattleTools.MassBattleTools"));
	if (!TestNotNull(TEXT("MassBattleTools Blueprint loads"), Blueprint)) return false;
	UPackage* Package = Blueprint->GetOutermost();
	const bool bWasDirty = Package->IsDirty();
	ON_SCOPE_EXIT { Package->SetDirtyFlag(bWasDirty); };
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
	if (!TestEqual(TEXT("MassBattleTools compiles without warnings or errors"),
		static_cast<int32>(Blueprint->Status.GetValue()), static_cast<int32>(BS_UpToDate))) return false;
	if (!TestTrue(TEXT("Generated class is an Editor Utility Widget"),
		Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UEditorUtilityWidget::StaticClass()))) return false;

	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), EditorWorld)) return false;
	const TSubclassOf<UEditorUtilityWidget> WidgetClass(Blueprint->GeneratedClass.Get());
	UEditorUtilityWidget* Widget = CreateWidget<UEditorUtilityWidget>(EditorWorld, WidgetClass);
	if (!TestNotNull(TEXT("MassBattleTools widget instance is created"), Widget)) return false;
	Widget->SetFlags(RF_Transient);
	ON_SCOPE_EXIT { Widget->RemoveFromParent(); Widget->ReleaseSlateResources(true); };
	const TSharedPtr<SWidget> SlateWidget = Widget->TakeWidget();
	TestTrue(TEXT("MassBattleTools Slate widget is built"), SlateWidget.IsValid());
	TestNotNull(TEXT("MassBattleTools retains its authored widget tree"), Widget->GetRootWidget());

	FMassBattleMCPVatSource Source;
	FString SearchPath, PackagePath, UnitName, ResolvedSearchPath, Error;
	USkeletalMesh* OutputMesh = nullptr;
	bool bUsedActor = false;
	TestFalse(TEXT("Missing VAT source fails instead of generating assets"),
		UMassBattleUnitEditorMCPApi::PrepareVatSourceMesh(Source,
			SearchPath, PackagePath, UnitName, OutputMesh, bUsedActor, ResolvedSearchPath, Error));
	TestNull(TEXT("Missing source does not return an output mesh"), OutputMesh);
	TestFalse(TEXT("Missing skeletal source is not reported as an Actor"), bUsedActor);
	TestFalse(TEXT("Missing source provides an actionable error"), Error.IsEmpty());

	// A private preview world keeps the fixture out of the user's level and asset packages.
	UWorld* FixtureWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("Transient fixture world is created"), FixtureWorld)) return false;
	ON_SCOPE_EXIT { FixtureWorld->DestroyWorld(false); };
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = FixtureWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!TestNotNull(TEXT("Empty Actor source fixture is created"), Actor)) return false;
	Source.SourceType = EMassBattleMCPVatSourceType::ActorInstance;
	Source.ActorInstance = Actor;
	Error.Reset();
	TestFalse(TEXT("Actor without a skeletal mesh fails VAT preparation"),
		UMassBattleUnitEditorMCPApi::PrepareVatSourceMesh(Source,
			SearchPath, PackagePath, UnitName, OutputMesh, bUsedActor, ResolvedSearchPath, Error));
	TestNull(TEXT("Failed Actor preparation produces no mesh"), OutputMesh);
	TestTrue(TEXT("Actor source type is retained in the result"), bUsedActor);
	TestFalse(TEXT("Invalid Actor source provides an error"), Error.IsEmpty());
	TestTrue(TEXT("Caller-owned Actor survives failed preparation"),
		IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->GetWorld() == FixtureWorld);
	TestTrue(TEXT("VAT source still references the caller-owned Actor"), Source.ActorInstance.Get() == Actor);
	return !HasAnyErrors();
}
#endif
