// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "MassBattleEditorVatSource.generated.h"

class AActor;
class UBlueprint;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EMassBattleMCPVatSourceType : uint8
{
	SkeletalMesh UMETA(DisplayName = "Skeletal Mesh"),
	ActorBlueprint UMETA(DisplayName = "Actor Blueprint"),
	ActorInstance UMETA(DisplayName = "Actor Instance")
};

/** Editor source owned by MCP, independent of the selected MassBattle runtime edition. */
USTRUCT(BlueprintType)
struct MASSBATTLEEDITORMCP_API FMassBattleMCPVatSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	EMassBattleMCPVatSourceType SourceType = EMassBattleMCPVatSourceType::SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (EditCondition = "SourceType == EMassBattleMCPVatSourceType::SkeletalMesh", EditConditionHides))
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (EditCondition = "SourceType == EMassBattleMCPVatSourceType::ActorBlueprint", EditConditionHides))
	TObjectPtr<UBlueprint> ActorBlueprint = nullptr;

	/** Retained for previously serialized Source values; new selections use ActorBlueprint. */
	UPROPERTY(BlueprintReadWrite, Category = "Source", meta = (DeprecatedProperty, DeprecationMessage = "Use ActorBlueprint."))
	TSubclassOf<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (EditCondition = "SourceType == EMassBattleMCPVatSourceType::ActorInstance", EditConditionHides))
	TObjectPtr<AActor> ActorInstance = nullptr;
};
