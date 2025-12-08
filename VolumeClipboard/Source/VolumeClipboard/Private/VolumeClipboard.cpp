#include "VolumeClipboard.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "ToolMenus.h"
#include "GameFramework/Volume.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "EditorStyleSet.h" 
#include "BSPOps.h" 
#include "Model.h" 

static const FName VolumeClipboardTabName("VolumeClipboard");

#define LOCTEXT_NAMESPACE "FVolumeClipboardModule"

void FVolumeClipboardModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(VolumeClipboardTabName, FOnSpawnTab::CreateRaw(this, &FVolumeClipboardModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("VolumeClipboardTabTitle", "Volume Tools"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FVolumeClipboardModule::RegisterMenus));
}

void FVolumeClipboardModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VolumeClipboardTabName);
}

TSharedRef<SDockTab> FVolumeClipboardModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("Header", "Volume Copy/Paste Tools"))
						.Justification(ETextJustify::Center)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 5)
				[
					SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.ContentPadding(FMargin(10, 5))
						.Text(LOCTEXT("ExtractBtn", "Copy Selected Volumes"))
						.OnClicked(FOnClicked::CreateRaw(this, &FVolumeClipboardModule::OnExtractVolumesClicked))
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 5)
				[
					SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.ContentPadding(FMargin(10, 5))
						.Text(LOCTEXT("CreateBtn", "Paste Volumes (Exact)"))
						.OnClicked(FOnClicked::CreateRaw(this, &FVolumeClipboardModule::OnCreateVolumesClicked))
				]
		];
}

void FVolumeClipboardModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus) return;

	UToolMenu* ToolbarMenu = ToolMenus->ExtendMenu("LevelEditor.LevelEditorToolBar");
	if (!ToolbarMenu) return;

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenVolumeTool",
		FUIAction(FExecuteAction::CreateRaw(this, &FVolumeClipboardModule::OpenPluginWindow)),
		LOCTEXT("VolumeToolBtn", "Volume Tools"),
		LOCTEXT("VolumeToolTooltip", "Open Volume Copy/Paste Tools"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details") // Standard Built-in Icon
	));
}

void FVolumeClipboardModule::OpenPluginWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(VolumeClipboardTabName);
}

// ---------------------------------------------------------
// HELPER: Property Filtering
// ---------------------------------------------------------
bool IsPropertySafeToCopy(FProperty* Property, AActor* SourceActor)
{
	FString Name = Property->GetName();

	// STRICT BLACKLIST for Transforms
	if (Name.Contains("Guid") ||
		Name.Contains("Cookie") ||
		Name.StartsWith("bHidden") ||
		Name == "Brush" ||
		Name == "BrushComponent" ||
		Name == "RootComponent" ||
		Name == "Model" ||
		Name == "BrushBuilder" ||
		Name == "ActorLabel" ||
		Name == "Owner" ||
		Name == "Instigator" ||
		Name == "SavedSelections" ||
		Name == "RelativeLocation" ||
		Name == "RelativeRotation" ||
		Name == "RelativeScale3D" ||
		Name == "Rotation" ||
		Name == "Location" ||
		Name == "PhysicsTransform" ||
		Name == "ReplicatedMovement" ||
		Name == "SpriteScale" ||
		Name == "PivotOffset" ||
		Name == "PrePivot" ||
		Name == "Tags" ||
		Name == "Layers" ||
		Name == "InputPriority")
	{
		return false;
	}

	if (Property->IsA(FNumericProperty::StaticClass())) return true;
	if (Property->IsA(FBoolProperty::StaticClass())) return true;
	if (Property->IsA(FStrProperty::StaticClass())) return true;
	if (Property->IsA(FNameProperty::StaticClass())) return true;
	if (Property->IsA(FTextProperty::StaticClass())) return true;
	if (Property->IsA(FEnumProperty::StaticClass())) return true;
	if (Property->IsA(FStructProperty::StaticClass())) return true;
	if (Property->IsA(FArrayProperty::StaticClass())) return true;

	return false;
}

FString DoubleToPrecisionString(double Val)
{
	return FString::Printf(TEXT("%.17g"), Val);
}

// ---------------------------------------------------------
// LOGIC: Serialization / Extraction
// ---------------------------------------------------------

void FVolumeClipboardModule::SerializeActorProperties(AActor* Actor, TSharedPtr<FJsonObject>& OutJson)
{
	TSharedPtr<FJsonObject> PropsObject = MakeShareable(new FJsonObject);

	for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
		if (!IsPropertySafeToCopy(Property, Actor)) continue;

		FString StringValue;
		Property->ExportTextItem(StringValue, Property->ContainerPtrToValuePtr<void>(Actor), nullptr, Actor, PPF_None);

		if (!StringValue.IsEmpty() && StringValue != "None" && StringValue != "()")
		{
			PropsObject->SetStringField(Property->GetName(), StringValue);
		}
	}

	OutJson->SetObjectField("Properties", PropsObject);
}

FReply FVolumeClipboardModule::OnExtractVolumesClicked()
{
	TArray<TSharedPtr<FJsonValue>> VolumeArray;

	if (!GEditor) return FReply::Handled();

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		AVolume* Volume = Cast<AVolume>(Actor);

		if (Volume)
		{
			TSharedPtr<FJsonObject> VolObj = MakeShareable(new FJsonObject);

			VolObj->SetStringField("Class", Volume->GetClass()->GetPathName());
			VolObj->SetStringField("InternalName", Volume->GetName());

			// *** TRANSFORM EXTRACTION ***
			FVector CenterLocation = Volume->GetActorLocation();
			FVector BrushExtent = FVector(200, 200, 200);

			if (Volume->GetBrushComponent())
			{
				FBoxSphereBounds LocalBounds = Volume->GetBrushComponent()->CalcBounds(FTransform::Identity);
				BrushExtent = LocalBounds.BoxExtent;

				FBoxSphereBounds WorldBounds = Volume->GetBrushComponent()->CalcBounds(Volume->GetActorTransform());
				CenterLocation = WorldBounds.Origin;
			}

			VolObj->SetStringField("LocX", DoubleToPrecisionString(CenterLocation.X));
			VolObj->SetStringField("LocY", DoubleToPrecisionString(CenterLocation.Y));
			VolObj->SetStringField("LocZ", DoubleToPrecisionString(CenterLocation.Z));

			FQuat Quat = Volume->GetActorQuat();
			VolObj->SetStringField("QuatX", DoubleToPrecisionString(Quat.X));
			VolObj->SetStringField("QuatY", DoubleToPrecisionString(Quat.Y));
			VolObj->SetStringField("QuatZ", DoubleToPrecisionString(Quat.Z));
			VolObj->SetStringField("QuatW", DoubleToPrecisionString(Quat.W));

			FVector Scale = Volume->GetActorScale3D();
			VolObj->SetStringField("SclX", DoubleToPrecisionString(Scale.X));
			VolObj->SetStringField("SclY", DoubleToPrecisionString(Scale.Y));
			VolObj->SetStringField("SclZ", DoubleToPrecisionString(Scale.Z));

			VolObj->SetStringField("BrushX", DoubleToPrecisionString(BrushExtent.X));
			VolObj->SetStringField("BrushY", DoubleToPrecisionString(BrushExtent.Y));
			VolObj->SetStringField("BrushZ", DoubleToPrecisionString(BrushExtent.Z));

			SerializeActorProperties(Volume, VolObj);

			VolumeArray.Add(MakeShareable(new FJsonValueObject(VolObj)));
		}
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(VolumeArray, Writer);

	FPlatformApplicationMisc::ClipboardCopy(*OutputString);

	return FReply::Handled();
}

// ---------------------------------------------------------
// LOGIC: Deserialization / Creation
// ---------------------------------------------------------

void FVolumeClipboardModule::RestoreActorProperties(AActor* Actor, TSharedPtr<FJsonObject> InJson)
{
	const TSharedPtr<FJsonObject>* PropsObject;
	if (InJson->TryGetObjectField("Properties", PropsObject))
	{
		for (auto& Pair : (*PropsObject)->Values)
		{
			FString PropName = Pair.Key;
			FString PropValString = Pair.Value->AsString();

			FProperty* Property = Actor->GetClass()->FindPropertyByName(*PropName);
			if (Property && IsPropertySafeToCopy(Property, Actor))
			{
				Property->ImportText(*PropValString, Property->ContainerPtrToValuePtr<void>(Actor), 0, Actor);
			}
		}
	}
}

FReply FVolumeClipboardModule::OnCreateVolumesClicked()
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	if (ClipboardContent.IsEmpty()) return FReply::Handled();

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardContent);

	if (FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		GEditor->BeginTransaction(LOCTEXT("PasteVolumes", "Paste Volumes"));

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World) return FReply::Handled();

		GEditor->SelectNone(true, true);

		for (TSharedPtr<FJsonValue> Val : JsonArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FString ClassPath = Obj->GetStringField("Class");
			UClass* ActorClass = LoadObject<UClass>(nullptr, *ClassPath);

			if (ActorClass && ActorClass->IsChildOf(AVolume::StaticClass()))
			{
				FString InternalName = Obj->GetStringField("InternalName");

				FVector Location;
				Location.X = FCString::Atod(*Obj->GetStringField("LocX"));
				Location.Y = FCString::Atod(*Obj->GetStringField("LocY"));
				Location.Z = FCString::Atod(*Obj->GetStringField("LocZ"));

				FQuat Quat = FQuat::Identity;
				if (Obj->HasField("QuatW"))
				{
					Quat.X = FCString::Atod(*Obj->GetStringField("QuatX"));
					Quat.Y = FCString::Atod(*Obj->GetStringField("QuatY"));
					Quat.Z = FCString::Atod(*Obj->GetStringField("QuatZ"));
					Quat.W = FCString::Atod(*Obj->GetStringField("QuatW"));
					// No normalization for exact bit copy
				}

				FVector Scale;
				Scale.X = FCString::Atod(*Obj->GetStringField("SclX"));
				Scale.Y = FCString::Atod(*Obj->GetStringField("SclY"));
				Scale.Z = FCString::Atod(*Obj->GetStringField("SclZ"));

				FVector BrushExtent = FVector(100, 100, 100);
				if (Obj->HasField("BrushX"))
				{
					BrushExtent.X = FCString::Atod(*Obj->GetStringField("BrushX"));
					BrushExtent.Y = FCString::Atod(*Obj->GetStringField("BrushY"));
					BrushExtent.Z = FCString::Atod(*Obj->GetStringField("BrushZ"));
				}

				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				SpawnParams.bNoFail = true;

				AVolume* NewVolume = World->SpawnActor<AVolume>(ActorClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

				if (NewVolume)
				{
					NewVolume->PreEditChange(nullptr);

					if (!InternalName.IsEmpty()) NewVolume->SetActorLabel(InternalName);

					UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(NewVolume, UCubeBuilder::StaticClass(), NAME_None, RF_Transactional | RF_Public);

					CubeBuilder->X = BrushExtent.X * 2.0f;
					CubeBuilder->Y = BrushExtent.Y * 2.0f;
					CubeBuilder->Z = BrushExtent.Z * 2.0f;

					NewVolume->BrushBuilder = CubeBuilder;

					if (NewVolume->Brush == nullptr)
					{
						NewVolume->Brush = NewObject<UModel>(NewVolume, NAME_None, RF_Transactional);
						NewVolume->Brush->Initialize(nullptr, true);
						if (NewVolume->GetBrushComponent())
						{
							NewVolume->GetBrushComponent()->Brush = NewVolume->Brush;
						}
					}

					CubeBuilder->Build(World, NewVolume);
					FBSPOps::csgPrepMovingBrush(NewVolume);

					RestoreActorProperties(NewVolume, Obj);

					NewVolume->PostEditChange();

					// FORCE EXACT TRANSFORM
					FTransform FinalTransform;
					FinalTransform.SetLocation(Location);
					FinalTransform.SetRotation(Quat);
					FinalTransform.SetScale3D(Scale);

					if (USceneComponent* RootComp = NewVolume->GetRootComponent())
					{
						RootComp->SetRelativeTransform(FinalTransform, false, nullptr, ETeleportType::TeleportPhysics);
						RootComp->UpdateBounds();
					}
					else
					{
						NewVolume->SetActorTransform(FinalTransform, false, nullptr, ETeleportType::TeleportPhysics);
					}

					GEditor->SelectActor(NewVolume, true, false);
				}
			}
		}

		GEditor->RebuildAlteredBSP();
		GEditor->EndTransaction();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVolumeClipboardModule, VolumeClipboard)