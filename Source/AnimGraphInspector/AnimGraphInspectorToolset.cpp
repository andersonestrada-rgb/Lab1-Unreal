// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphInspectorToolset.h"

#include "Animation/AnimBlueprint.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Loads the animation blueprint, raising (via ensure-like null) when it is not valid. */
	UAnimBlueprint* LoadAnimBlueprint(const FString& InPath)
	{
		if (InPath.IsEmpty())
		{
			return nullptr;
		}

		FString AssetPath = InPath;
		AssetPath.TrimStartAndEndInline();

		// Accept both "/Game/Path/Asset" and "/Game/Path/Asset.Asset".
		const FString ObjectPath = AssetPath.Contains(TEXT(".")) ? AssetPath : (AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath));

		return LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	}

	/** Recursively collects the graph plus every sub graph (state machine / state / macro graphs). */
	void CollectGraphs(UEdGraph* Graph, TArray<UEdGraph*>& OutGraphs, TSet<UEdGraph*>& Visited)
	{
		if (!Graph || Visited.Contains(Graph))
		{
			return;
		}

		Visited.Add(Graph);
		OutGraphs.Add(Graph);

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			for (UEdGraph* SubGraph : Node->GetSubGraphs())
			{
				CollectGraphs(SubGraph, OutGraphs, Visited);
			}

			if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node))
			{
				CollectGraphs(StateNode->GetBoundGraph(), OutGraphs, Visited);
			}
		}
	}

	/** Gathers every state machine graph reachable from the animation blueprint. */
	void GatherStateMachineGraphs(UAnimBlueprint* AnimBlueprint, TArray<UAnimationStateMachineGraph*>& OutGraphs)
	{
		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);

		TArray<UEdGraph*> RootGraphs = AllGraphs;
		for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
		{
			RootGraphs.AddUnique(Graph);
		}
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			RootGraphs.AddUnique(Graph);
		}
		for (UEdGraph* Graph : AnimBlueprint->MacroGraphs)
		{
			RootGraphs.AddUnique(Graph);
		}

		TArray<UEdGraph*> Collected;
		TSet<UEdGraph*> Visited;
		for (UEdGraph* Graph : RootGraphs)
		{
			CollectGraphs(Graph, Collected, Visited);
		}

		for (UEdGraph* Graph : Collected)
		{
			if (UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Graph))
			{
				OutGraphs.AddUnique(StateMachineGraph);
			}
		}
	}

	/** Compact text dump of a transition rule graph: node titles plus pin values and links. */
	FString SummarizeRuleGraph(const UEdGraph* RuleGraph)
	{
		if (!RuleGraph)
		{
			return TEXT("<no rule graph>");
		}

		TArray<FString> Lines;
		for (UEdGraphNode* Node : RuleGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			const FString ClassName = Node->GetClass()->GetName();

			TArray<FString> Inputs;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || Pin->bHidden)
				{
					continue;
				}

				FString Desc = Pin->PinName.ToString();
				if (!Pin->DefaultValue.IsEmpty())
				{
					Desc += FString::Printf(TEXT(" = %s"), *Pin->DefaultValue);
				}
				else if (Pin->DefaultObject)
				{
					Desc += FString::Printf(TEXT(" = <%s>"), *Pin->DefaultObject->GetName());
				}

				if (Pin->LinkedTo.Num() > 0)
				{
					TArray<FString> Sources;
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin)
						{
							continue;
						}
						const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
						Sources.Add(FString::Printf(TEXT("%s.%s"),
							LinkedNode ? *LinkedNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("?"),
							*LinkedPin->PinName.ToString()));
					}
					Desc += FString::Printf(TEXT("  <-  [%s]"), *FString::Join(Sources, TEXT(", ")));
				}

				Inputs.Add(Desc);
			}

			Lines.Add(FString::Printf(TEXT("%s <%s>%s%s"),
				*Title,
				*ClassName,
				Inputs.Num() > 0 ? TEXT(" : ") : TEXT(""),
				*FString::Join(Inputs, TEXT(" ; "))));
		}

		return Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("<empty rule graph>");
	}

	FString GetStateNameSafe(const UAnimStateNodeBase* StateNode)
	{
		return StateNode ? StateNode->GetStateName() : FString(TEXT("<none>"));
	}
}

TArray<FAnimGraphTransitionInfo> UAnimGraphInspectorToolset::ListStateMachineTransitions(const FString& AnimBlueprintPath)
{
	TArray<FAnimGraphTransitionInfo> Result;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return Result;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
			if (!Transition)
			{
				continue;
			}

			FAnimGraphTransitionInfo& Info = Result.AddDefaulted_GetRef();
			Info.StateMachine = StateMachineGraph->GetName();
			Info.FromState = GetStateNameSafe(Transition->GetPreviousState());
			Info.ToState = GetStateNameSafe(Transition->GetNextState());
			Info.bBidirectional = Transition->Bidirectional;
			Info.bAutomaticRule = Transition->bAutomaticRuleBasedOnSequencePlayerInState;
			Info.bDisabled = Transition->bDisabled;
			Info.RuleSummary = SummarizeRuleGraph(Transition->BoundGraph);
		}
	}

	return Result;
}

TArray<FString> UAnimGraphInspectorToolset::ListStateMachineStates(const FString& AnimBlueprintPath)
{
	TArray<FString> Result;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return Result;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				Result.Add(FString::Printf(TEXT("%s::%s"), *StateMachineGraph->GetName(), *StateNode->GetStateName()));
			}
		}
	}

	return Result;
}

FString UAnimGraphInspectorToolset::GetStateMachineGraphText(const FString& AnimBlueprintPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return TEXT("<anim blueprint not found>");
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("== %s =="), *AnimBlueprint->GetPathName()));

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		Lines.Add(FString::Printf(TEXT("\n[StateMachine] %s"), *StateMachineGraph->GetName()));

		Lines.Add(TEXT("  States:"));
		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				Lines.Add(FString::Printf(TEXT("    - %s"), *StateNode->GetStateName()));
			}
		}

		Lines.Add(TEXT("  Transitions:"));
		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
			if (!Transition)
			{
				continue;
			}

			Lines.Add(FString::Printf(TEXT("    * %s -> %s   [bidirectional=%s, autoRule=%s, disabled=%s]"),
				*GetStateNameSafe(Transition->GetPreviousState()),
				*GetStateNameSafe(Transition->GetNextState()),
				Transition->Bidirectional ? TEXT("true") : TEXT("false"),
				Transition->bAutomaticRuleBasedOnSequencePlayerInState ? TEXT("true") : TEXT("false"),
				Transition->bDisabled ? TEXT("true") : TEXT("false")));

			Lines.Add(FString::Printf(TEXT("        rule: %s"), *SummarizeRuleGraph(Transition->BoundGraph).Replace(TEXT("\n"), TEXT("\n        "))));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}
