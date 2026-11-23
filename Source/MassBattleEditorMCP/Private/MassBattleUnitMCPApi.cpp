// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#include "MassBattleUnitMCPApi.h"
#include "MassBattleUnitSource.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "MassBattleATTDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MassBattleEnums.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraSystem.h"
#include "ObjectTools.h"
#include "Rendering/MassBattleAgentRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogMassBattleUnitMCPApi);

namespace MassBattleUnitMCP
{
static constexpr int32 MaxDefaultUnitList = 200;
static constexpr int32 MaxPropertyDepth = 5;

enum class EFieldRole : uint8
{
	AuthoringConfig,
	SharedTypeConfig,
	SpawnInitialState,
	RuntimeState,
	SystemOwned,
	Deprecated,
	VisibilityControl,
	ExtensionConfig
};

struct FResolvedPropertyPath
{
	FProperty* Property = nullptr;
	void* ValuePtr = nullptr;
	FString Error;
};

struct FPatchPreview
{
	FString Path;
	FString Operation;
	FString Before;
	FString After;
};

static FString RoleToString(const EFieldRole Role)
{
	switch (Role)
	{
	case EFieldRole::AuthoringConfig: return TEXT("AuthoringConfig");
	case EFieldRole::SharedTypeConfig: return TEXT("SharedTypeConfig");
	case EFieldRole::SpawnInitialState: return TEXT("SpawnInitialState");
	case EFieldRole::RuntimeState: return TEXT("RuntimeState");
	case EFieldRole::SystemOwned: return TEXT("SystemOwned");
	case EFieldRole::Deprecated: return TEXT("Deprecated");
	case EFieldRole::VisibilityControl: return TEXT("VisibilityControl");
	case EFieldRole::ExtensionConfig: return TEXT("ExtensionConfig");
	default: return TEXT("Unknown");
	}
}

static EFieldRole ClassifyTopLevelField(const FString& Name)
{
	static const TSet<FString> RuntimeFields = {
		TEXT("Agent"), TEXT("Locating"), TEXT("Rotating"), TEXT("EventRT"), TEXT("Visualizing"),
		TEXT("Animating"), TEXT("Tracing"), TEXT("TracingShared"), TEXT("Moving"), TEXT("Navigating"),
		TEXT("Gathering"), TEXT("Appearing"), TEXT("Patrolling"), TEXT("Reinforcing"),
		TEXT("Returning"), TEXT("Attacking"), TEXT("Debuffing"), TEXT("BeingSelect"),
		TEXT("BeingHit"), TEXT("Dying")
	};
	static const TSet<FString> SharedFields = {
		TEXT("PhysicsShared"), TEXT("CurveShared"), TEXT("TrafficShared"), TEXT("LODShared"), TEXT("AnimShared")
	};
	static const TSet<FString> DeprecatedFields = {
		TEXT("Scale"), TEXT("Curves"), TEXT("Animation"), TEXT("Render"), TEXT("RenderShared")
	};

	if (Name.StartsWith(TEXT("bShow")) || Name == TEXT("bHideRuntimeFragments"))
	{
		return EFieldRole::VisibilityControl;
	}
	if (Name.StartsWith(TEXT("bMigrated")) || Name == TEXT("DataVersion"))
	{
		return EFieldRole::SystemOwned;
	}
	if (RuntimeFields.Contains(Name))
	{
		return EFieldRole::RuntimeState;
	}
	if (SharedFields.Contains(Name))
	{
		return EFieldRole::SharedTypeConfig;
	}
	if (DeprecatedFields.Contains(Name))
	{
		return EFieldRole::Deprecated;
	}
	if (Name == TEXT("ExtraData"))
	{
		return EFieldRole::ExtensionConfig;
	}
	if (Name == TEXT("SubType") || Name == TEXT("StyleType") || Name == TEXT("Scaling"))
	{
		return EFieldRole::SpawnInitialState;
	}
	return EFieldRole::AuthoringConfig;
}

static bool ShouldSkipTopLevelField(const FString& Name, const TSharedPtr<FJsonObject>& Options)
{
	bool bOmitRuntime = true;
	bool bOmitSystem = true;
	bool bOmitDeprecated = true;
	bool bOmitVisibility = true;

	if (Options.IsValid())
	{
		Options->TryGetBoolField(TEXT("omit_runtime"), bOmitRuntime);
		Options->TryGetBoolField(TEXT("omit_system"), bOmitSystem);
		Options->TryGetBoolField(TEXT("omit_deprecated"), bOmitDeprecated);
		Options->TryGetBoolField(TEXT("omit_visibility"), bOmitVisibility);
	}

	const EFieldRole Role = ClassifyTopLevelField(Name);
	return (Role == EFieldRole::RuntimeState && bOmitRuntime)
		|| (Role == EFieldRole::SystemOwned && bOmitSystem)
		|| (Role == EFieldRole::Deprecated && bOmitDeprecated)
		|| (Role == EFieldRole::VisibilityControl && bOmitVisibility);
}

static bool ShouldSkipNestedField(const FString& Name)
{
	return Name == TEXT("DeterministicHash")
		|| Name.StartsWith(TEXT("bMigrated"))
		|| Name.StartsWith(TEXT("Deprecated"))
		|| Name.EndsWith(TEXT("_DEPRECATED"));
}

static FString ToJsonString(const TSharedPtr<FJsonObject>& Root)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (Root.IsValid())
	{
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}
	return Output;
}

static FString ToJsonString(const TArray<TSharedPtr<FJsonValue>>& Array)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Array, Writer);
	return Output;
}

static TSharedPtr<FJsonObject> MakeSuccess()
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	return Root;
}

static FString MakeErrorJson(const FString& Error)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), false);
	Root->SetStringField(TEXT("error"), Error);
	return ToJsonString(Root);
}

static TSharedPtr<FJsonObject> ParseObject(const FString& Json)
{
	if (Json.TrimStartAndEnd().IsEmpty())
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Object;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return nullptr;
	}
	return Object;
}

static TArray<TSharedPtr<FJsonValue>> ParseArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(FieldName, Array))
	{
		return *Array;
	}
	return {};
}

static bool TryGetIntField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32& OutValue)
{
	double Number = 0.0;
	if (Object.IsValid() && Object->TryGetNumberField(FieldName, Number))
	{
		OutValue = static_cast<int32>(Number);
		return true;
	}
	return false;
}

static FString GetSavedRoot()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MassBattleEditorMCP"));
}

static FString GetAuditDir()
{
	return FPaths::Combine(GetSavedRoot(), TEXT("Audit"));
}

static FString GetExportDir()
{
	return FPaths::Combine(GetSavedRoot(), TEXT("Exports"));
}

static FString GetPluginRootDir()
{
	return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("MassBattleEditorMCP"));
}

static FString GetUnitManagementStyleDir()
{
	return FPaths::Combine(GetPluginRootDir(), TEXT("Resources"), TEXT("UnitManagementStyles"));
}

static TArray<FString> ReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	TArray<FString> Result;
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(FieldName, Array))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const FString Text = Value->AsString();
			if (!Text.IsEmpty())
			{
				Result.Add(Text);
			}
		}
	}
	return Result;
}

static TSharedPtr<FJsonObject> LoadJsonFileObject(const FString& FilePath)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		return nullptr;
	}
	return ParseObject(JsonText);
}

static FString FindConfigFileById(const FString& Directory, const FString& ConfigId)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.json"), true, false);
	Files.Sort();

	const FString WantedId = ConfigId.IsEmpty() ? TEXT("default") : ConfigId;
	for (const FString& File : Files)
	{
		const FString BaseName = FPaths::GetBaseFilename(File);
		if (BaseName.Equals(WantedId, ESearchCase::IgnoreCase) || BaseName.StartsWith(WantedId + TEXT("."), ESearchCase::IgnoreCase))
		{
			return File;
		}

		TSharedPtr<FJsonObject> Config = LoadJsonFileObject(File);
		FString Id;
		if (Config.IsValid() && Config->TryGetStringField(TEXT("id"), Id) && Id.Equals(WantedId, ESearchCase::IgnoreCase))
		{
			return File;
		}
	}
	return FString();
}

static TSharedPtr<FJsonObject> LoadUnitStyleConfig(const TSharedPtr<FJsonObject>& Options)
{
	FString StyleId = TEXT("default");
	if (Options.IsValid())
	{
		Options->TryGetStringField(TEXT("style_profile"), StyleId);
		Options->TryGetStringField(TEXT("style_config"), StyleId);
		Options->TryGetStringField(TEXT("unit_style"), StyleId);
	}

	const FString FilePath = FindConfigFileById(GetUnitManagementStyleDir(), StyleId);
	return FilePath.IsEmpty() ? nullptr : LoadJsonFileObject(FilePath);
}

static FString GetObjectPathString(const UObject* Object)
{
	return Object ? Object->GetPathName() : FString();
}

static FString NormalizeObjectPath(const FString& InPath)
{
	FString Path = InPath.TrimStartAndEnd();
	if (Path.IsEmpty() || Path.Contains(TEXT(".")))
	{
		return Path;
	}

	FString PackagePath;
	FString AssetName;
	if (Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return Path + TEXT(".") + AssetName;
	}
	return Path;
}

static UMassBattleAgentConfigDataAsset* LoadUnit(const FString& UnitPath, FString& OutError)
{
	UObject* Object = FSoftObjectPath(NormalizeObjectPath(UnitPath)).TryLoad();
	UMassBattleAgentConfigDataAsset* Unit = Cast<UMassBattleAgentConfigDataAsset>(Object);
	if (AMassBattleUnitSource* Source = AMassBattleUnitSource::Resolve(Object))
	{
		if (!Source->GetSourceAsset())
		{
			OutError = TEXT("The source Actor must belong to a Blueprint asset.");
			return nullptr;
		}
		Unit = Source->UnitData;
	}
	if (!Unit)
	{
		OutError = FString::Printf(TEXT("Failed to load UMassBattleAgentConfigDataAsset: %s"), *UnitPath);
	}
	return Unit;
}

static TArray<FName> DefaultScanRoots(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FName> Roots;
	const TSharedPtr<FJsonObject> StyleConfig = LoadUnitStyleConfig(Options);
	for (const FString& Root : ReadStringArrayField(StyleConfig, TEXT("default_scan_roots")))
	{
		Roots.Add(FName(*Root));
	}
	if (Roots.IsEmpty())
	{
		Roots = { FName(TEXT("/MassBattle/Demo")), FName(TEXT("/MassBattle/Test")) };
	}
	return Roots;
}

static TArray<FName> ParseRoots(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FName> Roots;
	const TArray<TSharedPtr<FJsonValue>>* RootArray = nullptr;
	if (Options.IsValid() && Options->TryGetArrayField(TEXT("roots"), RootArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RootArray)
		{
			FString Root = Value->AsString();
			if (!Root.IsEmpty())
			{
				Roots.Add(FName(*Root));
			}
		}
	}

	if (Roots.IsEmpty())
	{
		Roots = DefaultScanRoots(Options);
		bool bIncludeProject = false;
		if (Options.IsValid() && Options->TryGetBoolField(TEXT("include_project"), bIncludeProject) && bIncludeProject)
		{
			const TSharedPtr<FJsonObject> StyleConfig = LoadUnitStyleConfig(Options);
			TArray<FString> ProjectRoots = ReadStringArrayField(StyleConfig, TEXT("project_scan_roots"));
			if (ProjectRoots.IsEmpty())
			{
				ProjectRoots.Add(TEXT("/Game"));
			}
			for (const FString& ProjectRoot : ProjectRoots)
			{
				Roots.Add(FName(*ProjectRoot));
			}
		}
	}
	return Roots;
}

static IAssetRegistry& GetAssetRegistry()
{
	return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
}

static TArray<FAssetData> ScanUnitAssets(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FName> Roots = ParseRoots(Options);
	TArray<FString> RootStrings;
	for (const FName& Root : Roots)
	{
		RootStrings.Add(Root.ToString());
	}

	IAssetRegistry& Registry = GetAssetRegistry();
	if (!RootStrings.IsEmpty())
	{
		Registry.ScanPathsSynchronous(RootStrings, true);
	}

	FARFilter Filter;
	Filter.ClassPaths.Add(UMassBattleAgentConfigDataAsset::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths = Roots;

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Filter.ClassPaths = { AMassBattleUnitSource::StaticClass()->GetClassPathName() };
	TArray<FAssetData> Sources;
	UAssetRegistryHelpers::GetBlueprintAssets(Filter, Sources);
	Assets.Append(Sources);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});
	return Assets;
}

static FProperty* FindPropertyBySegment(UStruct* Struct, const FString& Segment)
{
	if (!Struct)
	{
		return nullptr;
	}

	if (FProperty* Exact = Struct->FindPropertyByName(FName(*Segment)))
	{
		return Exact;
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetName().Equals(Segment, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

static FProperty* FindRootProperty(UObject* Object, const FString& Path)
{
	int32 SeparatorIndex = 0;
	const FString RootName = Path.FindChar(TEXT('.'), SeparatorIndex) ? Path.Left(SeparatorIndex) : Path;
	return FindPropertyBySegment(Object->GetClass(), RootName);
}

static bool IsNumericSegment(const FString& Segment, int32& OutIndex)
{
	if (Segment.IsNumeric())
	{
		OutIndex = FCString::Atoi(*Segment);
		return true;
	}
	return false;
}

static bool ResolvePropertyPath(void* ContainerPtr, UStruct* ContainerStruct, const FString& Path, FResolvedPropertyPath& Out, void* StagedRootValue = nullptr)
{
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.IsEmpty())
	{
		Out.Error = TEXT("Empty property path");
		return false;
	}

	void* CurrentPtr = ContainerPtr;
	UStruct* CurrentStruct = ContainerStruct;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FString& Segment = Segments[Index];
		FProperty* Property = FindPropertyBySegment(CurrentStruct, Segment);
		if (!Property)
		{
			Out.Error = FString::Printf(TEXT("Property segment '%s' not found under %s"), *Segment, CurrentStruct ? *CurrentStruct->GetName() : TEXT("<none>"));
			return false;
		}

		void* ValuePtr = Index == 0 && StagedRootValue
			? StagedRootValue
			: Property->ContainerPtrToValuePtr<void>(CurrentPtr);
		const bool bLast = Index == Segments.Num() - 1;
		if (bLast)
		{
			Out.Property = Property;
			Out.ValuePtr = ValuePtr;
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			CurrentStruct = StructProperty->Struct;
			CurrentPtr = ValuePtr;
			continue;
		}

		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Index + 1 >= Segments.Num())
			{
				Out.Error = FString::Printf(TEXT("Array property '%s' requires an index"), *Segment);
				return false;
			}

			int32 ArrayIndex = INDEX_NONE;
			if (!IsNumericSegment(Segments[Index + 1], ArrayIndex))
			{
				Out.Error = FString::Printf(TEXT("Array property '%s' requires numeric index, got '%s'"), *Segment, *Segments[Index + 1]);
				return false;
			}

			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			if (!Helper.IsValidIndex(ArrayIndex))
			{
				Out.Error = FString::Printf(TEXT("Array index %d is out of range for '%s'"), ArrayIndex, *Segment);
				return false;
			}

			void* ElementPtr = Helper.GetRawPtr(ArrayIndex);
			Index++;
			const bool bArrayElementLast = Index == Segments.Num() - 1;
			if (bArrayElementLast)
			{
				Out.Property = ArrayProperty->Inner;
				Out.ValuePtr = ElementPtr;
				return true;
			}

			if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProperty->Inner))
			{
				CurrentStruct = InnerStruct->Struct;
				CurrentPtr = ElementPtr;
				continue;
			}

			Out.Error = FString::Printf(TEXT("Array property '%s' element is not a struct"), *Segment);
			return false;
		}

		Out.Error = FString::Printf(TEXT("Property '%s' is not a struct/array and cannot contain '%s'"), *Property->GetName(), *Segments[Index + 1]);
		return false;
	}

	Out.Error = FString::Printf(TEXT("Unable to resolve property path: %s"), *Path);
	return false;
}

static FString ExportProperty(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return FString();
	}

	FString Text;
	Property->ExportTextItem_Direct(Text, ValuePtr, nullptr, nullptr, PPF_None);
	return Text;
}

static bool TryExportPath(UObject* Object, const FString& Path, FString& OutText)
{
	FResolvedPropertyPath Resolved;
	if (!ResolvePropertyPath(Object, Object->GetClass(), Path, Resolved))
	{
		return false;
	}
	OutText = ExportProperty(Resolved.Property, Resolved.ValuePtr);
	return true;
}

static FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return TEXT("");
	}

	switch (Value->Type)
	{
	case EJson::String:
		return Value->AsString();
	case EJson::Number:
		return FString::SanitizeFloat(Value->AsNumber());
	case EJson::Boolean:
		return Value->AsBool() ? TEXT("True") : TEXT("False");
	case EJson::Object:
	{
		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), Writer);
		return Output;
	}
	case EJson::Array:
		return ToJsonString(Value->AsArray());
	case EJson::Null:
	default:
		return TEXT("");
	}
}

static bool ApplyImportText(FProperty* Property, void* ValuePtr, const FString& ImportText, UObject* Owner, FString& OutError)
{
	if (!Property || !ValuePtr)
	{
		OutError = TEXT("Invalid property target");
		return false;
	}

	const TCHAR* Result = Property->ImportText_Direct(*ImportText, ValuePtr, Owner, PPF_None);
	if (!Result)
	{
		OutError = FString::Printf(TEXT("Failed to import '%s' into %s"), *ImportText, *Property->GetName());
		return false;
	}
	return true;
}

static bool ApplyJsonValueToProperty(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, UObject* Owner, FString& OutError)
{
	if (!Property || !ValuePtr || !Value.IsValid())
	{
		OutError = TEXT("Invalid JSON property assignment target");
		return false;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (Value->Type != EJson::Object)
		{
			return ApplyImportText(Property, ValuePtr, JsonValueToImportText(Value), Owner, OutError);
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			FProperty* Child = FindPropertyBySegment(StructProperty->Struct, Pair.Key);
			if (!Child)
			{
				OutError = FString::Printf(TEXT("Struct property '%s' has no field '%s'"), *StructProperty->GetName(), *Pair.Key);
				return false;
			}

			void* ChildPtr = Child->ContainerPtrToValuePtr<void>(ValuePtr);
			if (!ApplyJsonValueToProperty(Child, ChildPtr, Pair.Value, Owner, OutError))
			{
				return false;
			}
		}
		return true;
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (Value->Type != EJson::Array)
		{
			OutError = FString::Printf(TEXT("Array property '%s' requires a JSON array"), *ArrayProperty->GetName());
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		Helper.EmptyValues();
		const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
		for (const TSharedPtr<FJsonValue>& ElementValue : Array)
		{
			const int32 NewIndex = Helper.AddValue();
			if (!ApplyJsonValueToProperty(ArrayProperty->Inner, Helper.GetRawPtr(NewIndex), ElementValue, Owner, OutError))
			{
				return false;
			}
		}
		return true;
	}

	return ApplyImportText(Property, ValuePtr, JsonValueToImportText(Value), Owner, OutError);
}

static bool ApplyPatch(UObject* Object, const TSharedPtr<FJsonObject>& Patch, FString& OutError,
	FPatchPreview* OutPreview = nullptr, void* StagedRootValue = nullptr)
{
	const FString Path = Patch->GetStringField(TEXT("path"));
	FResolvedPropertyPath Resolved;
	if (!ResolvePropertyPath(Object, Object->GetClass(), Path, Resolved, StagedRootValue))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *Resolved.Error);
		return false;
	}

	const FString Operation = Patch->GetStringField(TEXT("op"));
	if (OutPreview)
	{
		OutPreview->Path = Path;
		OutPreview->Operation = Operation;
		OutPreview->Before = ExportProperty(Resolved.Property, Resolved.ValuePtr);
	}

	const TSharedPtr<FJsonValue> Value = Patch->TryGetField(TEXT("value"));
	FString Error;
	if (Operation == TEXT("append"))
	{
		FArrayProperty* ArrayProperty = CastFieldChecked<FArrayProperty>(Resolved.Property);
		FScriptArrayHelper Helper(ArrayProperty, Resolved.ValuePtr);
		const int32 Index = Helper.AddValue();
		if (!ApplyJsonValueToProperty(ArrayProperty->Inner, Helper.GetRawPtr(Index), Value, Object, Error))
		{
			OutError = FString::Printf(TEXT("%s.%d: %s"), *Path, Index, *Error);
			return false;
		}
	}
	else if (!ApplyJsonValueToProperty(Resolved.Property, Resolved.ValuePtr, Value, Object, Error))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *Error);
		return false;
	}

	if (OutPreview)
	{
		OutPreview->After = ExportProperty(Resolved.Property, Resolved.ValuePtr);
	}
	return true;
}

static TSharedPtr<FJsonObject> PatchPreviewToJson(const FPatchPreview& Preview)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), Preview.Path);
	Object->SetStringField(TEXT("operation"), Preview.Operation);
	Object->SetStringField(TEXT("before"), Preview.Before);
	Object->SetStringField(TEXT("after"), Preview.After);
	return Object;
}

static TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Property, const void* ValuePtr, int32 Depth)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(ValuePtr));
	}
	if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
	}
	if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValuePtr).ToString());
	}
	if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
	}
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const int64 Raw = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(EnumProperty->GetEnum()->GetNameStringByValue(Raw));
	}
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (ByteProperty->Enum)
		{
			return MakeShared<FJsonValueString>(ByteProperty->Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(ValuePtr)));
		}
		return MakeShared<FJsonValueNumber>(ByteProperty->GetPropertyValue(ValuePtr));
	}
	if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsInteger())
		{
			return MakeShared<FJsonValueNumber>(static_cast<double>(Numeric->GetSignedIntPropertyValue(ValuePtr)));
		}
		return MakeShared<FJsonValueNumber>(Numeric->GetFloatingPointPropertyValue(ValuePtr));
	}
	if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
	{
		return MakeShared<FJsonValueString>(SoftObject->GetPropertyValue(ValuePtr).ToString());
	}
	if (FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
	{
		return MakeShared<FJsonValueString>(SoftClass->GetPropertyValue(ValuePtr).ToString());
	}
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		return MakeShared<FJsonValueString>(GetObjectPathString(ObjectProperty->GetObjectPropertyValue(ValuePtr)));
	}
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (Depth <= 0)
		{
			return MakeShared<FJsonValueString>(ExportProperty(Property, ValuePtr));
		}

		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			FProperty* Child = *It;
			if (ShouldSkipNestedField(Child->GetName()))
			{
				continue;
			}
			Object->SetField(Child->GetName(), PropertyToJsonValue(Child, Child->ContainerPtrToValuePtr<void>(ValuePtr), Depth - 1));
		}
		return MakeShared<FJsonValueObject>(Object);
	}
	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Array;
		const int32 Limit = FMath::Min(Helper.Num(), 64);
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			Array.Add(PropertyToJsonValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Depth - 1));
		}
		if (Helper.Num() > Limit)
		{
			Array.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("<truncated %d more>"), Helper.Num() - Limit)));
		}
		return MakeShared<FJsonValueArray>(Array);
	}

	return MakeShared<FJsonValueString>(ExportProperty(Property, ValuePtr));
}

static TArray<FString> GetFallbackSimpleTopLevelFields()
{
	return {
		TEXT("SubType"),
		TEXT("StyleType"),
		TEXT("Scaling"),
		TEXT("Collider"),
		TEXT("PhysicsShared"),
		TEXT("Health"),
		TEXT("Move"),
		TEXT("Attack"),
		TEXT("Damage"),
		TEXT("Defence"),
		TEXT("Trace"),
		TEXT("Visualize"),
		TEXT("LODShared"),
		TEXT("AnimShared")
	};
}

static TSet<FString> GetSimpleTopLevelFields(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FString> Fields = ReadStringArrayField(LoadUnitStyleConfig(Options), TEXT("simple_top_level_fields"));
	if (Fields.IsEmpty())
	{
		Fields = GetFallbackSimpleTopLevelFields();
	}

	TSet<FString> Result;
	for (const FString& Field : Fields)
	{
		if (!Field.IsEmpty())
		{
			Result.Add(Field);
		}
	}
	return Result;
}

static bool JsonValueIsEmptyObjectOrArray(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return true;
	}
	if (Value->Type == EJson::Object)
	{
		TSharedPtr<FJsonObject> Object = Value->AsObject();
		return !Object.IsValid() || Object->Values.IsEmpty();
	}
	if (Value->Type == EJson::Array)
	{
		return Value->AsArray().IsEmpty();
	}
	return false;
}

static TSharedPtr<FJsonObject> InstancedStructToJson(const FInstancedStruct& InstancedStruct, int32 Depth)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	const UScriptStruct* ScriptStruct = InstancedStruct.GetScriptStruct();
	Object->SetStringField(TEXT("Struct"), ScriptStruct ? ScriptStruct->GetPathName() : FString());
	if (!InstancedStruct.IsValid() || !ScriptStruct || !InstancedStruct.GetMemory())
	{
		return Object;
	}

	if (Depth <= 0)
	{
		FString ValueText;
		FInstancedStruct DefaultValue;
		InstancedStruct.ExportTextItem(ValueText, DefaultValue, nullptr, PPF_None, nullptr);
		Object->SetStringField(TEXT("Value"), ValueText);
		return Object;
	}

	TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
	{
		FProperty* Child = *It;
		if (ShouldSkipNestedField(Child->GetName()))
		{
			continue;
		}
		Value->SetField(Child->GetName(), PropertyToJsonValue(Child, Child->ContainerPtrToValuePtr<void>(InstancedStruct.GetMemory()), Depth - 1));
	}
	Object->SetObjectField(TEXT("Value"), Value);
	return Object;
}

static TArray<TSharedPtr<FJsonValue>> InstancedStructArrayToJson(const TArray<FInstancedStruct>& Values, int32 Depth)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FInstancedStruct& Value : Values)
	{
		Array.Add(MakeShared<FJsonValueObject>(InstancedStructToJson(Value, Depth)));
	}
	return Array;
}

static TSharedPtr<FJsonObject> ExtraDataToJson(const FMassBattleTemplate& ExtraData, int32 Depth, bool bOmitDefaults)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	if (!bOmitDefaults || !ExtraData.Tags.IsEmpty())
	{
		Object->SetArrayField(TEXT("Tags"), InstancedStructArrayToJson(ExtraData.Tags, Depth));
	}
	if (!bOmitDefaults || !ExtraData.Fragments.IsEmpty())
	{
		Object->SetArrayField(TEXT("Fragments"), InstancedStructArrayToJson(ExtraData.Fragments, Depth));
	}
	if (!bOmitDefaults || !ExtraData.MutableSharedFragments.IsEmpty())
	{
		Object->SetArrayField(TEXT("MutableSharedFragments"), InstancedStructArrayToJson(ExtraData.MutableSharedFragments, Depth));
	}
	if (!bOmitDefaults || !ExtraData.ConstSharedFragments.IsEmpty())
	{
		Object->SetArrayField(TEXT("ConstSharedFragments"), InstancedStructArrayToJson(ExtraData.ConstSharedFragments, Depth));
	}
	if (!bOmitDefaults || !ExtraData.BattleFlags.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Flags;
		const UEnum* Enum = StaticEnum<EBattleFlags>();
		for (const EBattleFlags Flag : ExtraData.BattleFlags)
		{
			Flags.Add(MakeShared<FJsonValueString>(Enum ? Enum->GetNameStringByValue(static_cast<int64>(Flag)) : FString::FromInt(static_cast<int32>(Flag))));
		}
		Object->SetArrayField(TEXT("BattleFlags"), Flags);
	}
	return Object;
}

static TSharedPtr<FJsonValue> PropertyToEffectiveJsonValue(FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr, int32 Depth, bool bOmitDefaults)
{
	if (!Property || !ValuePtr)
	{
		return nullptr;
	}

	if (bOmitDefaults && DefaultValuePtr && Property->Identical(ValuePtr, DefaultValuePtr, PPF_None))
	{
		return nullptr;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (Depth <= 0)
		{
			return PropertyToJsonValue(Property, ValuePtr, Depth);
		}

		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			FProperty* Child = *It;
			if (ShouldSkipNestedField(Child->GetName()))
			{
				continue;
			}
			const void* ChildValuePtr = Child->ContainerPtrToValuePtr<void>(ValuePtr);
			const void* ChildDefaultPtr = DefaultValuePtr ? Child->ContainerPtrToValuePtr<void>(DefaultValuePtr) : nullptr;
			TSharedPtr<FJsonValue> ChildValue = PropertyToEffectiveJsonValue(Child, ChildValuePtr, ChildDefaultPtr, Depth - 1, bOmitDefaults);
			// A full read with include_defaults=true is also used for authoritative
			// verification. Preserve explicit empty containers in that mode so a
			// cleared array can be distinguished from a field that was not read.
			if (ChildValue.IsValid() && (!bOmitDefaults || !JsonValueIsEmptyObjectOrArray(ChildValue)))
			{
				Object->SetField(Child->GetName(), ChildValue);
			}
		}
		if (Object->Values.IsEmpty())
		{
			return nullptr;
		}
		return MakeShared<FJsonValueObject>(Object);
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		if (bOmitDefaults && Helper.Num() == 0)
		{
			return nullptr;
		}
		return PropertyToJsonValue(Property, ValuePtr, Depth);
	}

	return PropertyToJsonValue(Property, ValuePtr, Depth);
}

static TArray<FString> ParseStringArrayOption(const TSharedPtr<FJsonObject>& Options, const FString& FieldName)
{
	TArray<FString> Result;
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Options.IsValid() && Options->TryGetArrayField(FieldName, Array))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const FString Text = Value->AsString();
			if (!Text.IsEmpty())
			{
				Result.Add(Text);
			}
		}
	}
	return Result;
}

static bool IsDetailedMode(const TSharedPtr<FJsonObject>& Options)
{
	FString Detail = TEXT("simple");
	if (Options.IsValid())
	{
		Options->TryGetStringField(TEXT("detail"), Detail);
		Options->TryGetStringField(TEXT("mode"), Detail);
		bool bCompact = true;
		if (Options->TryGetBoolField(TEXT("compact"), bCompact) && !bCompact)
		{
			Detail = TEXT("detailed");
		}
	}
	return Detail.Equals(TEXT("detailed"), ESearchCase::IgnoreCase)
		|| Detail.Equals(TEXT("detail"), ESearchCase::IgnoreCase)
		|| Detail.Equals(TEXT("full"), ESearchCase::IgnoreCase);
}

static TArray<FString> GetRequestedObjects(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FString> Objects = ParseStringArrayOption(Options, TEXT("objects"));
	FString SingleObject;
	if (Options.IsValid() && Options->TryGetStringField(TEXT("object"), SingleObject) && !SingleObject.IsEmpty())
	{
		Objects.Add(SingleObject);
	}
	return Objects;
}

static FString GetDetailLabel(const TSharedPtr<FJsonObject>& Options)
{
	if (!GetRequestedObjects(Options).IsEmpty())
	{
		return TEXT("objects");
	}
	return IsDetailedMode(Options) ? TEXT("detailed") : TEXT("simple");
}

static bool ShouldIncludeObjectInUnitJson(const FString& FieldName, const TSharedPtr<FJsonObject>& Options)
{
	const TArray<FString> RequestedObjects = GetRequestedObjects(Options);
	if (!RequestedObjects.IsEmpty())
	{
		for (const FString& ObjectName : RequestedObjects)
		{
			if (FieldName.Equals(ObjectName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	if (IsDetailedMode(Options))
	{
		return true;
	}

	return GetSimpleTopLevelFields(Options).Contains(FieldName);
}

static TSharedPtr<FJsonObject> BuildSourceAlignedUnitJson(UMassBattleAgentConfigDataAsset* Unit, const TSharedPtr<FJsonObject>& Options)
{
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	if (!Unit)
	{
		return Fields;
	}

	const UMassBattleAgentConfigDataAsset* DefaultUnit = GetDefault<UMassBattleAgentConfigDataAsset>();
	bool bIncludeDefaults = false;
	if (Options.IsValid())
	{
		Options->TryGetBoolField(TEXT("include_defaults"), bIncludeDefaults);
	}
	const bool bOmitDefaults = !bIncludeDefaults;
	const bool bDetailed = IsDetailedMode(Options);
	const bool bSpecificObjects = !GetRequestedObjects(Options).IsEmpty();
	const int32 Depth = bDetailed || bSpecificObjects ? MaxPropertyDepth : 3;

	for (TFieldIterator<FProperty> It(Unit->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		const FString FieldName = Property->GetName();
		if (ShouldSkipTopLevelField(FieldName, Options) || !ShouldIncludeObjectInUnitJson(FieldName, Options))
		{
			continue;
		}

		if (FieldName == TEXT("ExtraData"))
		{
			TSharedPtr<FJsonObject> ExtraDataJson = ExtraDataToJson(Unit->ExtraData, Depth, bOmitDefaults);
			if (!ExtraDataJson->Values.IsEmpty())
			{
				Fields->SetObjectField(TEXT("ExtraData"), ExtraDataJson);
			}
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Unit);
		const void* DefaultValuePtr = DefaultUnit ? Property->ContainerPtrToValuePtr<void>(DefaultUnit) : nullptr;
		TSharedPtr<FJsonValue> Value = PropertyToEffectiveJsonValue(Property, ValuePtr, DefaultValuePtr, Depth, bOmitDefaults);
		if (Value.IsValid() && (!bOmitDefaults || !JsonValueIsEmptyObjectOrArray(Value)))
		{
			Fields->SetField(FieldName, Value);
		}
	}
	return Fields;
}

static TSharedPtr<FJsonObject> BuildIgnorePolicyJson()
{
	TSharedPtr<FJsonObject> Policy = MakeShared<FJsonObject>();
	Policy->SetBoolField(TEXT("omit_runtime"), true);
	Policy->SetBoolField(TEXT("omit_system"), true);
	Policy->SetBoolField(TEXT("omit_deprecated"), true);
	Policy->SetBoolField(TEXT("omit_visibility"), true);
	Policy->SetStringField(TEXT("reason"), TEXT("Default output hides runtime cache, system migration state, deprecated migration fields, and editor visibility toggles to reduce context."));

	TArray<TSharedPtr<FJsonValue>> Runtime;
	const TArray<FString> RuntimeExamples = {
		TEXT("Moving"), TEXT("Attacking"), TEXT("Animating"), TEXT("Locating"),
		TEXT("Rotating"), TEXT("BeingHit"), TEXT("Dying"), TEXT("EventRT")
	};
	for (const FString& Name : RuntimeExamples)
	{
		Runtime.Add(MakeShared<FJsonValueString>(Name));
	}
	Policy->SetArrayField(TEXT("runtime_examples"), Runtime);
	return Policy;
}

static FString InferStyleFamily(const FString& ObjectPath)
{
	const FString Lower = ObjectPath.ToLower();
	if (Lower.Contains(TEXT("tank"))) return TEXT("armored");
	if (Lower.Contains(TEXT("soldier")) || Lower.Contains(TEXT("human"))) return TEXT("infantry");
	if (Lower.Contains(TEXT("zombie"))) return TEXT("creature");
	if (Lower.Contains(TEXT("helicopter")) || Lower.Contains(TEXT("fighterjet")) || Lower.Contains(TEXT("aircraft"))) return TEXT("air");
	if (Lower.Contains(TEXT("camp")) || Lower.Contains(TEXT("spawner")) || Lower.Contains(TEXT("building"))) return TEXT("building");
	if (Lower.Contains(TEXT("cannon")) || Lower.Contains(TEXT("machinegun")) || Lower.Contains(TEXT("rocketlauncher"))) return TEXT("weapon");
	return TEXT("default");
}

static void AddExportedPath(TSharedPtr<FJsonObject> Object, UObject* Source, const FString& JsonName, const FString& PropertyPath)
{
	FString Text;
	if (TryExportPath(Source, PropertyPath, Text))
	{
		Object->SetStringField(JsonName, Text);
	}
}

static TSharedPtr<FJsonObject> BuildUnitSummary(const FAssetData& AssetData, bool bLoadForFields, const TSharedPtr<FJsonObject>& Options)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	const FString ObjectPath = AssetData.GetObjectPathString();
	Object->SetStringField(TEXT("AssetName"), AssetData.AssetName.ToString());
	Object->SetStringField(TEXT("ObjectPath"), ObjectPath);
	Object->SetStringField(TEXT("PackagePath"), AssetData.PackagePath.ToString());
	Object->SetStringField(TEXT("Class"), AssetData.AssetClassPath.ToString());

	if (bLoadForFields)
	{
		FString Error;
		if (UMassBattleAgentConfigDataAsset* Unit = LoadUnit(ObjectPath, Error))
		{
			Object->SetObjectField(TEXT("Data"), BuildSourceAlignedUnitJson(Unit, Options));
		}
	}
	return Object;
}

static void AddRootsToJson(TSharedPtr<FJsonObject> Root, const TSharedPtr<FJsonObject>& Options)
{
	TArray<TSharedPtr<FJsonValue>> RootValues;
	for (const FName& RootPath : ParseRoots(Options))
	{
		RootValues.Add(MakeShared<FJsonValueString>(RootPath.ToString()));
	}
	Root->SetArrayField(TEXT("roots"), RootValues);
}

static bool SaveAsset(UObject* Asset, FString& OutError)
{
	if (AMassBattleUnitSource* Source = Asset ? Asset->GetTypedOuter<AMassBattleUnitSource>() : nullptr)
	{
		Asset = Source->GetSourceAsset();
	}
	if (!Asset)
	{
		OutError = TEXT("Invalid asset");
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Asset has no package");
		return false;
	}

	Package->MarkPackageDirty();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save package: %s"), *PackageFileName);
		return false;
	}
	return true;
}

static void AppendAuditRecord(const TSharedPtr<FJsonObject>& Record)
{
	IFileManager::Get().MakeDirectory(*GetAuditDir(), true);
	const FString AuditPath = FPaths::Combine(GetAuditDir(), TEXT("unit_changes.jsonl"));
	FString Line = ToJsonString(Record) + LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(Line, *AuditPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

static FString FindJsonFieldName(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	if (!Object.IsValid() || Object->HasField(Name))
	{
		return Name;
	}
	for (const auto& Field : Object->Values)
	{
		if (Field.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return Field.Key;
		}
	}
	return Name;
}

static bool HasJsonField(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	return Object.IsValid() && Object->HasField(FindJsonFieldName(Object, Name));
}

static TSharedPtr<FJsonObject> FindJsonObject(const TSharedPtr<FJsonObject>& Parent, const FString& Key)
{
	const TSharedPtr<FJsonObject>* Child = nullptr;
	return Parent.IsValid() && Parent->TryGetObjectField(FindJsonFieldName(Parent, Key), Child) && Child && Child->IsValid() ? *Child : nullptr;
}

static FString ResolveCreationTemplate(const TSharedPtr<FJsonObject>& Input, const TSharedPtr<FJsonObject>& Defaults, bool bSourceActor)
{
	FString TemplatePath;
	Input->TryGetStringField(TEXT("template_unit"), TemplatePath);
	if (TemplatePath.IsEmpty())
	{
		Input->TryGetStringField(TEXT("source_unit"), TemplatePath);
	}
	if (!TemplatePath.IsEmpty() || !Defaults.IsValid())
	{
		return TemplatePath;
	}

	FString Family;
	Input->TryGetStringField(TEXT("family"), Family);
	const TSharedPtr<FJsonObject> FamilyTemplates = FindJsonObject(Defaults,
		bSourceActor ? TEXT("default_source_unit_templates") : TEXT("default_unit_templates"));
	if (FamilyTemplates.IsValid() && !Family.IsEmpty())
	{
		FamilyTemplates->TryGetStringField(FindJsonFieldName(FamilyTemplates, Family), TemplatePath);
	}
	if (TemplatePath.IsEmpty() && FamilyTemplates.IsValid())
	{
		FamilyTemplates->TryGetStringField(TEXT("default"), TemplatePath);
	}
	if (TemplatePath.IsEmpty())
	{
		Defaults->TryGetStringField(bSourceActor ? TEXT("default_source_unit_template") : TEXT("default_unit_template"), TemplatePath);
	}
	return TemplatePath;
}

static bool ResolveCreateTarget(const TSharedPtr<FJsonObject>& Input, FString& OutAssetName, FString& OutPackagePath, FString& OutUnitPath, FString& OutError)
{
	Input->TryGetStringField(TEXT("asset_name"), OutAssetName);
	Input->TryGetStringField(TEXT("package_path"), OutPackagePath);
	Input->TryGetStringField(TEXT("unit_path"), OutUnitPath);
	Input->TryGetStringField(TEXT("target_unit"), OutUnitPath);
	Input->TryGetStringField(TEXT("new_unit_path"), OutUnitPath);

	if ((!OutAssetName.IsEmpty() && !OutPackagePath.IsEmpty()) || OutUnitPath.IsEmpty())
	{
		if (OutAssetName.IsEmpty() || OutPackagePath.IsEmpty())
		{
			OutError = TEXT("Unit creation requires either unit_path/new_unit_path or both asset_name and package_path");
			return false;
		}
		OutUnitPath = OutPackagePath / OutAssetName + TEXT(".") + OutAssetName;
		return true;
	}

	FString ObjectPath = OutUnitPath;
	FString PackageName;
	FString ObjectName;
	if (!ObjectPath.Split(TEXT("."), &PackageName, &ObjectName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		ObjectName = FPackageName::GetLongPackageAssetName(ObjectPath);
		PackageName = ObjectPath;
	}

	if (ObjectName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Could not resolve asset name from unit_path: %s"), *OutUnitPath);
		return false;
	}

	FString ParentPath;
	if (!PackageName.Split(TEXT("/"), &ParentPath, &OutAssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		OutError = FString::Printf(TEXT("Could not resolve package path from unit_path: %s"), *OutUnitPath);
		return false;
	}

	OutPackagePath = ParentPath;
	OutUnitPath = OutPackagePath / OutAssetName + TEXT(".") + OutAssetName;
	return true;
}

static void AddMergeError(TArray<TSharedPtr<FJsonValue>>& Errors, const FString& Path, const FString& Message)
{
	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("path"), Path);
	Error->SetStringField(TEXT("message"), Message);
	Errors.Add(MakeShared<FJsonValueObject>(Error));
}

static bool IsMergeMetadataField(const FString& FieldName)
{
	static const TSet<FString> MetadataFields = {
		TEXT("success"), TEXT("AssetName"), TEXT("ObjectPath"), TEXT("PackagePath"),
		TEXT("Class"), TEXT("detail"), TEXT("default_detail"), TEXT("default_ignore_policy"),
		TEXT("roots"), TEXT("count"), TEXT("total_scanned"), TEXT("units"),
		TEXT("options"), TEXT("merge_options"), TEXT("export_path"), TEXT("exported_unit"), TEXT("last_report")
	};
	return MetadataFields.Contains(FieldName);
}

static TSharedPtr<FJsonObject> ExtractMergeOptions(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid())
	{
		return MakeShared<FJsonObject>();
	}

	const TSharedPtr<FJsonObject>* Options = nullptr;
	if (Root->TryGetObjectField(TEXT("options"), Options) && Options && Options->IsValid())
	{
		return *Options;
	}
	if (Root->TryGetObjectField(TEXT("merge_options"), Options) && Options && Options->IsValid())
	{
		return *Options;
	}
	return MakeShared<FJsonObject>();
}

static TSharedPtr<FJsonObject> ExtractMergeDataObject(const TSharedPtr<FJsonObject>& Root, FString& OutError)
{
	if (!Root.IsValid())
	{
		OutError = TEXT("Merge JSON is not a valid object");
		return nullptr;
	}

	TSharedPtr<FJsonObject> DataObject = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonValue> Data = Root->TryGetField(TEXT("Data"));
	if (Data.IsValid())
	{
		if (Data->Type != EJson::Object)
		{
			OutError = TEXT("'Data' must be an object when provided");
			return nullptr;
		}
		DataObject->Values = Data->AsObject()->Values;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		if (Pair.Key == TEXT("Data") || IsMergeMetadataField(Pair.Key))
		{
			continue;
		}
		if (Data.IsValid() && HasJsonField(DataObject, Pair.Key))
		{
			OutError = FString::Printf(TEXT("Field '%s' occurs both in Data and at the root"), *Pair.Key);
			return nullptr;
		}
		DataObject->SetField(Pair.Key, Pair.Value);
	}
	return DataObject;
}

static void MergeCreationJson(const TSharedPtr<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Explicit)
{
	for (const auto& Field : Explicit->Values)
	{
		const FString Key = FindJsonFieldName(Target, Field.Key);
		const TSharedPtr<FJsonValue> Prior = Target->TryGetField(Key);
		if (Prior.IsValid() && Prior->Type == EJson::Object && Field.Value->Type == EJson::Object)
		{
			MergeCreationJson(Prior->AsObject(), Field.Value->AsObject());
		}
		else
		{
			Target->SetField(Key, Field.Value);
		}
	}
}

static TSharedPtr<FJsonValue> FindJsonValueAtPath(const TSharedPtr<FJsonObject>& Root, const FString& Path)
{
	if (!Root.IsValid() || Path.IsEmpty())
	{
		return nullptr;
	}

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Current = Root;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		TSharedPtr<FJsonValue> Value = Current->TryGetField(FindJsonFieldName(Current, Segments[Index]));
		if (!Value.IsValid())
		{
			return nullptr;
		}
		if (Index == Segments.Num() - 1)
		{
			return Value;
		}
		if (Value->Type != EJson::Object)
		{
			return nullptr;
		}
		Current = Value->AsObject();
	}
	return nullptr;
}

static bool TryGetJsonNumberAtPath(const TSharedPtr<FJsonObject>& Root, const FString& Path, double& OutValue)
{
	const TSharedPtr<FJsonValue> Value = FindJsonValueAtPath(Root, Path);
	if (!Value.IsValid() || Value->Type != EJson::Number)
	{
		return false;
	}
	OutValue = Value->AsNumber();
	return true;
}

static bool SetJsonNumberAtPath(const TSharedPtr<FJsonObject>& Root, const FString& Path, const double Value)
{
	if (!Root.IsValid() || Path.IsEmpty())
	{
		return false;
	}

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> Current = Root;
	for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
	{
		TSharedPtr<FJsonValue> Existing = Current->TryGetField(FindJsonFieldName(Current, Segments[Index]));
		if (Existing.IsValid())
		{
			if (Existing->Type != EJson::Object)
			{
				return false;
			}
			Current = Existing->AsObject();
		}
		else
		{
			TSharedPtr<FJsonObject> Child = MakeShared<FJsonObject>();
			Current->SetObjectField(Segments[Index], Child);
			Current = Child;
		}
	}

	Current->SetNumberField(FindJsonFieldName(Current, Segments.Last()), Value);
	return true;
}

static bool ApplyConfiguredNumberDerivations(
	const TSharedPtr<FJsonObject>& AuthoringDefaults,
	const TSharedPtr<FJsonObject>& UnitPatch,
	const TSharedPtr<FJsonObject>& ExplicitUnitPatch,
	TArray<TSharedPtr<FJsonValue>>& OutResults,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
	if (!AuthoringDefaults.IsValid() || !AuthoringDefaults->TryGetArrayField(TEXT("default_unit_patch_derivations"), Rules) || !Rules)
	{
		return true;
	}

	for (int32 RuleIndex = 0; RuleIndex < Rules->Num(); ++RuleIndex)
	{
		const TSharedPtr<FJsonValue>& RuleValue = (*Rules)[RuleIndex];
		if (!RuleValue.IsValid() || RuleValue->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d] must be an object."), RuleIndex);
			return false;
		}

		const TSharedPtr<FJsonObject> Rule = RuleValue->AsObject();
		FString SourcePath;
		if (!Rule->TryGetStringField(TEXT("source"), SourcePath) || SourcePath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d].source must be a non-empty JSON path."), RuleIndex);
			return false;
		}

		double SourceValue = 0.0;
		if (!TryGetJsonNumberAtPath(UnitPatch, SourcePath, SourceValue) || !FMath::IsFinite(SourceValue))
		{
			OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d] source '%s' is missing or is not a finite number."), RuleIndex, *SourcePath);
			return false;
		}

		double Multiplier = 1.0;
		if (Rule->HasField(TEXT("multiplier")) && (!Rule->TryGetNumberField(TEXT("multiplier"), Multiplier) || !FMath::IsFinite(Multiplier)))
		{
			OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d].multiplier must be a finite number."), RuleIndex);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
		if (!Rule->TryGetArrayField(TEXT("targets"), Targets) || !Targets || Targets->IsEmpty())
		{
			OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d].targets must contain at least one JSON path."), RuleIndex);
			return false;
		}

		for (int32 TargetIndex = 0; TargetIndex < Targets->Num(); ++TargetIndex)
		{
			FString TargetPath;
			if (!(*Targets)[TargetIndex].IsValid() || !(*Targets)[TargetIndex]->TryGetString(TargetPath) || TargetPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d].targets[%d] must be a non-empty JSON path."), RuleIndex, TargetIndex);
				return false;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("source"), SourcePath);
			Result->SetStringField(TEXT("target"), TargetPath);
			Result->SetNumberField(TEXT("source_value"), SourceValue);
			Result->SetNumberField(TEXT("multiplier"), Multiplier);

			if (FindJsonValueAtPath(ExplicitUnitPatch, TargetPath).IsValid())
			{
				Result->SetBoolField(TEXT("applied"), false);
				Result->SetStringField(TEXT("reason"), TEXT("explicit_target_preserved"));
				OutResults.Add(MakeShared<FJsonValueObject>(Result));
				continue;
			}

			const double DerivedValue = SourceValue * Multiplier;
			if (!FMath::IsFinite(DerivedValue) || !SetJsonNumberAtPath(UnitPatch, TargetPath, DerivedValue))
			{
				OutError = FString::Printf(TEXT("default_unit_patch_derivations[%d] could not write finite derived value to '%s'."), RuleIndex, *TargetPath);
				return false;
			}

			Result->SetBoolField(TEXT("applied"), true);
			Result->SetNumberField(TEXT("value"), DerivedValue);
			OutResults.Add(MakeShared<FJsonValueObject>(Result));
		}
	}
	return true;
}

static bool IsWritableTopLevelField(const FString& FieldName, const TSharedPtr<FJsonObject>& Options, FString& OutError)
{
	const EFieldRole Role = ClassifyTopLevelField(FieldName);
	if (Role == EFieldRole::RuntimeState || Role == EFieldRole::SystemOwned || Role == EFieldRole::Deprecated || Role == EFieldRole::VisibilityControl)
	{
		bool bAllowIgnoredFields = false;
		if (Options.IsValid())
		{
			Options->TryGetBoolField(TEXT("allow_ignored_fields"), bAllowIgnoredFields);
		}
		if (!bAllowIgnoredFields)
		{
			OutError = FString::Printf(TEXT("Field '%s' is %s and is blocked by default merge policy"), *FieldName, *RoleToString(Role));
			return false;
		}
	}
	return true;
}

static TSharedPtr<FJsonValue> CloneJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return MakeShared<FJsonValueNull>();
	}
	return Value;
}

static void AddSetPatchForMerge(UObject* Target, const FString& Path, const TSharedPtr<FJsonValue>& Value, TArray<TSharedPtr<FJsonObject>>& Patches, TArray<TSharedPtr<FJsonValue>>& Errors, UObject* ChangedSource = nullptr)
{
	FResolvedPropertyPath Resolved;
	if (!ResolvePropertyPath(Target, Target ? Target->GetClass() : nullptr, Path, Resolved))
	{
		AddMergeError(Errors, Path, Resolved.Error);
		return;
	}

	TSharedPtr<FJsonObject> Patch = MakeShared<FJsonObject>();
	Patch->SetStringField(TEXT("path"), Path);
	Patch->SetStringField(TEXT("op"), TEXT("set"));
	Patch->SetField(TEXT("value"), CloneJsonValue(Value));
	if (ChangedSource)
	{
		FResolvedPropertyPath Current;
		if (!ResolvePropertyPath(ChangedSource, ChangedSource->GetClass(), Path, Current))
		{
			AddMergeError(Errors, Path, Current.Error);
			return;
		}
		if (Resolved.Property->Identical(Resolved.ValuePtr, Current.ValuePtr, PPF_None)) return;
	}
	Patches.Add(Patch);
}

static void AddAppendPatchForMerge(UObject* Target, const FString& Path, const TSharedPtr<FJsonValue>& Value, TArray<TSharedPtr<FJsonObject>>& Patches, TArray<TSharedPtr<FJsonValue>>& Errors)
{
	FResolvedPropertyPath Resolved;
	if (!ResolvePropertyPath(Target, Target ? Target->GetClass() : nullptr, Path, Resolved))
	{
		AddMergeError(Errors, Path, Resolved.Error);
		return;
	}

	FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Resolved.Property);
	if (!ArrayProperty)
	{
		AddMergeError(Errors, Path, TEXT("append can only target an array property"));
		return;
	}

	TSharedPtr<FJsonObject> Patch = MakeShared<FJsonObject>();
	Patch->SetStringField(TEXT("path"), Path);
	Patch->SetStringField(TEXT("op"), TEXT("append"));
	Patch->SetField(TEXT("value"), CloneJsonValue(Value));
	Patches.Add(Patch);
}

static void FlattenMergeValue(UObject* Target, const UObject* DefaultObject, const FString& Path, const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>& Options, TArray<TSharedPtr<FJsonObject>>& Patches, TArray<TSharedPtr<FJsonValue>>& Errors, UObject* ChangedSource = nullptr)
{
	if (!Target || Path.IsEmpty())
	{
		AddMergeError(Errors, Path, TEXT("Invalid merge target or empty path"));
		return;
	}

	FResolvedPropertyPath Resolved;
	if (!ResolvePropertyPath(Target, Target->GetClass(), Path, Resolved))
	{
		AddMergeError(Errors, Path, Resolved.Error);
		return;
	}

	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		bool bNullResets = false;
		if (Options.IsValid() && (Options->TryGetBoolField(TEXT("null_resets"), bNullResets) || Options->TryGetBoolField(TEXT("null_deletes"), bNullResets)) && bNullResets)
		{
			FResolvedPropertyPath DefaultResolved;
			if (DefaultObject && ResolvePropertyPath(const_cast<UObject*>(DefaultObject), DefaultObject->GetClass(), Path, DefaultResolved))
			{
				AddSetPatchForMerge(Target, Path, PropertyToJsonValue(DefaultResolved.Property, DefaultResolved.ValuePtr, MaxPropertyDepth), Patches, Errors, ChangedSource);
			}
			else
			{
				AddMergeError(Errors, Path, TEXT("Cannot reset null value because the default property path was not resolved"));
			}
		}
		else
		{
			AddMergeError(Errors, Path, TEXT("Null merge values are ignored by default; pass null_resets=true to reset to class defaults"));
		}
		return;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Resolved.Property))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
			{
				FlattenMergeValue(Target, DefaultObject, Path + TEXT(".") + Pair.Key, Pair.Value, Options, Patches, Errors, ChangedSource);
			}
			return;
		}

		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Resolved.Property))
		{
			AddMergeError(Errors, Path, FString::Printf(TEXT("Array property '%s' requires a JSON array for indexed union merge"), *ArrayProperty->GetName()));
			return;
		}

		AddMergeError(Errors, Path, TEXT("Object value can only be merged into a struct property"));
		return;
	}

	if (Value->Type == EJson::Array)
	{
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Resolved.Property);
		if (!ArrayProperty)
		{
			AddMergeError(Errors, Path, TEXT("Array value can only be merged into an array property"));
			return;
		}

		bool bReplaceArrays = false;
		if (Options.IsValid())
		{
			Options->TryGetBoolField(TEXT("replace_arrays"), bReplaceArrays);
			Options->TryGetBoolField(TEXT("replace_array_values"), bReplaceArrays);
		}
		if (bReplaceArrays)
		{
			// Union merge remains the default. Explicit replacement is required for
			// authoritative manifests that need to remove inherited template entries,
			// including clearing Attack.SpawnProjectile with an empty JSON array.
			AddSetPatchForMerge(Target, Path, Value, Patches, Errors, ChangedSource);
			return;
		}

		FScriptArrayHelper Helper(ArrayProperty, Resolved.ValuePtr);
		const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
		bool bAppendArrays = true;
		if (Options.IsValid())
		{
			Options->TryGetBoolField(TEXT("append_arrays"), bAppendArrays);
			Options->TryGetBoolField(TEXT("allow_array_append"), bAppendArrays);
		}
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			const FString IndexPath = FString::Printf(TEXT("%s.%d"), *Path, Index);
			const TSharedPtr<FJsonValue>& ElementValue = Array[Index];
			if (!Helper.IsValidIndex(Index))
			{
				if (bAppendArrays && Index >= Helper.Num())
				{
					AddAppendPatchForMerge(Target, Path, ElementValue, Patches, Errors);
				}
				else
				{
					AddMergeError(Errors, IndexPath, TEXT("Union merge array index is out of range and append_arrays=false"));
				}
				continue;
			}

			if (ElementValue.IsValid() && ElementValue->Type == EJson::Object && CastField<FStructProperty>(ArrayProperty->Inner))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ElementValue->AsObject()->Values)
				{
					FlattenMergeValue(Target, DefaultObject, IndexPath + TEXT(".") + Pair.Key, Pair.Value, Options, Patches, Errors, ChangedSource);
				}
			}
			else
			{
				AddSetPatchForMerge(Target, IndexPath, ElementValue, Patches, Errors, ChangedSource);
			}
		}
		return;
	}

	AddSetPatchForMerge(Target, Path, Value, Patches, Errors, ChangedSource);
}

static TArray<TSharedPtr<FJsonObject>> BuildUnionMergePatches(UMassBattleAgentConfigDataAsset* Unit, const TSharedPtr<FJsonObject>& MergeRoot, const TSharedPtr<FJsonObject>& Options, TArray<TSharedPtr<FJsonValue>>& Errors, UObject* ChangedSource = nullptr)
{
	TArray<TSharedPtr<FJsonObject>> Patches;
	FString Error;
	TSharedPtr<FJsonObject> DataObject = ExtractMergeDataObject(MergeRoot, Error);
	if (!DataObject.IsValid())
	{
		AddMergeError(Errors, TEXT("Data"), Error);
		return Patches;
	}

	const UMassBattleAgentConfigDataAsset* DefaultUnit = GetDefault<UMassBattleAgentConfigDataAsset>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DataObject->Values)
	{
		FString RoleError;
		const FProperty* Property = FindRootProperty(Unit, Pair.Key);
		if (Property && !IsWritableTopLevelField(Property->GetName(), Options, RoleError))
		{
			AddMergeError(Errors, Pair.Key, RoleError);
			continue;
		}
		FlattenMergeValue(Unit, DefaultUnit, Pair.Key, Pair.Value, Options, Patches, Errors, ChangedSource);
	}
	return Patches;
}

/// @brief Owns transient storage for one reflected property value.
class FUnitPropertyValue
{
public:
	explicit FUnitPropertyValue(FProperty* InProperty)
		: Property(InProperty)
		, Data(FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()))
	{
		Property->InitializeValue(Data);
	}

	~FUnitPropertyValue()
	{
		Property->DestroyValue(Data);
		FMemory::Free(Data);
	}

	FUnitPropertyValue(const FUnitPropertyValue&) = delete;
	FUnitPropertyValue& operator=(const FUnitPropertyValue&) = delete;

	FProperty* const Property;
	void* const Data;
};

/// @brief Imports the submitted fields in scratch storage before changing the unit.
class FStagedUnitFields
{
public:
	bool Stage(UMassBattleAgentConfigDataAsset* Unit, const TArray<TSharedPtr<FJsonObject>>& Patches,
		FString& OutError, TArray<TSharedPtr<FJsonValue>>* OutChanges = nullptr)
	{
		for (const TSharedPtr<FJsonObject>& Patch : Patches)
		{
			const FString Path = Patch->GetStringField(TEXT("path"));
			FProperty* Property = FindRootProperty(Unit, Path);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property '%s' not found"), *Path);
				return false;
			}

			TUniquePtr<FStagedProperty>& Entry = Entries.FindOrAdd(Property);
			if (!Entry)
			{
				Entry = MakeUnique<FStagedProperty>(Unit, Property);
			}

			FPatchPreview Change;
			if (!ApplyPatch(Unit, Patch, OutError, OutChanges ? &Change : nullptr, Entry->Value.Data))
			{
				return false;
			}
			if (OutChanges)
			{
				OutChanges->Add(MakeShared<FJsonValueObject>(PatchPreviewToJson(Change)));
			}
		}
		return true;
	}

	void Apply()
	{
		for (const TPair<FProperty*, TUniquePtr<FStagedProperty>>& Pair : Entries)
		{
			const FStagedProperty& Entry = *Pair.Value;
			Pair.Key->CopyCompleteValue(Entry.Original.Data, Entry.Address);
			Pair.Key->CopyCompleteValue(Entry.Address, Entry.Value.Data);
		}
	}

	void Restore() const
	{
		for (const TPair<FProperty*, TUniquePtr<FStagedProperty>>& Pair : Entries)
		{
			Pair.Key->CopyCompleteValue(Pair.Value->Address, Pair.Value->Original.Data);
		}
	}

private:
	struct FStagedProperty
	{
		FStagedProperty(UMassBattleAgentConfigDataAsset* Unit, FProperty* Property)
			: Address(Property->ContainerPtrToValuePtr<void>(Unit))
			, Original(Property)
			, Value(Property)
		{
			Property->CopyCompleteValue(Value.Data, Address);
		}

		void* const Address;
		FUnitPropertyValue Original;
		FUnitPropertyValue Value;
	};

	TMap<FProperty*, TUniquePtr<FStagedProperty>> Entries;
};

static bool ResolveSourceExportDestination(AMassBattleUnitSource* Source, const FString& ExportPath,
	FString& OutDestination, FString& OutError)
{
	OutDestination = ExportPath;
	if (OutDestination.IsEmpty())
	{
		OutDestination = Source->ExportedUnit
			? Source->ExportedUnit->GetOutermost()->GetName()
			: GetDefault<UMassBattleUnitSourceSettings>()->ExportRoot / Source->GetSourceAsset()->GetName();
	}

	FText PathError;
	if (!FPackageName::IsValidLongPackageName(OutDestination, false, &PathError))
	{
		OutError = PathError.ToString();
		return false;
	}
	const FString ObjectPath = OutDestination + TEXT(".") + FPackageName::GetLongPackageAssetName(OutDestination);
	UObject* Occupant = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Occupant && Occupant != Source->ExportedUnit)
	{
		OutError = FString::Printf(TEXT("Export destination is occupied: %s"), *ObjectPath);
		return false;
	}
	return true;
}

static bool WriteUnitData(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets, bool bValidateOnly,
	FString& OutError)
{
	const TSharedPtr<FJsonObject> MergeRoot = ParseObject(UnitDataJson);
	if (!MergeRoot.IsValid())
	{
		OutError = TEXT("UnitDataJson is not valid JSON");
		return false;
	}

	UMassBattleAgentConfigDataAsset* Unit = LoadUnit(UnitPath, OutError);
	if (!Unit)
	{
		return false;
	}

	AMassBattleUnitSource* Source = Unit->GetTypedOuter<AMassBattleUnitSource>();
	FString ExportPath;
	const TSharedPtr<FJsonValue> ExportPathValue = MergeRoot->TryGetField(TEXT("export_path"));
	const bool bChangeExportPath = ExportPathValue.IsValid();
	if (bChangeExportPath && (!Source || ExportPathValue->Type != EJson::String))
	{
		OutError = TEXT("export_path requires a string and an Actor Source target");
		return false;
	}
	if (bChangeExportPath)
	{
		ExportPath = ExportPathValue->AsString();
	}
	for (const FString& Key : { FString(TEXT("options")), FString(TEXT("merge_options")) })
	{
		const TSharedPtr<FJsonValue> Value = MergeRoot->TryGetField(Key);
		if (Value.IsValid() && Value->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("%s must be a JSON object"), *Key);
			return false;
		}
	}
	const TSharedPtr<FJsonObject> Options = ExtractMergeOptions(MergeRoot);
	bool bExportSource = false;
	if (Options->HasField(TEXT("export_source")) && !Options->TryGetBoolField(TEXT("export_source"), bExportSource))
	{
		OutError = TEXT("options.export_source must be a boolean");
		return false;
	}
	if (bExportSource && (!Source || !bSaveAssets))
	{
		OutError = TEXT("options.export_source=true requires an Actor Source target and save_assets=true");
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MergeErrors;
	const TArray<TSharedPtr<FJsonObject>> Patches = BuildUnionMergePatches(Unit, MergeRoot, Options, MergeErrors);
	if (!MergeErrors.IsEmpty())
	{
		const TSharedPtr<FJsonObject> Error = MergeErrors[0]->AsObject();
		OutError = FString::Printf(TEXT("%s: %s"), *Error->GetStringField(TEXT("path")), *Error->GetStringField(TEXT("message")));
		return false;
	}
	if (!bValidateOnly && Patches.IsEmpty() && !bChangeExportPath && !bExportSource)
	{
		OutError = TEXT("UnitDataJson does not contain any writable unit fields");
		return false;
	}

	FStagedUnitFields Fields;
	if (!Fields.Stage(Unit, Patches, OutError))
	{
		return false;
	}
	if (bValidateOnly)
	{
		FString Destination;
		return !bExportSource || ResolveSourceExportDestination(Source,
			bChangeExportPath ? ExportPath : Source->ExportPath, Destination, OutError);
	}

	UObject* Asset = Source ? Source->GetSourceAsset() : Unit;
	UPackage* Package = Asset->GetOutermost();
	const bool bWasDirty = Package->IsDirty();
	const FString PreviousExportPath = Source ? Source->ExportPath : FString();
	Fields.Apply();
	if (Source && bChangeExportPath)
	{
		Source->ExportPath = ExportPath;
	}
	if (bExportSource)
	{
		bool bExportChanged = false;
		const TSharedPtr<FJsonObject> Export = ParseObject(UMassBattleUnitMCPApi::UpdateSource(Source, true, &bExportChanged));
		if (Export.IsValid() && Export->GetBoolField(TEXT("success")))
		{
			return true;
		}
		if (!Export.IsValid() || !Export->TryGetStringField(TEXT("error"), OutError))
		{
			OutError = TEXT("Actor Source export failed");
		}
		if (bExportChanged)
		{
			Asset->MarkPackageDirty();
			return false;
		}
	}
	else
	{
		if (Source)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(CastChecked<UBlueprint>(Asset));
		}
		else
		{
			Asset->MarkPackageDirty();
		}
		if (!bSaveAssets || SaveAsset(Asset, OutError))
		{
			return true;
		}
	}

	Fields.Restore();
	if (Source && bChangeExportPath)
	{
		Source->ExportPath = PreviousExportPath;
	}
	Package->SetDirtyFlag(bWasDirty);
	return false;
}

static TArray<TSharedPtr<FJsonValue>> BuildReferencerJson(UObject* Unit, UObject* InternalReferencer = nullptr)
{
	TArray<TSharedPtr<FJsonValue>> RefJson;
	if (!Unit)
	{
		return RefJson;
	}

	IAssetRegistry& Registry = GetAssetRegistry();
	TArray<FName> Referencers;
	Registry.GetReferencers(Unit->GetOutermost()->GetFName(), Referencers);
	for (const FName& Referencer : Referencers)
	{
		if (Referencer == Unit->GetOutermost()->GetFName()) continue;
		if (InternalReferencer && Referencer == InternalReferencer->GetOutermost()->GetFName()) continue;
		RefJson.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
	}
	return RefJson;
}

static FString NormalizeDeleteMode(const TSharedPtr<FJsonObject>& Options)
{
	FString Mode = TEXT("soft");
	if (Options.IsValid())
	{
		Options->TryGetStringField(TEXT("mode"), Mode);
		Options->TryGetStringField(TEXT("delete_mode"), Mode);
	}
	Mode = Mode.ToLower();
	if (Mode == TEXT("permanent"))
	{
		return TEXT("hard");
	}
	return Mode == TEXT("hard") ? TEXT("hard") : TEXT("soft");
}

static FString EscapeCsv(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Out);
}

static TArray<FString> GetFallbackExportFields()
{
	return {
		TEXT("StyleType.Index"),
		TEXT("SubType.Index"),
		TEXT("Health.Maximum"),
		TEXT("Health.Current"),
		TEXT("Move.MovementModel"),
		TEXT("Move.XY.MoveSpeed"),
		TEXT("Move.XY.MoveAcceleration"),
		TEXT("Move.XY.AcceptanceRadius"),
		TEXT("Attack.bEnable"),
		TEXT("Attack.Range"),
		TEXT("Attack.CoolDown"),
		TEXT("Attack.DurationPerRound"),
		TEXT("Damage.Damage"),
		TEXT("Damage.DmgRadius"),
		TEXT("Defence.NormalDmgImmune"),
		TEXT("Collider.Radius"),
		TEXT("Collider.Height"),
		TEXT("Collider.Mass")
	};
}

static TArray<FString> GetDefaultExportFields(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FString> Fields = ReadStringArrayField(LoadUnitStyleConfig(Options), TEXT("default_export_fields"));
	return Fields.IsEmpty() ? GetFallbackExportFields() : Fields;
}

static TArray<FString> GetExportFields(const TSharedPtr<FJsonObject>& Options)
{
	TArray<FString> Fields;
	const TArray<TSharedPtr<FJsonValue>>* FieldArray = nullptr;
	if (Options.IsValid() && Options->TryGetArrayField(TEXT("fields"), FieldArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *FieldArray)
		{
			const FString Field = Value->AsString();
			if (!Field.IsEmpty())
			{
				Fields.Add(Field);
			}
		}
	}
	return Fields.IsEmpty() ? GetDefaultExportFields(Options) : Fields;
}

static TSharedPtr<FJsonObject> BuildFieldSchemaObject(const FString& Path, FProperty* Property, const EFieldRole Role)
{
	TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
	Field->SetStringField(TEXT("path"), Path);
	Field->SetStringField(TEXT("role"), RoleToString(Role));
	Field->SetStringField(TEXT("cpp_type"), Property ? Property->GetCPPType() : FString());
	Field->SetBoolField(TEXT("editable"), Role != EFieldRole::RuntimeState && Role != EFieldRole::SystemOwned && Role != EFieldRole::Deprecated && Role != EFieldRole::VisibilityControl);
	if (Property)
	{
		const FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty())
		{
			Field->SetStringField(TEXT("tooltip"), Tooltip);
		}
	}
	return Field;
}

static void AddSchemaFieldsRecursive(TArray<TSharedPtr<FJsonValue>>& OutFields, UStruct* Struct, const FString& Prefix, EFieldRole Role, int32 Depth)
{
	if (!Struct || Depth < 0)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		const FString Name = Property->GetName();
		if (ShouldSkipNestedField(Name))
		{
			continue;
		}

		const FString Path = Prefix.IsEmpty() ? Name : Prefix + TEXT(".") + Name;
		OutFields.Add(MakeShared<FJsonValueObject>(BuildFieldSchemaObject(Path, Property, Role)));

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			AddSchemaFieldsRecursive(OutFields, StructProperty->Struct, Path, Role, Depth - 1);
		}
	}
}

static void AddObjectClassFilter(FARFilter& Filter, UClass* Class)
{
	if (Class)
	{
		Filter.ClassPaths.Add(Class->GetClassPathName());
	}
}

} // namespace MassBattleUnitMCP

using namespace MassBattleUnitMCP;

UMassBattleAgentConfigDataAsset* UMassBattleUnitMCPApi::ResolveUnit(const FString& UnitPath, FString& OutError)
{
	return LoadUnit(UnitPath, OutError);
}

bool UMassBattleUnitMCPApi::ResolveCreationPatch(const TSharedPtr<FJsonObject>& AuthoringDefaults,
	const TSharedPtr<FJsonObject>& ExplicitPatch, TSharedPtr<FJsonObject>& OutUnitPatch,
	TArray<TSharedPtr<FJsonValue>>& OutDerivations, FString& OutError)
{
	const TSharedPtr<FJsonObject> Defaults = FindJsonObject(AuthoringDefaults, TEXT("default_unit_patch"));
	if (AuthoringDefaults.IsValid() && AuthoringDefaults->HasField(TEXT("default_unit_patch")) && !Defaults.IsValid())
	{
		OutError = TEXT("style authoring_defaults.default_unit_patch must be a JSON object");
		return false;
	}

	OutUnitPatch = MakeShared<FJsonObject>();
	if (Defaults.IsValid())
	{
		FJsonObject::Duplicate(Defaults, OutUnitPatch);
	}
	TSharedPtr<FJsonObject> MergedData = ExtractMergeDataObject(OutUnitPatch, OutError);
	if (!MergedData.IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> NormalizedExplicit = MakeShared<FJsonObject>();
	if (ExplicitPatch.IsValid())
	{
		TSharedPtr<FJsonObject> ExplicitCopy = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(ExplicitPatch, ExplicitCopy);
		const TSharedPtr<FJsonObject> ExplicitData = ExtractMergeDataObject(ExplicitCopy, OutError);
		if (!ExplicitData.IsValid())
		{
			return false;
		}
		TSharedPtr<FJsonObject> ExplicitDerivationInput = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(ExplicitData, ExplicitDerivationInput);
		NormalizedExplicit->SetObjectField(TEXT("Data"), ExplicitDerivationInput);
		MergeCreationJson(MergedData, ExplicitData);

		if (ExplicitCopy->HasField(TEXT("options")) || ExplicitCopy->HasField(TEXT("merge_options")))
		{
			OutUnitPatch->RemoveField(TEXT("merge_options"));
			OutUnitPatch->SetObjectField(TEXT("options"), ExtractMergeOptions(ExplicitCopy));
		}
		if (ExplicitCopy->HasField(TEXT("export_path")))
		{
			OutUnitPatch->SetField(TEXT("export_path"), ExplicitCopy->TryGetField(TEXT("export_path")));
		}
	}

	// Normalize flat input before applying nested style derivations or reflected fields.
	const TSharedPtr<FJsonObject> MergeOptions = ExtractMergeOptions(OutUnitPatch);
	const TSharedPtr<FJsonValue> ExportPath = OutUnitPatch->TryGetField(TEXT("export_path"));
	OutUnitPatch = MakeShared<FJsonObject>();
	OutUnitPatch->SetObjectField(TEXT("Data"), MergedData);
	OutUnitPatch->SetObjectField(TEXT("options"), MergeOptions);
	if (ExportPath.IsValid())
	{
		OutUnitPatch->SetField(TEXT("export_path"), ExportPath);
	}
	return ApplyConfiguredNumberDerivations(AuthoringDefaults, OutUnitPatch, NormalizedExplicit, OutDerivations, OutError);
}

FString UMassBattleUnitMCPApi::DestroySourceUnit(AMassBattleUnitSource* Source)
{
	if (!Source || !Source->GetSourceAsset() || !Source->ExportedUnit)
		return MakeErrorJson(TEXT("Source has no exported unit."));
	Source = AMassBattleUnitSource::Resolve(Source->GetSourceAsset());
	UMassBattleAgentConfigDataAsset* Exported = Source->ExportedUnit;
	if (!BuildReferencerJson(Exported, Source->GetSourceAsset()).IsEmpty())
		return MakeErrorJson(TEXT("Deletion blocked: exported unit has external referencers."));
	const FString UnitPath = Exported->GetPathName();
	Source->Modify();
	Source->ExportedUnit = nullptr;
	FString Error;
	if (!SaveAsset(Source->GetSourceAsset(), Error))
	{
		Source->ExportedUnit = Exported;
		return MakeErrorJson(Error);
	}
	GetAssetRegistry().ScanModifiedAssetFiles({ FPackageName::LongPackageNameToFilename(
		Source->GetSourceAsset()->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()) });
	const FString Result = MCP_UnitDelete(UnitPath, TEXT("{\"mode\":\"hard\",\"dry_run\":false}"));
	const TSharedPtr<FJsonObject> Response = ParseObject(Result);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
		Source->ExportedUnit = Exported;
	else Source->bHasUpdated = false;
	Source->LastReport = Result;
	Source->MarkPackageDirty();
	if (!SaveAsset(Source->GetSourceAsset(), Error)) return MakeErrorJson(Error);
	return Result;
}

FString UMassBattleUnitMCPApi::UpdateSource(AMassBattleUnitSource* Source, bool bSaveAssets, bool* OutExportChanged)
{
	if (OutExportChanged)
	{
		*OutExportChanged = false;
	}
	if (!Source || !Source->UnitData || !Source->GetSourceAsset())
		return MakeErrorJson(TEXT("Update requires a saved source Actor Blueprint with UnitData."));
	if (!Source->HasAnyFlags(RF_ClassDefaultObject))
	{
		AMassBattleUnitSource* Defaults = AMassBattleUnitSource::Resolve(Source->GetSourceAsset());
		UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyOptions;
		CopyOptions.bDoDelta = false;
		UEngine::CopyPropertiesForUnrelatedObjects(Source->UnitData, Defaults->UnitData, CopyOptions);
		Defaults->ExportPath = Source->ExportPath;
		const FString Result = UpdateSource(Defaults, bSaveAssets, OutExportChanged);
		Source->ExportedUnit = Defaults->ExportedUnit;
		return Result;
	}
	FString Error;
	FString Destination;
	if (!ResolveSourceExportDestination(Source, Source->ExportPath, Destination, Error))
	{
		return MakeErrorJson(Error);
	}
	const FString Name = FPackageName::GetLongPackageAssetName(Destination);

	TArray<TSharedPtr<FJsonObject>> Patches;
	TArray<TSharedPtr<FJsonValue>> Errors;
	TSharedPtr<FJsonObject> ReadOptions = ParseObject(TEXT("{\"detail\":\"full\",\"include_defaults\":true}"));
	if (Source->bHasUpdated && Source->ExportedUnit)
	{
		TSharedPtr<FJsonObject> Options = ParseObject(TEXT("{\"replace_arrays\":true}"));
		Patches = BuildUnionMergePatches(Source->LastUpdatedSource,
			BuildSourceAlignedUnitJson(Source->UnitData, ReadOptions), Options, Errors, Source->UnitData);
	}
	if (!Errors.IsEmpty())
	{
		const TSharedPtr<FJsonObject> Error = Errors[0]->AsObject();
		return MakeErrorJson(FString::Printf(TEXT("%s: %s"), *Error->GetStringField(TEXT("path")), *Error->GetStringField(TEXT("message"))));
	}

	FStagedUnitFields Fields;
	TArray<TSharedPtr<FJsonValue>> Diff;
	if (!Fields.Stage(Source->ExportedUnit, Patches, Error, &Diff))
	{
		return MakeErrorJson(Error);
	}
	bool bCreated = false;
	bool bMoved = false;
	if (!Source->ExportedUnit)
	{
		Source->Modify();
		Source->ExportedUnit = DuplicateObject<UMassBattleAgentConfigDataAsset>(Source->UnitData, CreatePackage(*Destination), *Name);
		Source->ExportedUnit->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Source->ExportedUnit);
		bCreated = true;
	}
	else if (Source->ExportedUnit->GetOutermost()->GetName() != Destination)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (!AssetTools.RenameAssets({ FAssetRenameData(Source->ExportedUnit,
			FPackageName::GetLongPackagePath(Destination), Name) }))
			return MakeErrorJson(TEXT("Export move failed; source baseline was not advanced."));
		bMoved = true;
	}
	Fields.Apply();
	if (bSaveAssets && (bCreated || bMoved || !Patches.IsEmpty()) && !SaveAsset(Source->ExportedUnit, Error))
	{
		Fields.Restore();
		if (bCreated)
		{
			UMassBattleAgentConfigDataAsset* UnsavedExport = Source->ExportedUnit;
			Source->ExportedUnit = nullptr;
			FAssetRegistryModule::AssetDeleted(UnsavedExport);
			UnsavedExport->ClearFlags(RF_Public | RF_Standalone);
			UPackage* TransientPackage = GetTransientPackage();
			const FName TransientName = MakeUniqueObjectName(TransientPackage, UnsavedExport->GetClass());
			UnsavedExport->Rename(*TransientName.ToString(), TransientPackage, REN_DontCreateRedirectors | REN_NonTransactional);
		}
		if (bMoved)
		{
			if (OutExportChanged)
			{
				*OutExportChanged = true;
			}
			Error = FString::Printf(TEXT("The linked unit moved to %s before saving failed. Retry the source update. %s"), *Destination, *Error);
		}
		return MakeErrorJson(Error);
	}
	const bool bExportChanged = bCreated || bMoved || !Patches.IsEmpty();
	if (OutExportChanged)
	{
		*OutExportChanged = bExportChanged;
	}
	Source->Modify();
	UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyOptions;
	CopyOptions.bDoDelta = false;
	UMassBattleAgentConfigDataAsset* PreviousBaseline = bSaveAssets && !bExportChanged
		? DuplicateObject<UMassBattleAgentConfigDataAsset>(Source->LastUpdatedSource, GetTransientPackage())
		: nullptr;
	const bool bWasUpdated = Source->bHasUpdated;
	const FString PreviousReport = Source->LastReport;
	UEngine::CopyPropertiesForUnrelatedObjects(Source->UnitData, Source->LastUpdatedSource, CopyOptions);
	Source->bHasUpdated = true;
	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetStringField(TEXT("asset_path"), Source->ExportedUnit->GetPathName());
	Result->SetBoolField(TEXT("created"), bCreated);
	Result->SetBoolField(TEXT("moved"), bMoved);
	Result->SetArrayField(TEXT("applied_diff"), Diff);
	Source->LastReport = ToJsonString(Result);
	FBlueprintEditorUtils::MarkBlueprintAsModified(CastChecked<UBlueprint>(Source->GetSourceAsset()));
	if (bSaveAssets && !SaveAsset(Source->GetSourceAsset(), Error))
	{
		Source->LastReport = PreviousReport;
		if (!bExportChanged)
		{
			UEngine::CopyPropertiesForUnrelatedObjects(PreviousBaseline, Source->LastUpdatedSource, CopyOptions);
			Source->bHasUpdated = bWasUpdated;
		}
		else
		{
			// Keep the baseline aligned with the persisted output so a retry can apply subsequent source edits.
			Source->MarkPackageDirty();
			Error = FString::Printf(TEXT("The linked unit was saved at %s before the Actor Source could be saved. Retry the source update. %s"), *Destination, *Error);
		}
		return MakeErrorJson(Error);
	}
	return Source->LastReport;
}

FString UMassBattleUnitMCPApi::MCP_UnitList(const FString& OptionsJson)
{
	TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	FString Query;
	Options->TryGetStringField(TEXT("query"), Query);
	Query = Query.ToLower();

	int32 Limit = MaxDefaultUnitList;
	TryGetIntField(Options, TEXT("limit"), Limit);
	Limit = FMath::Clamp(Limit, 1, 2000);

	bool bLoadFields = true;
	Options->TryGetBoolField(TEXT("load_fields"), bLoadFields);

	TArray<TSharedPtr<FJsonValue>> Units;
	const TArray<FAssetData> Assets = ScanUnitAssets(Options);
	for (const FAssetData& Asset : Assets)
	{
		const FString ObjectPath = Asset.GetObjectPathString();
		if (!Query.IsEmpty() && !ObjectPath.ToLower().Contains(Query))
		{
			continue;
		}
		if (Units.Num() >= Limit)
		{
			break;
		}
		Units.Add(MakeShared<FJsonValueObject>(BuildUnitSummary(Asset, bLoadFields, Options)));
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetNumberField(TEXT("count"), Units.Num());
	Root->SetNumberField(TEXT("total_scanned"), Assets.Num());
	AddRootsToJson(Root, Options);
	Root->SetObjectField(TEXT("default_ignore_policy"), BuildIgnorePolicyJson());
	Root->SetStringField(TEXT("default_detail"), TEXT("simple"));
	Root->SetArrayField(TEXT("units"), Units);
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitGet(const FString& UnitPath, const FString& OptionsJson)
{
	TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	FString Error;
	UMassBattleAgentConfigDataAsset* Unit = LoadUnit(UnitPath, Error);
	if (!Unit)
	{
		return MakeErrorJson(Error);
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	AMassBattleUnitSource* Source = Unit->GetTypedOuter<AMassBattleUnitSource>();
	UObject* TargetAsset = Source ? Source->GetSourceAsset() : Unit;
	Root->SetStringField(TEXT("AssetName"), TargetAsset->GetName());
	Root->SetStringField(TEXT("ObjectPath"), TargetAsset->GetPathName());
	Root->SetStringField(TEXT("Class"), TargetAsset->GetClass()->GetPathName());
	if (Source)
	{
		Root->SetStringField(TEXT("export_path"), Source->ExportPath);
		Root->SetStringField(TEXT("exported_unit"), GetObjectPathString(Source->ExportedUnit));
		Root->SetStringField(TEXT("last_report"), Source->LastReport);
	}
	Root->SetStringField(TEXT("detail"), GetDetailLabel(Options));
	Root->SetObjectField(TEXT("default_ignore_policy"), BuildIgnorePolicyJson());
	Root->SetObjectField(TEXT("Data"), BuildSourceAlignedUnitJson(Unit, Options));
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitGetSchema(const FString& OptionsJson)
{
	TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	bool bRecursive = true;
	Options->TryGetBoolField(TEXT("recursive"), bRecursive);
	int32 Depth = bRecursive ? 3 : 0;
	TryGetIntField(Options, TEXT("depth"), Depth);
	Depth = FMath::Clamp(Depth, 0, 8);

	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> It(UMassBattleAgentConfigDataAsset::StaticClass()); It; ++It)
	{
		FProperty* Property = *It;
		const FString Name = Property->GetName();
		if (ShouldSkipTopLevelField(Name, Options))
		{
			continue;
		}

		const EFieldRole Role = ClassifyTopLevelField(Name);
		Fields.Add(MakeShared<FJsonValueObject>(BuildFieldSchemaObject(Name, Property, Role)));
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			AddSchemaFieldsRecursive(Fields, StructProperty->Struct, Name, Role, Depth - 1);
		}
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetStringField(TEXT("asset_class"), UMassBattleAgentConfigDataAsset::StaticClass()->GetPathName());
	Root->SetObjectField(TEXT("default_ignore_policy"), BuildIgnorePolicyJson());
	Root->SetArrayField(TEXT("fields"), Fields);
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitExport(const FString& OptionsJson)
{
	TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	FString Format = TEXT("csv");
	Options->TryGetStringField(TEXT("format"), Format);
	Format = Format.ToLower();

	TArray<FString> Fields = GetExportFields(Options);
	TArray<FAssetData> Assets = ScanUnitAssets(Options);

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> CsvLines;
	TArray<FString> Header = { TEXT("AssetName"), TEXT("ObjectPath"), TEXT("PackagePath") };
	Header.Append(Fields);
	TArray<FString> EscapedHeader;
	for (const FString& Column : Header)
	{
		EscapedHeader.Add(EscapeCsv(Column));
	}
	CsvLines.Add(FString::Join(EscapedHeader, TEXT(",")));

	for (const FAssetData& Asset : Assets)
	{
		FString LoadError;
		UObject* Loaded = LoadUnit(Asset.GetObjectPathString(), LoadError);
		TSharedPtr<FJsonObject> Row = BuildUnitSummary(Asset, false, Options);
		TArray<FString> CsvCells = {
			EscapeCsv(Asset.AssetName.ToString()),
			EscapeCsv(Asset.GetObjectPathString()),
			EscapeCsv(Asset.PackagePath.ToString())
		};

		for (const FString& Field : Fields)
		{
			FString Value;
			if (Loaded && TryExportPath(Loaded, Field, Value))
			{
				Row->SetStringField(Field, Value);
			}
			CsvCells.Add(EscapeCsv(Value));
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		CsvLines.Add(FString::Join(CsvCells, TEXT(",")));
	}

	IFileManager::Get().MakeDirectory(*GetExportDir(), true);
	FString OutputPath;
	if (!Options->TryGetStringField(TEXT("output_path"), OutputPath) || OutputPath.IsEmpty())
	{
		OutputPath = FPaths::Combine(GetExportDir(), FString::Printf(TEXT("massbattle_units_%s.%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")), Format == TEXT("json") ? TEXT("json") : TEXT("csv")));
	}

	bool bSaved = false;
	if (Format == TEXT("json"))
	{
		TSharedPtr<FJsonObject> ExportRoot = MakeShared<FJsonObject>();
		ExportRoot->SetArrayField(TEXT("fields"), [&Fields]()
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Field : Fields)
			{
				Values.Add(MakeShared<FJsonValueString>(Field));
			}
			return Values;
		}());
		ExportRoot->SetArrayField(TEXT("rows"), Rows);
		bSaved = FFileHelper::SaveStringToFile(ToJsonString(ExportRoot), *OutputPath);
	}
	else
	{
		bSaved = FFileHelper::SaveStringToFile(FString::Join(CsvLines, LINE_TERMINATOR), *OutputPath);
	}

	if (!bSaved)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to save export: %s"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetStringField(TEXT("format"), Format);
	Root->SetStringField(TEXT("output_path"), OutputPath);
	Root->SetNumberField(TEXT("unit_count"), Rows.Num());
	Root->SetArrayField(TEXT("fields"), [&Fields]()
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Field : Fields)
		{
			Values.Add(MakeShared<FJsonValueString>(Field));
		}
		return Values;
	}());
	return ToJsonString(Root);
}

TSharedPtr<FJsonObject> UMassBattleUnitMCPApi::ReadUnitWriteData(const TSharedPtr<FJsonObject>& Root, FString& OutError)
{
	return ExtractMergeDataObject(Root, OutError);
}

bool UMassBattleUnitMCPApi::ValidateUnitWrite(const FString& UnitPath, const FString& UnitDataJson, FString& OutError)
{
	return WriteUnitData(UnitPath, UnitDataJson, true, true, OutError);
}

FString UMassBattleUnitMCPApi::MCP_UnitWrite(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets)
{
	FString Error;
	return WriteUnitData(UnitPath, UnitDataJson, bSaveAssets, false, Error) ? ToJsonString(MakeSuccess()) : MakeErrorJson(Error);
}

FString UMassBattleUnitMCPApi::MCP_UnitCreate(const FString& OptionsJson, bool bSaveAssets)
{
	const TSharedPtr<FJsonObject> Input = ParseObject(OptionsJson);
	if (!Input.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	FString AssetType;
	Input->TryGetStringField(TEXT("asset_type"), AssetType);
	const bool bSourceActor = AssetType == TEXT("source_actor");
	FString AssetName, PackagePath, UnitPath, Error;
	if (!ResolveCreateTarget(Input, AssetName, PackagePath, UnitPath, Error))
	{
		return MakeErrorJson(Error);
	}
	const FString Destination = PackagePath / AssetName;
	if (!FPackageName::IsValidLongPackageName(Destination))
	{
		return MakeErrorJson(TEXT("Invalid unit asset path."));
	}
	if (LoadObject<UObject>(nullptr, *UnitPath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Target unit already exists: %s"), *UnitPath));
	}

	const TSharedPtr<FJsonObject> Style = LoadUnitStyleConfig(Input);
	if (!Style.IsValid())
	{
		return MakeErrorJson(TEXT("The selected unit style could not be loaded."));
	}
	const TSharedPtr<FJsonObject> AuthoringDefaults = FindJsonObject(Style, TEXT("authoring_defaults"));
	if (Style->HasField(TEXT("authoring_defaults")) && !AuthoringDefaults.IsValid())
	{
		return MakeErrorJson(TEXT("style authoring_defaults must be a JSON object."));
	}
	const FString TemplatePath = ResolveCreationTemplate(Input, AuthoringDefaults, bSourceActor);
	UMassBattleAgentConfigDataAsset* Template = nullptr;
	if (!TemplatePath.IsEmpty())
	{
		Template = LoadUnit(TemplatePath, Error);
		if (!Template)
		{
			return MakeErrorJson(Error);
		}
	}

	TSharedPtr<FJsonObject> AuthoredPatch;
	for (const TCHAR* Name : { TEXT("unit_data"), TEXT("initial_unit_data") })
	{
		if (Input->HasField(Name))
		{
			AuthoredPatch = FindJsonObject(Input, Name);
			if (!AuthoredPatch.IsValid())
			{
				return MakeErrorJson(FString::Printf(TEXT("%s must be a JSON object."), Name));
			}
			break;
		}
	}

	TSharedPtr<FJsonObject> CreationPatch;
	TArray<TSharedPtr<FJsonValue>> Derivations;
	if (!ResolveCreationPatch(AuthoringDefaults, AuthoredPatch, CreationPatch, Derivations, Error))
	{
		return MakeErrorJson(Error);
	}

	/// 在临时配置上验证并应用创建缺省，避免先注册半成品资产。
	UMassBattleAgentConfigDataAsset* Unit = nullptr;
	UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyOptions;
	CopyOptions.bDoDelta = false;
	if (Template && !bSourceActor)
	{
		const FName DraftName = MakeUniqueObjectName(GetTransientPackage(), Template->GetClass(), FName(TEXT("MassBattleUnitDraft")));
		Unit = DuplicateObject<UMassBattleAgentConfigDataAsset>(Template, GetTransientPackage(), DraftName);
	}
	else
	{
		Unit = NewObject<UMassBattleAgentConfigDataAsset>(GetTransientPackage(), NAME_None, RF_Transient);
		if (Unit && Template)
		{
			UEngine::CopyPropertiesForUnrelatedObjects(Template, Unit, CopyOptions);
		}
	}
	if (!Unit)
	{
		return MakeErrorJson(TEXT("Could not prepare unit data for creation."));
	}
	Unit->SetFlags(RF_Transient);

	TArray<TSharedPtr<FJsonValue>> AppliedDiffs;
	const auto ApplyCreationData = [&](const TSharedPtr<FJsonObject>& Patch)
	{
		TArray<TSharedPtr<FJsonValue>> MergeErrors;
		const TArray<TSharedPtr<FJsonObject>> Patches = BuildUnionMergePatches(Unit, Patch, ExtractMergeOptions(Patch), MergeErrors);
		if (!MergeErrors.IsEmpty())
		{
			const TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
			Failure->SetBoolField(TEXT("success"), false);
			Failure->SetStringField(TEXT("error"), TEXT("Create-time unit data contains non-mergeable fields"));
			Failure->SetArrayField(TEXT("merge_errors"), MergeErrors);
			return ToJsonString(Failure);
		}
		for (const TSharedPtr<FJsonObject>& Change : Patches)
		{
			FPatchPreview Preview;
			FString Error;
			if (!ApplyPatch(Unit, Change, Error, &Preview))
			{
				return MakeErrorJson(Error);
			}
			AppliedDiffs.Add(MakeShared<FJsonValueObject>(PatchPreviewToJson(Preview)));
		}
		return FString();
	};
	const TSharedPtr<FJsonObject> CreationOptions = ExtractMergeOptions(CreationPatch);
	if (!CreationOptions->HasField(TEXT("replace_arrays")) && !CreationOptions->HasField(TEXT("replace_array_values"))
		&& !CreationOptions->HasField(TEXT("append_arrays")) && !CreationOptions->HasField(TEXT("allow_array_append")))
	{
		CreationOptions->SetBoolField(TEXT("replace_arrays"), true);
	}
	const FString DataFailure = ApplyCreationData(CreationPatch);
	if (!DataFailure.IsEmpty())
	{
		return DataFailure;
	}

	const TSharedPtr<FJsonObject> CreationData = FindJsonObject(CreationPatch, TEXT("Data"));
	const TSharedPtr<FJsonObject> InitialTrace = FindJsonObject(CreationData, TEXT("Trace"));
	const TSharedPtr<FJsonObject> InitialAttack = FindJsonObject(CreationData, TEXT("Attack"));
	const TSharedPtr<FJsonObject> InitialChase = FindJsonObject(CreationData, TEXT("Chase"));
	const TSharedPtr<FJsonObject> InitialSector = FindJsonObject(InitialTrace, TEXT("SectorTrace"));

	const TSharedPtr<FJsonObject> EngagementDefaults = FindJsonObject(AuthoringDefaults, TEXT("engagement_defaults"));
	if (AuthoringDefaults.IsValid() && AuthoringDefaults->HasField(TEXT("engagement_defaults")) && !EngagementDefaults.IsValid())
	{
		return MakeErrorJson(TEXT("style authoring_defaults.engagement_defaults must be a JSON object."));
	}
	if (EngagementDefaults.IsValid())
	{
		double ChaseAcceptanceRangeRatio = 0.0;
		double HitToleranceRangeRatio = 0.0;
		double TracePerceptionRangeRatio = 0.0;
		double UnarmedMovingTraceRadius = 0.0;
		const auto ReadNonnegativeNumber = [&](const TCHAR* Name, double& OutValue)
		{
			return EngagementDefaults->TryGetNumberField(Name, OutValue) && FMath::IsFinite(OutValue) && OutValue >= 0.0;
		};
		if (!ReadNonnegativeNumber(TEXT("chase_acceptance_range_ratio"), ChaseAcceptanceRangeRatio)
			|| !ReadNonnegativeNumber(TEXT("hit_tolerance_range_ratio"), HitToleranceRangeRatio)
			|| !ReadNonnegativeNumber(TEXT("trace_perception_range_ratio"), TracePerceptionRangeRatio)
			|| !ReadNonnegativeNumber(TEXT("unarmed_moving_trace_radius"), UnarmedMovingTraceRadius))
		{
			return MakeErrorJson(TEXT("style engagement defaults require finite nonnegative ratios and moving trace radius."));
		}
		const bool bMoving = Unit->Move.bEnable;
		const bool bArmed = Unit->Attack.bEnable && Unit->Attack.Range > 0.0f;
		const bool bArmedStationary = !bMoving && bArmed;
		const double DefaultTraceRadius = bArmed
			? Unit->Attack.Range * TracePerceptionRangeRatio
			: (bMoving ? UnarmedMovingTraceRadius : 0.0);
		bool bHasInitialTraceRadius = false;
		TSharedPtr<FJsonObject> SectorPatch = MakeShared<FJsonObject>();
		for (const TCHAR* StateName : { TEXT("Common"), TEXT("Sleep"), TEXT("Patrol"), TEXT("Chase"), TEXT("Reinforce") })
		{
			const TSharedPtr<FJsonObject>* InitialStateField = nullptr;
			const TSharedPtr<FJsonObject> InitialState = InitialSector.IsValid()
				&& InitialSector->TryGetObjectField(StateName, InitialStateField)
				&& InitialStateField && InitialStateField->IsValid()
				? *InitialStateField
				: nullptr;
			const bool bInitialRadius = InitialState.IsValid() && InitialState->HasField(TEXT("TraceRadius"));
			bHasInitialTraceRadius |= bInitialRadius;
			if (!bInitialRadius)
			{
				TSharedPtr<FJsonObject> StatePatch = MakeShared<FJsonObject>();
				StatePatch->SetNumberField(TEXT("TraceRadius"), DefaultTraceRadius);
				SectorPatch->SetObjectField(StateName, StatePatch);
			}
		}

		TSharedPtr<FJsonObject> TracePatch = MakeShared<FJsonObject>();
		TracePatch->SetObjectField(TEXT("SectorTrace"), SectorPatch);
		if (!bMoving && !HasJsonField(InitialTrace, TEXT("bEnable")) && !bHasInitialTraceRadius)
		{
			TracePatch->SetBoolField(TEXT("bEnable"), bArmedStationary);
		}
		TSharedPtr<FJsonObject> EngagementDataPatch = MakeShared<FJsonObject>();
		EngagementDataPatch->SetObjectField(TEXT("Trace"), TracePatch);
		if (bArmed && !HasJsonField(InitialAttack, TEXT("RangeToleranceHit")))
		{
			TSharedPtr<FJsonObject> AttackPatch = MakeShared<FJsonObject>();
			AttackPatch->SetNumberField(TEXT("RangeToleranceHit"), Unit->Attack.Range * HitToleranceRangeRatio);
			EngagementDataPatch->SetObjectField(TEXT("Attack"), AttackPatch);
		}
		if (bArmed && !HasJsonField(InitialChase, TEXT("AcceptanceRadius")))
		{
			TSharedPtr<FJsonObject> ChasePatch = MakeShared<FJsonObject>();
			ChasePatch->SetNumberField(TEXT("AcceptanceRadius"), Unit->Attack.Range * ChaseAcceptanceRangeRatio);
			EngagementDataPatch->SetObjectField(TEXT("Chase"), ChasePatch);
		}
		TSharedPtr<FJsonObject> EngagementPolicyPatch = MakeShared<FJsonObject>();
		EngagementPolicyPatch->SetObjectField(TEXT("Data"), EngagementDataPatch);

		const FString EngagementFailure = ApplyCreationData(EngagementPolicyPatch);
		if (!EngagementFailure.IsEmpty())
		{
			return EngagementFailure;
		}
	}

	/// 表现缺省只在创建时应用到尚未提供的字段。
	const auto JsonObject = [](const TSharedPtr<FJsonObject>& Parent, const FString& Key)
	{
		const TSharedPtr<FJsonObject> Child = FindJsonObject(Parent, Key);
		return Child.IsValid() ? Child : MakeShared<FJsonObject>();
	};
	const TSharedPtr<FJsonObject> FxPolicy = JsonObject(Style, TEXT("combat_fx_defaults"));
	TSharedPtr<FJsonObject> FxDefaultReport = MakeShared<FJsonObject>();
	if (!FxPolicy->Values.IsEmpty())
	{
		const TSharedPtr<FJsonObject> FxInput = JsonObject(Input, TEXT("combat_fx"));
		const TSharedPtr<FJsonObject> ContextAttack = JsonObject(CreationData, TEXT("Attack"));
		const TSharedPtr<FJsonObject> ContextHit = JsonObject(CreationData, TEXT("Hit"));
		FString Family;
		Input->TryGetStringField(TEXT("family"), Family);
		const FString Label = PackagePath + TEXT("/") + AssetName + TEXT("/") + Family;
		const auto Classify = [&](const TCHAR* RulesKey, const TCHAR* DefaultKey, const TCHAR* OverrideKey)
		{
			FString Value;
			if (FxInput->TryGetStringField(OverrideKey, Value)) return Value;
			FxPolicy->TryGetStringField(DefaultKey, Value);
			const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
			if (FxPolicy->TryGetArrayField(RulesKey, Rules))
			{
				for (const TSharedPtr<FJsonValue>& RuleValue : *Rules)
				{
					const TSharedPtr<FJsonObject> Rule = RuleValue->AsObject();
					for (const FString& Token : ReadStringArrayField(Rule, TEXT("contains_any")))
					{
						if (Label.Contains(Token)) return Rule->GetStringField(TEXT("value"));
					}
				}
			}
			return Value;
		};
		FString Weapon = Classify(TEXT("weapon_rules"), TEXT("default_weapon"), TEXT("weapon_category"));
		const FString Surface = Classify(TEXT("surface_rules"), TEXT("default_surface"), TEXT("impact_surface"));
		bool bAttackEnabled = Unit->Attack.bEnable;
		ContextAttack->TryGetBoolField(TEXT("bEnable"), bAttackEnabled);
		if (!bAttackEnabled) Weapon = TEXT("unarmed");
		const TSharedPtr<FJsonObject> WeaponProfiles = JsonObject(FxPolicy, TEXT("weapon_profiles"));
		const TSharedPtr<FJsonObject> HitProfiles = JsonObject(FxPolicy, TEXT("hit_profiles"));
		if (!WeaponProfiles->HasField(Weapon) || !HitProfiles->HasField(Surface))
		{
			return MakeErrorJson(TEXT("combat_fx requires a known weapon_category and impact_surface from the style profile"));
		}
		const bool bProjectileAttack = !Unit->Attack.SpawnProjectile.IsEmpty();
		TSharedPtr<FJsonObject> FxData = MakeShared<FJsonObject>();
		if (!HasJsonField(ContextAttack, TEXT("SpawnFx")))
		{
			const TArray<TSharedPtr<FJsonValue>> Templates = bProjectileAttack ? TArray<TSharedPtr<FJsonValue>>()
				: JsonObject(WeaponProfiles, *Weapon)->GetArrayField(TEXT("attack_fx"));
			const TSharedPtr<FJsonObject> Offset = JsonObject(FxInput, TEXT("muzzle_offset"));
			TArray<const FFxConfig_Attack*> MuzzleAnchors;
			if (Offset->Values.IsEmpty())
			{
				for (const FFxConfig_Attack& Existing : Unit->Attack.SpawnFx)
				{
					if (!Existing.bEnable || Existing.SpawnOrigin != ESpawnOrigin::AtSelf) continue;
					const bool bDuplicateAnchor = MuzzleAnchors.ContainsByPredicate([&Existing](const FFxConfig_Attack* Anchor)
					{
						return Anchor->Transform.GetTranslation() == Existing.Transform.GetTranslation()
							&& Anchor->Transform.GetRotation() == Existing.Transform.GetRotation()
							&& Anchor->Delay == Existing.Delay
							&& Anchor->BindToAnimIndex == Existing.BindToAnimIndex;
					});
					if (!bDuplicateAnchor) MuzzleAnchors.Add(&Existing);
				}
			}
			TArray<TSharedPtr<FJsonValue>> Entries;
			// Each authored hand/animation/time receives one copy of the selected style.
			// Flash, tracer and smoke at the same anchor must not multiply that style.
			for (int32 AnchorIndex = 0; AnchorIndex < FMath::Max(1, MuzzleAnchors.Num()); ++AnchorIndex)
			{
				for (const TSharedPtr<FJsonValue>& Template : Templates)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					FJsonObject::Duplicate(Template->AsObject(), Entry);
					const TSharedPtr<FJsonObject> Transform = JsonObject(Entry, TEXT("Transform"));
					if (MuzzleAnchors.IsValidIndex(AnchorIndex))
					{
						const FFxConfig_Attack& Anchor = *MuzzleAnchors[AnchorIndex];
						const FVector3f Point = Anchor.Transform.GetTranslation();
						const FQuat4f Rotation = Anchor.Transform.GetRotation();
						TSharedPtr<FJsonObject> TranslationJson = MakeShared<FJsonObject>();
						TranslationJson->SetNumberField(TEXT("X"), Point.X);
						TranslationJson->SetNumberField(TEXT("Y"), Point.Y);
						TranslationJson->SetNumberField(TEXT("Z"), Point.Z);
						TSharedPtr<FJsonObject> RotationJson = MakeShared<FJsonObject>();
						RotationJson->SetNumberField(TEXT("X"), Rotation.X);
						RotationJson->SetNumberField(TEXT("Y"), Rotation.Y);
						RotationJson->SetNumberField(TEXT("Z"), Rotation.Z);
						RotationJson->SetNumberField(TEXT("W"), Rotation.W);
						Transform->SetObjectField(TEXT("Translation"), TranslationJson);
						Transform->SetObjectField(TEXT("Rotation"), RotationJson);
						Entry->SetNumberField(TEXT("Delay"), Anchor.Delay);
						Entry->SetNumberField(TEXT("BindToAnimIndex"), Anchor.BindToAnimIndex);
					}
					else if (!Offset->Values.IsEmpty())
					{
						Transform->SetObjectField(TEXT("Translation"), Offset);
					}
					Entry->SetObjectField(TEXT("Transform"), Transform);
					Entries.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
			TSharedPtr<FJsonObject> FxAttack = MakeShared<FJsonObject>();
			FxAttack->SetArrayField(TEXT("SpawnFx"), Entries);
			FxData->SetObjectField(TEXT("Attack"), FxAttack);
		}
		if (!HasJsonField(ContextHit, TEXT("SpawnFx")))
		{
			TSharedPtr<FJsonObject> FxHit = MakeShared<FJsonObject>();
			FxHit->SetArrayField(TEXT("SpawnFx"), HitProfiles->GetArrayField(Surface));
			FxData->SetObjectField(TEXT("Hit"), FxHit);
		}
		if (bProjectileAttack && !HasJsonField(ContextAttack, TEXT("SpawnProjectile")))
		{
			const TSharedPtr<FJsonObject> Variants = JsonObject(FxPolicy, TEXT("projectile_visual_variants"));
			FStructProperty* AttackProperty = FindFProperty<FStructProperty>(Unit->GetClass(), TEXT("Attack"));
			const TSharedPtr<FJsonObject> AttackJson = PropertyToJsonValue(AttackProperty, &Unit->Attack, 8)->AsObject();
			TArray<TSharedPtr<FJsonValue>> Projectiles = AttackJson->GetArrayField(TEXT("SpawnProjectile"));
			bool bRemapped = false;
			for (const TSharedPtr<FJsonValue>& Value : Projectiles)
			{
				const TSharedPtr<FJsonObject> Projectile = Value->AsObject();
				FString SourcePath, VariantPath;
				if (Projectile->TryGetStringField(TEXT("ProjectileConfigDataAsset"), SourcePath)
					&& Variants->TryGetStringField(SourcePath, VariantPath))
				{
					Projectile->SetStringField(TEXT("ProjectileConfigDataAsset"), VariantPath);
					bRemapped = true;
				}
			}
			if (bRemapped)
			{
				const TSharedPtr<FJsonObject> FxAttack = JsonObject(FxData, TEXT("Attack"));
				FxAttack->SetArrayField(TEXT("SpawnProjectile"), Projectiles);
				FxData->SetObjectField(TEXT("Attack"), FxAttack);
			}
		}
		/// 仅在 Style 声明目标属性时迁移继承的特效引用。
		if (!HasJsonField(JsonObject(CreationData, TEXT("ExtraData")), TEXT("MutableSharedFragments")))
		{
			const TSharedPtr<FJsonObject> Variants = JsonObject(FxPolicy, TEXT("projectile_visual_variants"));
			for (const TSharedPtr<FJsonValue>& RuleValue : ParseArrayField(FxPolicy, TEXT("embedded_projectile_fields")))
			{
				const TSharedPtr<FJsonObject> Rule = RuleValue.IsValid() && RuleValue->Type == EJson::Object
					? RuleValue->AsObject() : nullptr;
				FString StructPath, PropertyName;
				if (!Rule.IsValid() || !Rule->TryGetStringField(TEXT("struct_path"), StructPath) || StructPath.IsEmpty()
					|| !Rule->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
				{
					return MakeErrorJson(TEXT("style combat_fx_defaults.embedded_projectile_fields requires struct_path and property."));
				}
				for (FInstancedStruct& Fragment : Unit->ExtraData.MutableSharedFragments)
				{
					if (!Fragment.IsValid() || Fragment.GetScriptStruct()->GetPathName() != StructPath)
					{
						continue;
					}
					FObjectPropertyBase* ProjectileProperty = FindFProperty<FObjectPropertyBase>(
						Fragment.GetScriptStruct(), FName(*PropertyName));
					if (!ProjectileProperty)
					{
						continue;
					}
					void* ValuePtr = ProjectileProperty->ContainerPtrToValuePtr<void>(Fragment.GetMutableMemory());
					UObject* Source = ProjectileProperty->GetObjectPropertyValue(ValuePtr);
					FString VariantPath;
					if (Source && Variants->TryGetStringField(Source->GetPathName(), VariantPath))
					{
						UObject* Variant = StaticLoadObject(ProjectileProperty->PropertyClass, nullptr, *VariantPath);
						if (!Variant) return MakeErrorJson(TEXT("Combat FX embedded projectile default could not be loaded"));
						ProjectileProperty->SetObjectPropertyValue(ValuePtr, Variant);
					}
				}
			}
		}
		TSharedPtr<FJsonObject> FxPatch = MakeShared<FJsonObject>();
		FxPatch->SetObjectField(TEXT("Data"), FxData);
		const TSharedPtr<FJsonObject> FxOptions = MakeShared<FJsonObject>();
		FxOptions->SetBoolField(TEXT("replace_arrays"), true);
		FxPatch->SetObjectField(TEXT("options"), FxOptions);
		const FString FxFailure = ApplyCreationData(FxPatch);
		if (!FxFailure.IsEmpty())
		{
			return FxFailure;
		}
		FxDefaultReport->SetStringField(TEXT("weapon_category"), Weapon);
		FxDefaultReport->SetStringField(TEXT("impact_surface"), Surface);
		FxDefaultReport->SetBoolField(TEXT("launch_owned_by_projectile"), bProjectileAttack);
		FxDefaultReport->SetBoolField(TEXT("explicit_attack_fx_preserved"), HasJsonField(ContextAttack, TEXT("SpawnFx")));
		FxDefaultReport->SetBoolField(TEXT("explicit_hit_fx_preserved"), HasJsonField(ContextHit, TEXT("SpawnFx")));
	}

	UObject* NewAsset = nullptr;
	if (bSourceActor)
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AMassBattleUnitSource::StaticClass(),
			CreatePackage(*Destination), *AssetName, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		AMassBattleUnitSource* Source = AMassBattleUnitSource::Resolve(Blueprint);
		if (!Source || !Source->UnitData)
		{
			return MakeErrorJson(TEXT("Could not create source Actor Blueprint."));
		}
		FAssetRegistryModule::AssetCreated(Blueprint);
		UEngine::CopyPropertiesForUnrelatedObjects(Unit, Source->UnitData, CopyOptions);
		Input->TryGetStringField(TEXT("export_path"), Source->ExportPath);
		CreationPatch->TryGetStringField(TEXT("export_path"), Source->ExportPath);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		NewAsset = Blueprint;
	}
	else
	{
		UMassBattleAgentConfigDataAsset* NewUnit = DuplicateObject<UMassBattleAgentConfigDataAsset>(Unit, CreatePackage(*Destination), *AssetName);
		if (!NewUnit)
		{
			return MakeErrorJson(TEXT("Could not create unit data asset."));
		}
		NewUnit->ClearFlags(RF_Transient);
		NewUnit->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(NewUnit);
		NewAsset = NewUnit;
	}
	if (bSaveAssets && !SaveAsset(NewAsset, Error))
	{
		return MakeErrorJson(Error);
	}

	TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
	Audit->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Audit->SetStringField(TEXT("type"), TEXT("create_unit"));
	Audit->SetStringField(TEXT("template_path"), TemplatePath);
	Audit->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
	Audit->SetArrayField(TEXT("diff"), AppliedDiffs);
	AppendAuditRecord(Audit);
	if (bSourceActor)
	{
		return MCP_UnitGet(UnitPath, TEXT("{}"));
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetStringField(TEXT("operation"), TEXT("create_unit"));
	Root->SetStringField(TEXT("template_path"), TemplatePath);
	Root->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
	Root->SetBoolField(TEXT("created"), true);
	Root->SetBoolField(TEXT("saved"), bSaveAssets);
	Root->SetArrayField(TEXT("applied_diff"), AppliedDiffs);
	Root->SetObjectField(TEXT("combat_fx_defaults"), FxDefaultReport);
	if (!Derivations.IsEmpty()) Root->SetArrayField(TEXT("default_unit_patch_derivations"), Derivations);
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitDeleteSoft(const FString& UnitPath, const FString& OptionsJson)
{
	if (AMassBattleUnitSource::Resolve(FSoftObjectPath(NormalizeObjectPath(UnitPath)).TryLoad()))
	{
		TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
		if (!Options.IsValid()) return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
		Options->SetStringField(TEXT("mode"), TEXT("soft"));
		Options->SetStringField(TEXT("delete_mode"), TEXT("soft"));
		return MCP_UnitDelete(UnitPath, ToJsonString(Options));
	}
	TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	bool bDryRun = true;
	Options->TryGetBoolField(TEXT("dry_run"), bDryRun);

	FString Error;
	UMassBattleAgentConfigDataAsset* Unit = LoadUnit(UnitPath, Error);
	if (!Unit)
	{
		return MakeErrorJson(Error);
	}

	IAssetRegistry& Registry = GetAssetRegistry();
	TArray<FName> Referencers;
	Registry.GetReferencers(Unit->GetOutermost()->GetFName(), Referencers);

	TArray<TSharedPtr<FJsonValue>> RefJson;
	for (const FName& Referencer : Referencers)
	{
		RefJson.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
	}

	FString TrashRoot = TEXT("/Game/_Trash/MassBattle/Units");
	Options->TryGetStringField(TEXT("trash_root"), TrashRoot);
	const FString DateFolder = FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d"));
	const FString NewPackagePath = TrashRoot / DateFolder;
	const FString NewName = Unit->GetName();

	bool bMoved = false;
	if (!bDryRun)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TArray<FAssetRenameData> RenameData;
		RenameData.Emplace(Unit, NewPackagePath, NewName);
		bMoved = AssetTools.RenameAssets(RenameData);
		if (!bMoved)
		{
			return MakeErrorJson(FString::Printf(TEXT("Failed to move %s to %s/%s"), *UnitPath, *NewPackagePath, *NewName));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetBoolField(TEXT("dry_run"), bDryRun);
	Root->SetBoolField(TEXT("moved"), bMoved);
	Root->SetStringField(TEXT("source_path"), UnitPath);
	Root->SetStringField(TEXT("trash_path"), NewPackagePath / NewName + TEXT(".") + NewName);
	Root->SetArrayField(TEXT("referencers"), RefJson);
	Root->SetStringField(TEXT("safety_note"), TEXT("This is a soft delete by move. Permanent deletion and reference replacement are intentionally not part of this tool."));
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitDelete(const FString& UnitPath, const FString& OptionsJson)
{
	const TSharedPtr<FJsonObject> Options = ParseObject(OptionsJson);
	if (!Options.IsValid())
	{
		return MakeErrorJson(TEXT("OptionsJson is not valid JSON"));
	}

	FString Error;
	UMassBattleAgentConfigDataAsset* Unit = LoadUnit(UnitPath, Error);
	if (!Unit)
	{
		return MakeErrorJson(Error);
	}

	const FString DeleteMode = NormalizeDeleteMode(Options);
	const bool bSoftDelete = DeleteMode == TEXT("soft");
	bool bDryRun = true;
	bool bDeleteExported = false;
	bool bAllowReferenced = bSoftDelete;
	bool bForce = false;
	Options->TryGetBoolField(TEXT("dry_run"), bDryRun);
	Options->TryGetBoolField(TEXT("delete_exported_unit"), bDeleteExported);
	Options->TryGetBoolField(TEXT("allow_referenced"), bAllowReferenced);
	Options->TryGetBoolField(TEXT("force"), bForce);

	AMassBattleUnitSource* Source = Unit->GetTypedOuter<AMassBattleUnitSource>();
	UObject* DeleteTarget = Source ? Source->GetSourceAsset() : Unit;
	UObject* Exported = Source && bDeleteExported ? Source->ExportedUnit.Get() : nullptr;
	TArray<TSharedPtr<FJsonValue>> Referencers = BuildReferencerJson(DeleteTarget, Exported);
	Referencers.Append(BuildReferencerJson(Exported, DeleteTarget));

	const FString TargetPath = DeleteTarget->GetPathName();
	const bool bApplicable = bSoftDelete || Referencers.IsEmpty() || (bAllowReferenced && bForce);
	const FString BlockedReason = TEXT("Hard delete has external referencers and requires allow_referenced=true and force=true");
	FString TrashRoot = TEXT("/Game/_Trash/MassBattle/Units");
	Options->TryGetStringField(TEXT("trash_root"), TrashRoot);
	const FString TrashPackagePath = TrashRoot / FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d"));
	const FString TrashAssetName = DeleteTarget->GetName();
	const FString TrashPath = TrashPackagePath / TrashAssetName + TEXT(".") + TrashAssetName;

	const TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetStringField(TEXT("delete_mode"), DeleteMode);
	Root->SetArrayField(TEXT("referencers"), Referencers);
	if (bDryRun)
	{
		Root->SetStringField(TEXT("target_path"), TargetPath);
		Root->SetBoolField(TEXT("dry_run"), true);
		Root->SetBoolField(TEXT("would_delete"), bApplicable);
		Root->SetBoolField(TEXT("applicable"), bApplicable);
		if (bSoftDelete)
		{
			Root->SetStringField(TEXT("trash_path"), TrashPath);
		}
		if (!bApplicable)
		{
			Root->SetStringField(TEXT("blocked_reason"), BlockedReason);
		}
		return ToJsonString(Root);
	}
	if (!bApplicable)
	{
		return MakeErrorJson(BlockedReason);
	}

	const TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
	Audit->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Audit->SetStringField(TEXT("type"), TEXT("delete_unit"));
	Audit->SetStringField(TEXT("asset_path"), TargetPath);
	Audit->SetStringField(TEXT("delete_mode"), DeleteMode);
	Audit->SetArrayField(TEXT("referencers"), Referencers);
	Root->SetStringField(TEXT("asset_path"), TargetPath);

	if (bSoftDelete)
	{
		TArray<FAssetRenameData> RenameData;
		RenameData.Emplace(DeleteTarget, TrashPackagePath, TrashAssetName);
		if (Exported)
		{
			RenameData.Emplace(Exported, TrashPackagePath / TEXT("Generated"), Exported->GetName());
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (!AssetTools.RenameAssets(RenameData))
		{
			return MakeErrorJson(FString::Printf(TEXT("Failed to move %s to %s"), *TargetPath, *TrashPath));
		}

		Audit->SetBoolField(TEXT("moved"), true);
		Audit->SetStringField(TEXT("trash_path"), TrashPath);
		Root->SetBoolField(TEXT("deleted"), false);
		Root->SetBoolField(TEXT("moved"), true);
		Root->SetStringField(TEXT("trash_path"), TrashPath);
	}
	else
	{
		TArray<UObject*> Objects = { DeleteTarget };
		if (Exported)
		{
			Objects.Add(Exported);
		}
		const int32 DeletedCount = bForce
			? ObjectTools::ForceDeleteObjects(Objects, false)
			: ObjectTools::DeleteObjects(Objects, false);
		const bool bDeleted = DeletedCount >= Objects.Num();
		Audit->SetBoolField(TEXT("deleted"), bDeleted);
		if (!bDeleted)
		{
			AppendAuditRecord(Audit);
			return MakeErrorJson(FString::Printf(TEXT("ObjectTools deleted %d assets for %s"), DeletedCount, *TargetPath));
		}

		Root->SetBoolField(TEXT("deleted"), true);
		Root->SetBoolField(TEXT("moved"), false);
		Root->SetNumberField(TEXT("deleted_count"), DeletedCount);
	}

	AppendAuditRecord(Audit);
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitFindAssets(const FString& QueryJson)
{
	TSharedPtr<FJsonObject> Query = ParseObject(QueryJson);
	if (!Query.IsValid())
	{
		return MakeErrorJson(TEXT("QueryJson is not valid JSON"));
	}

	FString Text;
	Query->TryGetStringField(TEXT("query"), Text);
	Text = Text.ToLower();

	int32 Limit = 100;
	TryGetIntField(Query, TEXT("limit"), Limit);
	Limit = FMath::Clamp(Limit, 1, 1000);

	TArray<FName> Roots = ParseRoots(Query);
	TArray<FString> RootStrings;
	for (const FName& Root : Roots)
	{
		RootStrings.Add(Root.ToString());
	}

	IAssetRegistry& Registry = GetAssetRegistry();
	Registry.ScanPathsSynchronous(RootStrings, true);

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths = Roots;
	AddObjectClassFilter(Filter, UMassBattleAgentConfigDataAsset::StaticClass());
	AddObjectClassFilter(Filter, UStaticMesh::StaticClass());
	AddObjectClassFilter(Filter, USkeletalMesh::StaticClass());
	AddObjectClassFilter(Filter, UNiagaraSystem::StaticClass());
	AddObjectClassFilter(Filter, UMassBattleATTDataAsset::StaticClass());
	AddObjectClassFilter(Filter, UBlueprint::StaticClass());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString ObjectPath = Asset.GetObjectPathString();
		if (!Text.IsEmpty() && !ObjectPath.ToLower().Contains(Text))
		{
			continue;
		}
		if (Results.Num() >= Limit)
		{
			break;
		}

		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Item->SetStringField(TEXT("path"), ObjectPath);
		Item->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
		Item->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Item->SetStringField(TEXT("style_family"), InferStyleFamily(ObjectPath));
		Results.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetNumberField(TEXT("count"), Results.Num());
	AddRootsToJson(Root, Query);
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
}

FString UMassBattleUnitMCPApi::MCP_UnitGetApiStatus()
{
	TSharedPtr<FJsonObject> Root = MakeSuccess();
	Root->SetStringField(TEXT("api_name"), TEXT("MassBattleUnitMCPApi"));
	Root->SetStringField(TEXT("module"), TEXT("MassBattleEditorMCP"));
	Root->SetStringField(TEXT("dependency_note"), TEXT("Independent from UMGMCP. Any MCP transport can call these JSON UFUNCTION endpoints."));

	auto Tool = [](const FString& Name, const FString& Category, const FString& Description)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("category"), Category);
		Obj->SetStringField(TEXT("description"), Description);
		return MakeShared<FJsonValueObject>(Obj);
	};

	TArray<TSharedPtr<FJsonValue>> Tools;
	Tools.Add(Tool(TEXT("MCP_UnitList"), TEXT("unit.query"), TEXT("List MassBattle unit config assets.")));
	Tools.Add(Tool(TEXT("MCP_UnitGet"), TEXT("unit.query"), TEXT("Get one unit with default ignore policy.")));
	Tools.Add(Tool(TEXT("MCP_UnitGetSchema"), TEXT("unit.schema"), TEXT("Expose editable schema and field roles.")));
	Tools.Add(Tool(TEXT("MCP_UnitExport"), TEXT("unit.export"), TEXT("Export balance fields to CSV/JSON.")));
	Tools.Add(Tool(TEXT("MCP_UnitCreate"), TEXT("unit.create"), TEXT("Create a unit or source Actor with explicit data and the selected style's defaults.")));
	Tools.Add(Tool(TEXT("MCP_UnitWrite"), TEXT("unit.write"), TEXT("Write the requested fields of an existing unit or Actor Source.")));
	Tools.Add(Tool(TEXT("MCP_UnitDeleteSoft"), TEXT("unit.lifecycle"), TEXT("Move a unit to trash after referencer scan.")));
	Tools.Add(Tool(TEXT("MCP_UnitDelete"), TEXT("unit.delete"), TEXT("Delete a unit explicitly; dry_run=true by default.")));
	Tools.Add(Tool(TEXT("MCP_UnitFindAssets"), TEXT("unit.assets"), TEXT("Find existing assets for unit generation.")));
	Tools.Add(Tool(TEXT("MCP_EditorListStyles"), TEXT("unit_editor.style"), TEXT("List installed unit styles.")));
	Tools.Add(Tool(TEXT("MCP_EditorGetStyle"), TEXT("unit_editor.style"), TEXT("Read one unit style's defaults.")));
	Tools.Add(Tool(TEXT("MCP_EditorPlanUnitAuthoringWorkflow"), TEXT("unit_editor.workflow"), TEXT("Plan a staged workflow across prepare, animation update, VAT create/refresh, and organization.")));
	Tools.Add(Tool(TEXT("MCP_EditorApplyUnitAuthoringWorkflow"), TEXT("unit_editor.workflow"), TEXT("Apply a reviewed staged unit authoring workflow; dry_run=true by default.")));
	Tools.Add(Tool(TEXT("MCP_EditorPlanPreparePurchasedAsset"), TEXT("unit_editor.prepare"), TEXT("Plan discovery, official naming, and optional source-folder organization for a purchased skeletal asset pack.")));
	Tools.Add(Tool(TEXT("MCP_EditorApplyPreparePurchasedAsset"), TEXT("unit_editor.prepare"), TEXT("Apply a reviewed purchased-asset preparation plan; dry_run=true by default.")));
	Tools.Add(Tool(TEXT("MCP_EditorDiscoverCompatibleAnimations"), TEXT("unit_editor.animation"), TEXT("Discover compatible animation sequences across explicit and style-configured search roots.")));
	Tools.Add(Tool(TEXT("MCP_EditorPlanCreateVatUnit"), TEXT("unit_editor.create.diagnostic"), TEXT("Diagnostic: preview the MassBattleTools DoAll-equivalent VAT unit plan. Strict apply still requires canonical complete inputs.")));
	Tools.Add(Tool(TEXT("MCP_EditorValidateCreateVatUnit"), TEXT("unit_editor.create.diagnostic"), TEXT("Diagnostic: validate DoAll-equivalent VAT unit inputs without writing assets.")));
	Tools.Add(Tool(TEXT("MCP_EditorApplyCreateVatUnit"), TEXT("unit_editor.create"), TEXT("Primary non-selection DoAll-equivalent VAT unit authoring entry. Requires canonical complete inputs and validates before writing assets.")));
	Tools.Add(Tool(TEXT("MCP_EditorPlanCreateVatUnitFromSelection"), TEXT("unit_editor.create.diagnostic"), TEXT("Resolve the selection to options to review and pass to MCP_EditorApplyCreateVatUnit.")));
	Tools.Add(Tool(TEXT("MCP_EditorInspectActorAssembly"), TEXT("unit_editor.actor"), TEXT("Inspect an Actor's modular skeletal/static component assembly and weapon bindings.")));
	Tools.Add(Tool(TEXT("MCP_EditorInspectVatAnimation"), TEXT("unit_editor.actor"), TEXT("Audit assembled skin weights, baked VAT position-frame motion, lookup UVs, and the selected animation profile.")));
	Tools.Add(Tool(TEXT("MCP_EditorPlanCreateVatUnitFromActor"), TEXT("unit_editor.actor"), TEXT("Plan Actor assembly followed by strict VAT unit authoring.")));
	Tools.Add(Tool(TEXT("MCP_EditorApplyCreateVatUnitFromActor"), TEXT("unit_editor.actor"), TEXT("Assemble an Actor and weapon into an animation-compatible SkeletalMesh, then author the VAT unit.")));
	Tools.Add(Tool(TEXT("MCP_EditorApplyOrganizeUnitAssets"), TEXT("unit_editor.organize"), TEXT("Preview a linked-asset organization plan with dry_run=true, or apply it with dry_run=false.")));
	Root->SetArrayField(TEXT("tools"), Tools);
	Root->SetObjectField(TEXT("default_ignore_policy"), BuildIgnorePolicyJson());
	return ToJsonString(Root);
}
