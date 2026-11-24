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

	/** Object path of the transition rule graph (usable as a graph reference in other tools). */
	UPROPERTY(BlueprintReadOnly, Category = "AnimGraph")
	FString RuleGraphPath;
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

	/**
	 * Creates a transition (arrow) between two states of a state machine, if one does not already exist.
	 * The new transition starts with an empty rule (never fires); set its rule afterwards.
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state (for example "Idle").
	 * @param ToState Name of the destination state (for example "AgachadoBlendSpace1D").
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool CreateStateTransition(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState);

	/**
	 * Makes a transition use the automatic rule based on the asset player's remaining time
	 * (plays the animation of the source state fully before blending), or disables that rule.
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state.
	 * @param ToState Name of the destination state.
	 * @param bEnabled True to use the automatic sequence-player rule, false to use the graph rule.
	 * @param TriggerTime Seconds before the end of the asset player to trigger. Ignored when bEnabled is false.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool SetTransitionAutomaticRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bEnabled, double TriggerTime);

	/**
	 * Rebuilds the rule of a transition as "Variable == bExpectedValue" (a boolean Animation Blueprint variable).
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state.
	 * @param ToState Name of the destination state.
	 * @param VariableName Name of a boolean variable of the Animation Blueprint (without the b prefix added by UHT).
	 * @param bExpectedValue Value the variable must have for the transition to be taken.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool SetTransitionBoolRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, const FString& VariableName, bool bExpectedValue);

	/**
	 * Rebuilds the rule of a transition as "Variable > Threshold" or "Variable < Threshold".
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state.
	 * @param ToState Name of the destination state.
	 * @param VariableName Name of a numeric variable of the Animation Blueprint (for example Speed).
	 * @param bGreaterThan True for ">", false for "<".
	 * @param Threshold Value to compare against.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool SetTransitionThresholdRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, const FString& VariableName, bool bGreaterThan, double Threshold);

	/**
	 * Rebuilds the rule of a transition as a constant (always enter or never enter).
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state.
	 * @param ToState Name of the destination state.
	 * @param bValue True to always allow the transition, false to never allow it.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool SetTransitionConstantRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bValue);

	/**
	 * Enables or disables a transition without deleting it.
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint.
	 * @param FromState Name of the source state.
	 * @param ToState Name of the destination state.
	 * @param bDisabled True to disable the transition.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static bool SetTransitionDisabled(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bDisabled);

	/**
	 * Adds a "get variable" node (wired to Self) to any graph of an Animation Blueprint, for example a
	 * state graph, and returns the new node path so its pins can be connected.
	 * @param AnimBlueprintPath Asset path of the Animation Blueprint that owns the graph.
	 * @param GraphPath Object path of the target graph.
	 * @param VariableName Name of the variable to read.
	 */
	UFUNCTION(meta = (AICallable), Category = "AnimGraph")
	static FString AddVariableGetterToGraph(const FString& AnimBlueprintPath, const FString& GraphPath, const FString& VariableName);
};
