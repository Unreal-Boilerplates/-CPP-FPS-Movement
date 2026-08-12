#include "AC_InputMapper.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// --- Free helpers ------------------------------------------------------------

// The owning actor's class, given any object in a component template's outer chain.
static const UClass* OwnerClassFromObject(UObject* Object)
{
    if (const AActor* Actor = Cast<AActor>(Object))
        return Actor->GetClass();
    const UClass* AsClass = Cast<UClass>(Object);
    return (AsClass && AsClass->IsChildOf(AActor::StaticClass())) ? AsClass : nullptr;
}

// First non-return parameter of a function, or null if it takes none.
static FProperty* FindFirstInputParam(UFunction* Function)
{
    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
        if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
            return *It;
    return nullptr;
}

static void WriteStructValue(const FStructProperty* Property, void* Frame, const FInputActionValue& Value)
{
    void* Dest = Property->ContainerPtrToValuePtr<void>(Frame);
    if (Property->Struct == FInputActionValue::StaticStruct()) { *static_cast<FInputActionValue*>(Dest) = Value; return; }
    if (Property->Struct == TBaseStructure<FVector2D>::Get())  { *static_cast<FVector2D*>(Dest) = Value.Get<FVector2D>(); return; }
    if (Property->Struct == TBaseStructure<FVector>::Get())    { *static_cast<FVector*>(Dest) = Value.Get<FVector>(); }
}

// Converts the action value to the parameter's type and writes it into the frame.
static void WriteActionValue(FProperty* Param, void* Frame, const FInputActionValue& Value)
{
    if (!Param) return;
    if (const auto* P = CastField<FFloatProperty>(Param))  { P->SetPropertyValue_InContainer(Frame, Value.Get<float>()); return; }
    if (const auto* P = CastField<FDoubleProperty>(Param)) { P->SetPropertyValue_InContainer(Frame, Value.Get<float>()); return; }
    if (const auto* P = CastField<FBoolProperty>(Param))   { P->SetPropertyValue_InContainer(Frame, Value.Get<bool>()); return; }
    if (const auto* P = CastField<FStructProperty>(Param))   WriteStructValue(P, Frame, Value);
}

// --- Lifecycle ---------------------------------------------------------------

UAC_InputMapper::UAC_InputMapper()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAC_InputMapper::BeginPlay()
{
    Super::BeginPlay();
    if (APawn* Pawn = GetOwnerPawn())
    {
        TrySetupInput();  // already possessed?
        Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UAC_InputMapper::HandleControllerChanged);
    }
}

void UAC_InputMapper::HandleControllerChanged(APawn*, AController*, AController*)
{
    TrySetupInput();
}

// --- Setup -------------------------------------------------------------------

void UAC_InputMapper::TrySetupInput()
{
    if (bBound) return;

    UEnhancedInputComponent* InputComponent = GetOwnerInputComponent();
    if (!InputComponent) return;

    AddMappingContext();
    BindActions(InputComponent);
    bBound = true;
}

void UAC_InputMapper::AddMappingContext()
{
    UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem();
    if (MappingContext && Subsystem)
        Subsystem->AddMappingContext(MappingContext, MappingPriority);
}

void UAC_InputMapper::BindActions(UEnhancedInputComponent* InputComponent)
{
    for (const FMappedInputBinding& Binding : Bindings)
        BindAction(InputComponent, Binding);
}

void UAC_InputMapper::BindAction(UEnhancedInputComponent* InputComponent, const FMappedInputBinding& Binding)
{
    if (!Binding.Action || Binding.FunctionName.IsNone()) return;

    const FName FunctionName = Binding.FunctionName;
    TWeakObjectPtr<UAC_InputMapper> WeakThis(this);
    InputComponent->BindActionValueLambda(Binding.Action, Binding.TriggerEvent,
        [WeakThis, FunctionName](const FInputActionValue& Value)
        {
            if (UAC_InputMapper* Self = WeakThis.Get())
                Self->DispatchInput(Value, FunctionName);
        });
}

// --- Dispatch ----------------------------------------------------------------

void UAC_InputMapper::DispatchInput(const FInputActionValue& Value, FName FunctionName)
{
    AActor* Owner = GetOwner();
    UFunction* Function = Owner ? Owner->FindFunction(FunctionName) : nullptr;
    if (Function)
        CallOwnerFunction(Function, Value);
}

void UAC_InputMapper::CallOwnerFunction(UFunction* Function, const FInputActionValue& Value)
{
    if (Function->NumParms == 0)
        CallParameterless(Function);
    else
        CallWithValue(Function, Value);
}

void UAC_InputMapper::CallParameterless(UFunction* Function)
{
    GetOwner()->ProcessEvent(Function, nullptr);
}

void UAC_InputMapper::CallWithValue(UFunction* Function, const FInputActionValue& Value)
{
    void* Frame = FMemory_Alloca(Function->ParmsSize);
    FMemory::Memzero(Frame, Function->ParmsSize);
    WriteActionValue(FindFirstInputParam(Function), Frame, Value);
    GetOwner()->ProcessEvent(Function, Frame);
}

// --- Owner lookups -----------------------------------------------------------

APawn* UAC_InputMapper::GetOwnerPawn() const
{
    return Cast<APawn>(GetOwner());
}

APlayerController* UAC_InputMapper::GetOwnerController() const
{
    APawn* Pawn = GetOwnerPawn();
    return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
}

UEnhancedInputComponent* UAC_InputMapper::GetOwnerInputComponent() const
{
    APawn* Pawn = GetOwnerPawn();
    if (!Pawn || !Pawn->IsPlayerControlled()) return nullptr;
    return Cast<UEnhancedInputComponent>(Pawn->InputComponent);
}

UEnhancedInputLocalPlayerSubsystem* UAC_InputMapper::GetInputSubsystem() const
{
    APlayerController* PC = GetOwnerController();
    ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
    return LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
}

const UClass* UAC_InputMapper::ResolveOwnerClass() const
{
    if (const AActor* Owner = GetOwner())
        return Owner->GetClass();
    return FindOwnerClassInOuterChain();  // edit time: component is a template
}

const UClass* UAC_InputMapper::FindOwnerClassInOuterChain() const
{
    for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
        if (const UClass* Found = OwnerClassFromObject(Outer))
            return Found;
    return nullptr;
}

TArray<FString> UAC_InputMapper::CollectOwnerFunctionNames() const
{
    TArray<FString> Names;
    const UClass* OwnerClass = ResolveOwnerClass();
    if (!OwnerClass) return Names;

    for (TFieldIterator<UFunction> It(OwnerClass); It; ++It)
        Names.Add(It->GetName());
    return Names;
}

TArray<FString> UAC_InputMapper::GetBindableFunctionNames() const
{
    TArray<FString> Names = CollectOwnerFunctionNames();
    Names.Sort();
    return Names;
}