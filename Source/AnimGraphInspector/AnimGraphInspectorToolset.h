// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "AnimGraphInspectorToolset.generated.h"

/** One state machine transition (arrow) with its rule summary. */
USTRUCT(BlueprintType)
struct FAnimGraphTransitionInfo
{
	GENERATED_BODY()

	/** Name of the state machine graph that owns this transition. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	FString StateMachine;

	/** State the arrow comes FROM. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	FString FromState;

	/** State the arrow points TO. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	FString ToState;

	/** True when the transition can be taken in both directions. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	bool bBidirectional = false;

	/** True when the transition uses the automatic sequence-player rule instead of the graph. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	bool bAutomaticRule = false;

	/** True when the transition is currently disabled in the editor. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	bool bDisabled = false;

	/** Human readable dump of the transition rule graph (nodes, pin values and links). */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	FString RuleSummary;
};

/**
 * Read-only inspector for Animation Blueprint state machines.
 * Lets an AI read the exact wiring (arrows and transition rules) of AnimGraph state machines,
 * which is not exposed by the generic Blueprint tools.
 */
UCLASS(BlueprintType, MinimalAPI)
class UAnimGraphInspectorToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Lists every transition of every state machine inside an Animation Blueprint, including
	 * the from/to states, flags and a text dump of the transition rule.
	 * @param AnimBlueprintPath Asset path, for example /Game/Modelos3D/Entidades/Player/Animaciones/AgnesAnimBlueprint
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static TArray<FAnimGraphTransitionInfo> ListStateMachineTransitions(const FString& AnimBlueprintPath);

	/**
	 * Lists every state of every state machine inside an Animation Blueprint, as "StateMachine::State".
	 * @param AnimBlueprintPath Asset path, for example /Game/Modelos3D/Entidades/Player/Animaciones/AgnesAnimBlueprint
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static TArray<FString> ListStateMachineStates(const FString& AnimBlueprintPath);

	/**
	 * Returns a plain-text dump of all state machines of an Animation Blueprint: states, transitions
	 * (from -> to) and the nodes/links of every transition rule graph.
	 * @param AnimBlueprintPath Asset path, for example /Game/Modelos3D/Entidades/Player/Animaciones/AgnesAnimBlueprint
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static FString GetStateMachineGraphText(const FString& AnimBlueprintPath);
};
