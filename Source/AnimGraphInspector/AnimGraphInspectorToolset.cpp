// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphInspectorToolset.h"

#include "Animation/AnimBlueprint.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
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

	/** Finds a state node by its display name inside a state machine graph. */
	UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* StateMachineGraph, const FString& StateName)
	{
		if (!StateMachineGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				if (StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
				{
					return StateNode;
				}
			}
		}

		return nullptr;
	}

	/** Finds an existing transition between two states. */
	UAnimStateTransitionNode* FindTransition(UAnimationStateMachineGraph* StateMachineGraph, const UAnimStateNodeBase* FromState, const UAnimStateNodeBase* ToState)
	{
		if (!StateMachineGraph || !FromState || !ToState)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
			{
				if (Transition->GetPreviousState() == FromState && Transition->GetNextState() == ToState)
				{
					return Transition;
				}
			}
		}

		return nullptr;
	}

	/** Returns the first visible pin of a node with the wanted direction. */
	UEdGraphPin* FindFirstPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden && Pin->Direction == Direction)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	/** Removes every node of a transition rule graph except the transition result node. */
	void ClearRuleNodes(UEdGraph* RuleGraph)
	{
		if (!RuleGraph)
		{
			return;
		}

		for (int32 Index = RuleGraph->Nodes.Num() - 1; Index >= 0; --Index)
		{
			UEdGraphNode* Node = RuleGraph->Nodes[Index];
			if (!Node || Node->IsA<UAnimGraphNode_TransitionResult>())
			{
				continue;
			}

			RuleGraph->RemoveNode(Node);
		}
	}

	/** Finds the transition result node of a rule graph. */
	UAnimGraphNode_TransitionResult* FindResultNode(UEdGraph* RuleGraph)
	{
		if (!RuleGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : RuleGraph->Nodes)
		{
			if (UAnimGraphNode_TransitionResult* Result = Cast<UAnimGraphNode_TransitionResult>(Node))
			{
				return Result;
			}
		}

		return nullptr;
	}

	/** Creates and links two pins using the graph schema, if possible. */
	bool LinkPins(UEdGraph* Graph, UEdGraphPin* OutputPin, UEdGraphPin* InputPin)
	{
		if (!Graph || !OutputPin || !InputPin)
		{
			UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: LinkPins got a null argument"));
			return false;
		}

		if (const UEdGraphSchema* Schema = Graph->GetSchema())
		{
			if (Schema->TryCreateConnection(OutputPin, InputPin))
			{
				return true;
			}
		}

		OutputPin->MakeLinkTo(InputPin);

		const bool bLinked = OutputPin->LinkedTo.Contains(InputPin) || InputPin->LinkedTo.Contains(OutputPin);
		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: LinkPins fallback %s [%s] -> [%s]"),
			bLinked ? TEXT("OK") : TEXT("FAILED"), *OutputPin->PinName.ToString(), *InputPin->PinName.ToString());
		return bLinked;
	}

	/** Creates a "get variable" node in a transition rule graph. */
	UK2Node_VariableGet* AddVariableGetNode(UEdGraph* RuleGraph, const FString& VariableName, UAnimBlueprint* AnimBlueprint)
	{
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(RuleGraph);
		Node->VariableReference.SetSelfMember(FName(*VariableName));
		RuleGraph->AddNode(Node, false, false);
		Node->ReconstructNode();

		UEdGraphPin* ValuePin = Node->GetValuePin();
		const bool bResolved = ValuePin && ValuePin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard;

		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: self-scope getter '%s' bp=%s resolved=%d"),
			*VariableName, *GetNameSafe(Node->GetBlueprint()), bResolved ? 1 : 0);

		// Fallback for graphs whose blueprint cannot be resolved: reference the variable through the
		// (stable) generated class instead. The node then needs its "self" pin wired to a Self node.
		if (!bResolved && AnimBlueprint && AnimBlueprint->GeneratedClass)
		{
			Node->VariableReference.SetExternalMember(FName(*VariableName), AnimBlueprint->GeneratedClass);
			Node->ReconstructNode();
			ValuePin = Node->GetValuePin();

			UEdGraphPin* SelfPin = Node->FindPin(UEdGraphSchema_K2::PN_Self);
			if (SelfPin && SelfPin->Direction == EGPD_Input && SelfPin->LinkedTo.Num() == 0)
			{
				UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(RuleGraph);
				RuleGraph->AddNode(SelfNode, false, false);
				SelfNode->ReconstructNode();
				SelfNode->NodePosX = -620;
				SelfNode->NodePosY = 40;

				if (UEdGraphPin* SelfOut = SelfNode->FindPin(UEdGraphSchema_K2::PN_Self))
				{
					UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: wiring Self -> getter self pin: %s"),
						LinkPins(RuleGraph, SelfOut, SelfPin) ? TEXT("ok") : TEXT("failed"));
				}
			}
		}

		Node->NodePosX = -400;
		Node->NodePosY = 0;
		return Node;
	}

	/** Returns the class that owns the Animation Blueprint variables. */
	UClass* GetAnimVariableScope(UAnimBlueprint* AnimBlueprint)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		if (AnimBlueprint->SkeletonGeneratedClass)
		{
			return AnimBlueprint->SkeletonGeneratedClass;
		}

		return AnimBlueprint->GeneratedClass;
	}

	/** Locates a KismetMathLibrary function by name, trying both common lookup paths. */
	UFunction* FindMathFunction(const FName& FunctionName)
	{
		UClass* MathClass = UKismetMathLibrary::StaticClass();
		if (UFunction* Function = MathClass ? MathClass->FindFunctionByName(FunctionName) : nullptr)
		{
			return Function;
		}

		const FString Path = FString::Printf(TEXT("/Script/Engine.KismetMathLibrary:%s"), *FunctionName.ToString());
		if (UFunction* Function = FindObject<UFunction>(nullptr, *Path))
		{
			return Function;
		}

		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: could not find math function '%s'"), *FunctionName.ToString());
		return nullptr;
	}

	/** Creates a bool-returning comparison call node (Greater/Less) based on the pin type. */
	UK2Node_CallFunction* AddComparisonNode(UEdGraph* RuleGraph, const FEdGraphPinType& PinType, bool bGreaterThan)
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: comparison category='%s' subcategory='%s'"),
			*PinType.PinCategory.ToString(), *PinType.PinSubCategory.ToString());

		FName FunctionName;

		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int || PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
		{
			FunctionName = bGreaterThan ? FName(TEXT("Greater_IntInt")) : FName(TEXT("Less_IntInt"));
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Float
			|| (PinType.PinCategory == UEdGraphSchema_K2::PC_Real && PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float))
		{
			FunctionName = bGreaterThan ? FName(TEXT("Greater_FloatFloat")) : FName(TEXT("Less_FloatFloat"));
		}
		else
		{
			FunctionName = bGreaterThan ? FName(TEXT("Greater_DoubleDouble")) : FName(TEXT("Less_DoubleDouble"));
		}

		UFunction* Function = FindMathFunction(FunctionName);
		if (!Function)
		{
			return nullptr;
		}

		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(RuleGraph);
		Node->FunctionReference.SetExternalMember(Function->GetFName(), Function->GetOuterUClass());
		RuleGraph->AddNode(Node, false, false);
		Node->ReconstructNode();
		Node->NodePosX = -150;
		Node->NodePosY = 0;

		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: comparison node pins A=%s B=%s Ret=%s"),
			Node->FindPin(TEXT("A")) ? TEXT("ok") : TEXT("null"),
			Node->FindPin(TEXT("B")) ? TEXT("ok") : TEXT("null"),
			Node->GetReturnValuePin() ? TEXT("ok") : TEXT("null"));

		return Node;
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
			Info.RuleGraphPath = Transition->BoundGraph ? Transition->BoundGraph->GetPathName() : FString();
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

bool UAnimGraphInspectorToolset::CreateStateTransition(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateNode* FromNode = FindStateNode(StateMachineGraph, FromState);
		UAnimStateNode* ToNode = FindStateNode(StateMachineGraph, ToState);
		if (!FromNode || !ToNode)
		{
			continue;
		}

		// Already wired? Nothing to do.
		if (FindTransition(StateMachineGraph, FromNode, ToNode))
		{
			return true;
		}

		const UEdGraphSchema* Schema = StateMachineGraph->GetSchema();
		UEdGraphPin* FromPin = FindFirstPin(FromNode, EGPD_Output);
		UEdGraphPin* ToPin = FindFirstPin(ToNode, EGPD_Input);
		if (!Schema || !FromPin || !ToPin)
		{
			return false;
		}

		AnimBlueprint->Modify();
		if (!Schema->TryCreateConnection(FromPin, ToPin))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();

		return FindTransition(StateMachineGraph, FromNode, ToNode) != nullptr;
	}

	return false;
}

bool UAnimGraphInspectorToolset::SetTransitionAutomaticRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bEnabled, double TriggerTime)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateNode* FromNode = FindStateNode(StateMachineGraph, FromState);
		UAnimStateNode* ToNode = FindStateNode(StateMachineGraph, ToState);
		if (!FromNode || !ToNode)
		{
			continue;
		}

		UAnimStateTransitionNode* Transition = FindTransition(StateMachineGraph, FromNode, ToNode);
		if (!Transition)
		{
			continue;
		}

		AnimBlueprint->Modify();
		Transition->Modify();
		Transition->bAutomaticRuleBasedOnSequencePlayerInState = bEnabled;
		if (bEnabled)
		{
			Transition->AutomaticRuleTriggerTime = static_cast<float>(TriggerTime);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();
		return true;
	}

	return false;
}

bool UAnimGraphInspectorToolset::SetTransitionBoolRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, const FString& VariableName, bool bExpectedValue)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateTransitionNode* Transition = FindTransition(StateMachineGraph, FindStateNode(StateMachineGraph, FromState), FindStateNode(StateMachineGraph, ToState));
		if (!Transition || !Transition->BoundGraph)
		{
			continue;
		}

		UEdGraph* RuleGraph = Transition->BoundGraph;
		AnimBlueprint->Modify();
		Transition->Modify();
		ClearRuleNodes(RuleGraph);

		UAnimGraphNode_TransitionResult* Result = FindResultNode(RuleGraph);
		UEdGraphPin* ResultPin = Result ? Result->FindPin(TEXT("bCanEnterTransition")) : nullptr;
		if (!ResultPin)
		{
			return false;
		}

		UK2Node_VariableGet* GetNode = AddVariableGetNode(RuleGraph, VariableName, AnimBlueprint);
		UEdGraphPin* ValuePin = GetNode ? GetNode->GetValuePin() : nullptr;
		if (!ValuePin && GetNode)
		{
			ValuePin = GetNode->FindPin(FName(*VariableName));
		}
		if (!ValuePin)
		{
			UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: rule failed, no value pin for '%s'"), *VariableName);
			return false;
		}

		bool bLinked = false;
		if (bExpectedValue)
		{
			bLinked = LinkPins(RuleGraph, ValuePin, ResultPin);
		}
		else
		{
			UFunction* NotFunction = FindMathFunction(FName(TEXT("Not_PreBool")));
			if (!NotFunction)
			{
				return false;
			}

			UK2Node_CallFunction* NotNode = NewObject<UK2Node_CallFunction>(RuleGraph);
			NotNode->FunctionReference.SetExternalMember(NotFunction->GetFName(), NotFunction->GetOuterUClass());
			RuleGraph->AddNode(NotNode, false, false);
			NotNode->ReconstructNode();
			NotNode->NodePosX = -150;
			NotNode->NodePosY = 0;

			UEdGraphPin* APin = NotNode->FindPin(TEXT("A"));
			UEdGraphPin* ReturnPin = NotNode->GetReturnValuePin();
			UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: NOT node pins A=%s Return=%s"),
				APin ? TEXT("ok") : TEXT("null"), ReturnPin ? TEXT("ok") : TEXT("null"));

			bLinked = APin && ReturnPin && LinkPins(RuleGraph, ValuePin, APin) && LinkPins(RuleGraph, ReturnPin, ResultPin);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();
		return bLinked;
	}

	return false;
}

bool UAnimGraphInspectorToolset::SetTransitionThresholdRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, const FString& VariableName, bool bGreaterThan, double Threshold)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateTransitionNode* Transition = FindTransition(StateMachineGraph, FindStateNode(StateMachineGraph, FromState), FindStateNode(StateMachineGraph, ToState));
		if (!Transition || !Transition->BoundGraph)
		{
			continue;
		}

		UEdGraph* RuleGraph = Transition->BoundGraph;
		AnimBlueprint->Modify();
		Transition->Modify();
		ClearRuleNodes(RuleGraph);

		UAnimGraphNode_TransitionResult* Result = FindResultNode(RuleGraph);
		UEdGraphPin* ResultPin = Result ? Result->FindPin(TEXT("bCanEnterTransition")) : nullptr;
		if (!ResultPin)
		{
			return false;
		}

		UK2Node_VariableGet* GetNode = AddVariableGetNode(RuleGraph, VariableName, AnimBlueprint);
		UEdGraphPin* ValuePin = GetNode ? GetNode->GetValuePin() : nullptr;
		if (!ValuePin && GetNode)
		{
			ValuePin = GetNode->FindPin(FName(*VariableName));
		}
		if (!ValuePin)
		{
			UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: threshold rule failed, no value pin for '%s'"), *VariableName);
			return false;
		}

		UK2Node_CallFunction* ComparisonNode = AddComparisonNode(RuleGraph, ValuePin->PinType, bGreaterThan);
		if (!ComparisonNode)
		{
			return false;
		}

		UEdGraphPin* APin = ComparisonNode->FindPin(TEXT("A"));
		UEdGraphPin* BPin = ComparisonNode->FindPin(TEXT("B"));
		UEdGraphPin* ReturnPin = ComparisonNode->GetReturnValuePin();
		if (!APin || !BPin || !ReturnPin)
		{
			return false;
		}

		BPin->DefaultValue = FString::SanitizeFloat(Threshold);

		const bool bLinked = LinkPins(RuleGraph, ValuePin, APin) && LinkPins(RuleGraph, ReturnPin, ResultPin);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();
		return bLinked;
	}

	return false;
}

bool UAnimGraphInspectorToolset::SetTransitionConstantRule(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bValue)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateTransitionNode* Transition = FindTransition(StateMachineGraph, FindStateNode(StateMachineGraph, FromState), FindStateNode(StateMachineGraph, ToState));
		if (!Transition || !Transition->BoundGraph)
		{
			continue;
		}

		UEdGraph* RuleGraph = Transition->BoundGraph;
		AnimBlueprint->Modify();
		Transition->Modify();
		ClearRuleNodes(RuleGraph);

		UAnimGraphNode_TransitionResult* Result = FindResultNode(RuleGraph);
		UEdGraphPin* ResultPin = Result ? Result->FindPin(TEXT("bCanEnterTransition")) : nullptr;
		if (!ResultPin)
		{
			return false;
		}

		ResultPin->DefaultValue = bValue ? TEXT("true") : TEXT("false");

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();
		return true;
	}

	return false;
}

bool UAnimGraphInspectorToolset::SetTransitionDisabled(const FString& AnimBlueprintPath, const FString& FromState, const FString& ToState, bool bDisabled)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	TArray<UAnimationStateMachineGraph*> StateMachineGraphs;
	GatherStateMachineGraphs(AnimBlueprint, StateMachineGraphs);

	for (UAnimationStateMachineGraph* StateMachineGraph : StateMachineGraphs)
	{
		UAnimStateTransitionNode* Transition = FindTransition(StateMachineGraph, FindStateNode(StateMachineGraph, FromState), FindStateNode(StateMachineGraph, ToState));
		if (!Transition)
		{
			continue;
		}

		AnimBlueprint->Modify();
		Transition->Modify();
		Transition->bDisabled = bDisabled;

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		AnimBlueprint->MarkPackageDirty();
		return true;
	}

	return false;
}

FString UAnimGraphInspectorToolset::AddVariableGetterToGraph(const FString& AnimBlueprintPath, const FString& GraphPath, const FString& VariableName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	UEdGraph* Graph = LoadObject<UEdGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimGraphInspector: graph not found '%s'"), *GraphPath);
		return FString();
	}

	AnimBlueprint->Modify();
	UK2Node_VariableGet* Node = AddVariableGetNode(Graph, VariableName, AnimBlueprint);
	if (!Node)
	{
		return FString();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	AnimBlueprint->MarkPackageDirty();
	return Node->GetPathName();
}

