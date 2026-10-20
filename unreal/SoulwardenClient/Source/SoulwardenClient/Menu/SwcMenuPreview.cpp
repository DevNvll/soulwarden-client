#include "Menu/SwcMenuPreview.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Environment/SwcEnvironmentSubsystem.h"
#include "Environment/SwcLocalLightRules.h"
#include "Menu/SwcMenuLights.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
/** The camera of the character view relative to the feet of the preview (game space m, the space map: x, -y, z; cm). */
const FVector CameraOffset(111.81641, 190.84472, 107.78198);
/** Pitch -3.5 and yaw 31.5 degrees of the game: the forward Rz(31.5) Rx(-3.5) (0, 1, 0) in Unreal space. */
constexpr double CameraPitch = -3.5;
constexpr double CameraYaw = -121.5;
/** The body yaw -147.88 degrees of the game: the Unreal yaw of the mesh is its negative (the native mesh faces game +Y). */
constexpr double BodyYaw = 147.8817902;
/** The vertical FOV of MainCamera (degrees). */
constexpr double VerticalFov = 55.0;
/** The lights of the scene: their candela are physical units for the environment of the client (its menu mode, SetEnvironment). With
 *  the environment of the level (its engine sun and its fixed exposure) they burned the bodies white (the run f4-scene2). */
TAutoConsoleVariable<int32> CVarMenuLights(TEXT("swc.MenuLights"), 1,
	TEXT("The lights of the menu scene (stage F4): 1 on (the default), 0 off (a compare; -SwcNoMenuLights at the start)."));
} // namespace

ASwcMenuPreview::ASwcMenuPreview()
{
	// The actor ticks only while a camera sequence plays.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Leader = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Leader"));
	Leader->SetupAttachment(Root);
	Leader->SetRelativeRotation(FRotator(0.0, BodyYaw, 0.0));
	// The leader is not drawn, but its pose is computed each frame for the parts (as the world character).
	Leader->SetVisibility(false);
	Leader->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocationAndRotation(CameraOffset, FRotator(CameraPitch, CameraYaw, 0.0));
	Camera->SetFieldOfView(85.5655901f);
	Camera->bConstrainAspectRatio = false;
}

namespace
{
/** The space map of stage 1.1: game (x, y, z) m -> Unreal (x, -y, z) cm. */
FVector ToUnreal(const FVector& Game)
{
	return FVector(Game.X * 100.0, -Game.Y * 100.0, Game.Z * 100.0);
}

FVector ToUnrealAxis(const FVector& Game)
{
	return FVector(Game.X, -Game.Y, Game.Z);
}

/** @return The 12 numbers of a transform of the scene record: the three axis columns, then the translation. */
TArray<double> Numbers(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	TArray<double> Out;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			Out.Add(Value->AsNumber());
		}
	}
	return Out;
}

/** @return The Unreal rotation of a game rotation by its axis columns: `Forward` is the axis that points forward (the Y axis of
 *  a camera of the game; the X axis of a body, whose mesh has the axes of the game), `Up` the Z axis. */
FRotator RotationOf(const FVector& Forward, const FVector& Up)
{
	return FRotationMatrix::MakeFromXZ(ToUnrealAxis(Forward), ToUnrealAxis(Up)).Rotator();
}
} // namespace

bool ASwcMenuPreview::LoadScene(const FString& Path)
{
	SceneContent = FPaths::GetPath(FPaths::GetPath(Path));   // CONTENT/frontend/nighthaven.json
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("menu: no scene record %s (tools/convert-frontend.sh scene): the fixed character view"), *Path);
		bScenePlaced = true;
		return false;
	}
	const TSharedPtr<FJsonObject>* Entities = nullptr;
	Doc->TryGetObjectField(TEXT("entities"), Entities);
	const auto Entity = [Entities](const TCHAR* Name)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Entities != nullptr && (*Entities)->TryGetObjectField(Name, Out) ? *Out : TSharedPtr<FJsonObject>();
	};
	const TArray<double> Player = Numbers(Entity(TEXT("Player Entity")), TEXT("world"));
	const TArray<double> Cam = Numbers(Entity(TEXT("MainCamera")), TEXT("world"));
	const TArray<double> Parent = Numbers(Entity(TEXT("Main_layer")), TEXT("world"));
	if (Player.Num() != 12 || Cam.Num() != 12)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("menu: the scene record has no Player Entity or MainCamera"));
		bScenePlaced = true;
		return false;
	}
	// The actor stands at the feet of the Player Entity; the body has its rotation (with its small tilt).
	PlayerWorld = FVector(Player[9], Player[10], Player[11]);
	SetActorLocation(ToUnreal(PlayerWorld));
	Leader->SetWorldRotation(RotationOf(FVector(Player[0], Player[1], Player[2]), FVector(Player[6], Player[7], Player[8])));
	CameraParent = Parent.Num() == 12 ? FVector(Parent[9], Parent[10], Parent[11]) : FVector::ZeroVector;
	// The camera at the start: the world transform of MainCamera (its Y axis is the view direction).
	Camera->SetWorldLocation(ToUnreal(FVector(Cam[9], Cam[10], Cam[11])));
	Camera->SetWorldRotation(RotationOf(FVector(Cam[3], Cam[4], Cam[5]), FVector(Cam[6], Cam[7], Cam[8])));
	const TSharedPtr<FJsonObject>* List = nullptr;
	if (Doc->TryGetObjectField(TEXT("sequences"), List))
	{
		static const TCHAR* const TrackNames[] = {TEXT("PX"), TEXT("PY"), TEXT("PZ"), TEXT("RX"), TEXT("RY"), TEXT("RZ")};
		for (const auto& Item : (*List)->Values)
		{
			const TSharedPtr<FJsonObject> Record = Item.Value->AsObject();
			const TSharedPtr<FJsonObject>* Nodes = nullptr;
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (!Record.IsValid() || !Record->TryGetObjectField(TEXT("nodes"), Nodes) || !(*Nodes)->TryGetObjectField(TEXT("MainCamera"), Node))
			{
				continue;
			}
			FSequence Out;
			Out.End = static_cast<float>(Record->GetNumberField(TEXT("end")));
			for (int32 Track = 0; Track < 6; ++Track)
			{
				const TSharedPtr<FJsonObject>* Data = nullptr;
				if (!(*Node)->TryGetObjectField(TrackNames[Track], Data))
				{
					continue;
				}
				swc::menu::FCurve& Curve = Out.Tracks[Track];
				Curve.Default = static_cast<float>((*Data)->GetNumberField(TEXT("default")));
				for (const TSharedPtr<FJsonValue>& KeyValue : (*Data)->GetArrayField(TEXT("keys")))
				{
					const TSharedPtr<FJsonObject> Key = KeyValue->AsObject();
					const auto Pair = [&Key](const TCHAR* Field, float* Out2)
					{
						const TArray<TSharedPtr<FJsonValue>> Values = Key->GetArrayField(Field);
						Out2[0] = Values.Num() > 0 ? static_cast<float>(Values[0]->AsNumber()) : 0.0f;
						Out2[1] = Values.Num() > 1 ? static_cast<float>(Values[1]->AsNumber()) : 0.0f;
					};
					swc::menu::FCurveKey Parsed;
					Parsed.Time = static_cast<float>(Key->GetNumberField(TEXT("time")));
					Parsed.Flags = static_cast<int>(Key->GetNumberField(TEXT("flags")));
					Pair(TEXT("value"), Parsed.Value);
					Pair(TEXT("ds"), Parsed.Ds);
					Pair(TEXT("dd"), Parsed.Dd);
					Curve.Keys.push_back(Parsed);
				}
				swc::menu::UpdateTangents(Curve.Keys);
			}
			Sequences.Add(FString(Item.Key.ToView()), MoveTemp(Out));
		}
	}
	UE_LOG(LogSwcBackend, Log, TEXT("menu: the scene record: the Player Entity at %.2f %.2f %.2f m, %d camera sequence(s)"), PlayerWorld.X,
		PlayerWorld.Y, PlayerWorld.Z, Sequences.Num());
	SetEnvironment(Doc);
	BuildScene(Doc);
	return true;
}

void ASwcMenuPreview::SetEnvironment(const TSharedPtr<FJsonObject>& Doc)
{
	const TSharedPtr<FJsonObject>* Record = nullptr;
	USwcEnvironmentSubsystem* Environment = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcEnvironmentSubsystem>() : nullptr;
	if (Environment == nullptr || !Doc->TryGetObjectField(TEXT("environment"), Record))
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("menu: no environment in the scene record (tools/convert-frontend.sh scene): the environment of the level"));
		return;
	}
	const TSharedPtr<FJsonObject>& Values = *Record;
	USwcEnvironmentSubsystem::FMenuScene Scene;
	Scene.Preset = Values->GetStringField(TEXT("preset"));
	Scene.Sky = Values->GetStringField(TEXT("sky"));
	Values->TryGetNumberField(TEXT("hour"), Scene.Hour);
	Values->TryGetNumberField(TEXT("sunRotation"), Scene.SunRotation);
	Values->TryGetNumberField(TEXT("longitude"), Scene.Longitude);
	Values->TryGetNumberField(TEXT("moonLatitude"), Scene.MoonLatitude);
	Values->TryGetNumberField(TEXT("moonLongitude"), Scene.MoonLongitude);
	Values->TryGetNumberField(TEXT("moonSize"), Scene.MoonSize);
	// The environment probes of the scene (stage F4b): the record form of the local probes of probes.json, with their cube pictures.
	const TArray<TSharedPtr<FJsonValue>>* Probes = nullptr;
	const TSharedPtr<FJsonObject>* Cubes = nullptr;
	// -SwcNoMenuProbes: no menu probes (the world probe as before; a compare of the colors).
	if (!FParse::Param(FCommandLine::Get(), TEXT("SwcNoMenuProbes")) && Values->TryGetArrayField(TEXT("probes"), Probes)
		&& Values->TryGetObjectField(TEXT("cubes"), Cubes))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Probes)
		{
			const TSharedPtr<FJsonObject> Probe = Value->AsObject();
			const TSharedPtr<FJsonObject>* Cube = nullptr;
			FString Cubemap, Picture;
			USwcEnvironmentSubsystem::FData::FLocalProbe Item;
			if (Probe.IsValid() && Probe->TryGetStringField(TEXT("cubemap"), Cubemap) && (*Cubes)->TryGetObjectField(Cubemap, Cube)
				&& (*Cube)->TryGetStringField(TEXT("hdr"), Picture) && USwcEnvironmentSubsystem::ReadLocalProbe(Probe, Picture, Item))
			{
				Scene.Probes.Add(MoveTemp(Item));
			}
		}
	}
	Environment->SetMenuScene(Scene);
}

void ASwcMenuPreview::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USwcEnvironmentSubsystem* Environment = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcEnvironmentSubsystem>() : nullptr)
	{
		Environment->ClearMenuScene();
	}
	Super::EndPlay(Reason);
}

void ASwcMenuPreview::BuildScene(const TSharedPtr<FJsonObject>& Doc)
{
	// The meshes: the converted static meshes of the world content (the object path of the imported asset; an older record has
	// only the manifest folder and the stem).
	struct FPlacement
	{
		FSoftObjectPath Mesh;
		FTransform Transform;
		/** The material overrides (docs/WORLD.md "Material overrides of the placements"): slot name -> the material instance of
		 *  its key (/SwcGameContent/Materials/KEY.KEY, as the cells of the world). */
		TArray<TPair<FName, FSoftObjectPath>> Slots;
	};
	TArray<FPlacement> Placements;
	TArray<FSoftObjectPath> Paths;
	int32 NotConverted = 0;
	const TArray<TSharedPtr<FJsonValue>>* Meshes = nullptr;
	if (Doc->TryGetArrayField(TEXT("meshes"), Meshes))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Meshes)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			const FString Folder = Item->GetStringField(TEXT("unreal"));
			const FString Stem = Item->GetStringField(TEXT("stem"));
			if (Folder.IsEmpty() || Stem.IsEmpty())
			{
				++NotConverted;
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& L = Item->GetArrayField(TEXT("location"));
			const TArray<TSharedPtr<FJsonValue>>& R = Item->GetArrayField(TEXT("rotation"));
			const TArray<TSharedPtr<FJsonValue>>& S = Item->GetArrayField(TEXT("scale"));
			FPlacement Placement;
			const FString Asset = Item->GetStringField(TEXT("asset"));
			Placement.Mesh = FSoftObjectPath(Asset.IsEmpty() ? FString::Printf(TEXT("%s/%s/StaticMeshes/%s.%s"), *Folder, *Stem, *Stem, *Stem) : Asset);
			Placement.Transform = FTransform(FQuat(R[0]->AsNumber(), R[1]->AsNumber(), R[2]->AsNumber(), R[3]->AsNumber()),
				FVector(L[0]->AsNumber(), L[1]->AsNumber(), L[2]->AsNumber()), FVector(S[0]->AsNumber(), S[1]->AsNumber(), S[2]->AsNumber()));
			Paths.AddUnique(Placement.Mesh);
			const TSharedPtr<FJsonObject>* Slots = nullptr;
			if (Item->TryGetObjectField(TEXT("slots"), Slots))
			{
				for (const auto& Slot : (*Slots)->Values)
				{
					const FString Key = Slot.Value->AsString();
					const FSoftObjectPath Material(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Key, *Key));
					Placement.Slots.Emplace(FName(*Slot.Key), Material);
					Paths.AddUnique(Material);
				}
			}
			Placements.Add(MoveTemp(Placement));
		}
	}
	// The projected decals of the scene (stage F4b: the dirt patches of the camp), in the form of the world surface layer
	// (decals.decal_transforms: the dp_ instance, the box, the sort priority, the color and the opacity).
	struct FDecalPlacement
	{
		FSoftObjectPath Material;
		FTransform Transform;
		FVector Size;
		int32 SortOrder = 0;
		FLinearColor Color = FLinearColor::White;
	};
	TArray<FDecalPlacement> Decals;
	const TArray<TSharedPtr<FJsonValue>>* DecalRecords = nullptr;
	if (Doc->TryGetArrayField(TEXT("decals"), DecalRecords))
	{
		for (const TSharedPtr<FJsonValue>& Value : *DecalRecords)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			const FString Key = Item->GetStringField(TEXT("material"));
			const TArray<TSharedPtr<FJsonValue>>& L = Item->GetArrayField(TEXT("location"));
			const TArray<TSharedPtr<FJsonValue>>& R = Item->GetArrayField(TEXT("rotation"));
			const TArray<TSharedPtr<FJsonValue>>& S = Item->GetArrayField(TEXT("size"));
			FDecalPlacement Decal;
			Decal.Material = FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Key, *Key));
			Decal.Transform = FTransform(FQuat(R[0]->AsNumber(), R[1]->AsNumber(), R[2]->AsNumber(), R[3]->AsNumber()),
				FVector(L[0]->AsNumber(), L[1]->AsNumber(), L[2]->AsNumber()));
			Decal.Size = FVector(S[0]->AsNumber(), S[1]->AsNumber(), S[2]->AsNumber());
			Decal.SortOrder = static_cast<int32>(Item->GetNumberField(TEXT("sort priority")));
			const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
			if (Item->TryGetArrayField(TEXT("color"), Color) && Color->Num() >= 4)
			{
				Decal.Color = FLinearColor((*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber(), (*Color)[3]->AsNumber());
			}
			double Opacity = 1.0;
			if (Item->TryGetNumberField(TEXT("opacity"), Opacity))
			{
				Decal.Color.A *= static_cast<float>(Opacity);
			}
			Paths.AddUnique(Decal.Material);
			Decals.Add(MoveTemp(Decal));
		}
	}
	TWeakObjectPtr<ASwcMenuPreview> Self(this);
	const auto Place = [Self, Placements, Decals, NotConverted]()
	{
		ASwcMenuPreview* Preview = Self.Get();
		if (Preview == nullptr)
		{
			return;
		}
		int32 Shown = 0;
		int32 Missing = 0;
		int32 SlotsSet = 0;
		int32 SlotsMissing = 0;
		for (const FPlacement& Placement : Placements)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Placement.Mesh.ResolveObject());
			if (Mesh == nullptr)
			{
				++Missing;
				continue;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Preview);
			// Movable as the root of the actor: a static part cannot attach to it, and its world place would be lost.
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetupAttachment(Preview->GetRootComponent());
			Component->SetStaticMesh(Mesh);
			for (const TPair<FName, FSoftObjectPath>& Slot : Placement.Slots)
			{
				UMaterialInterface* Material = Cast<UMaterialInterface>(Slot.Value.ResolveObject());
				const int32 Index = Mesh->GetMaterialIndex(Slot.Key);
				if (Material != nullptr && Index != INDEX_NONE)
				{
					Component->SetMaterial(Index, Material);
					++SlotsSet;
				}
				else
				{
					++SlotsMissing;
				}
			}
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetWorldTransform(Placement.Transform);
			Component->RegisterComponent();
			Preview->SceneParts.Add(Component);
			++Shown;
		}
		int32 DecalsShown = 0;
		for (const FDecalPlacement& Decal : Decals)
		{
			UMaterialInterface* Material = Cast<UMaterialInterface>(Decal.Material.ResolveObject());
			if (Material == nullptr)
			{
				continue;
			}
			UDecalComponent* Component = NewObject<UDecalComponent>(Preview);
			Component->SetupAttachment(Preview->GetRootComponent());
			Component->SetDecalMaterial(Material);
			Component->DecalSize = Decal.Size;
			Component->SortOrder = Decal.SortOrder;
			Component->SetDecalColor(Decal.Color);
			Component->SetWorldTransform(Decal.Transform);
			Component->RegisterComponent();
			Preview->SceneParts.Add(Component);
			++DecalsShown;
		}
		UE_LOG(LogSwcBackend, Log, TEXT("menu: the scene meshes: %d shown, %d not imported, %d not converted; the material overrides: %d slot(s) set, %d not imported; %d of %d decal(s)"),
			Shown, Missing, NotConverted, SlotsSet, SlotsMissing, DecalsShown, Decals.Num());
		Preview->bScenePlaced = true;
	};
	SceneLoading = Paths.Num() > 0 ? UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, Place) : nullptr;
	if (!SceneLoading.IsValid())
	{
		Place();
	}
	const TSharedPtr<FJsonObject>* Ground = nullptr;
	if (Doc->TryGetObjectField(TEXT("ground"), Ground))
	{
		BuildGround(*Ground);
	}
	const TArray<TSharedPtr<FJsonValue>>* Lights = nullptr;
	if (Doc->TryGetArrayField(TEXT("lights"), Lights))
	{
		BuildLights(*Lights);
	}
}

void ASwcMenuPreview::BuildGround(const TSharedPtr<FJsonObject>& Ground)
{
	// The heights of the menu world about the Player Entity (1 m apart, row 0 = the smallest y; 301 x 301 m), as one static mesh
	// with the albedo picture of the terrain composite of the square (stage F4b; row 0 = north; else the macro color picture),
	// loaded at run time, on M_SwcSurface; with no picture, the mean color of the macro map.
	const int32 Size = static_cast<int32>(Ground->GetNumberField(TEXT("size")));
	const TArray<TSharedPtr<FJsonValue>>& Heights = Ground->GetArrayField(TEXT("heights"));
	const TArray<TSharedPtr<FJsonValue>>& Origin = Ground->GetArrayField(TEXT("origin"));
	const TArray<TSharedPtr<FJsonValue>>& Color = Ground->GetArrayField(TEXT("color"));
	const double Step = Ground->GetNumberField(TEXT("step"));
	if (Size < 2 || Heights.Num() != Size * Size || Origin.Num() != 2 || Color.Num() != 3)
	{
		return;
	}
	const FVector Center = ToUnreal(PlayerWorld);
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Ground");
	const auto Height = [&Heights, Size](int32 I, int32 J)
	{
		const double H = Heights[FMath::Clamp(J, 0, Size - 1) * Size + FMath::Clamp(I, 0, Size - 1)]->AsNumber();
		return FMath::IsNaN(H) ? 0.0 : H;
	};
	TArray<FVertexInstanceID> Instances;
	Instances.SetNum(Size * Size);
	for (int32 J = 0; J < Size; ++J)
	{
		for (int32 I = 0; I < Size; ++I)
		{
			const FVector Game(Origin[0]->AsNumber() + I * Step, Origin[1]->AsNumber() + J * Step, Height(I, J));
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = FVector3f(ToUnreal(Game) - Center);
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			// The normal of the height field in game space (dh/dx, dh/dy), then the space map.
			const FVector GameNormal = FVector(-(Height(I + 1, J) - Height(I - 1, J)) / (2.0 * Step),
				-(Height(I, J + 1) - Height(I, J - 1)) / (2.0 * Step), 1.0).GetSafeNormal();
			Normals[Instance] = FVector3f(ToUnrealAxis(GameNormal));
			UVs.Set(Instance, 0, FVector2f(static_cast<float>(I) / (Size - 1), 1.0f - static_cast<float>(J) / (Size - 1)));
			Instances[J * Size + I] = Instance;
		}
	}
	for (int32 J = 0; J + 1 < Size; ++J)
	{
		for (int32 I = 0; I + 1 < Size; ++I)
		{
			const FVertexInstanceID A = Instances[J * Size + I], B = Instances[J * Size + I + 1];
			const FVertexInstanceID C = Instances[(J + 1) * Size + I], D = Instances[(J + 1) * Size + I + 1];
			// The order of an upward face in Unreal space (the mirror of y). The order A, C, B was culled from above: the ground did not
			// show in any run of F4 (the body stood over the fog, run f4b-g2).
			Description.CreateTriangle(Group, {A, B, C});
			Description.CreateTriangle(Group, {B, D, C});
		}
	}
	UMaterialInstanceDynamic* Material = nullptr;
	FString Picture;
	if (Ground->TryGetStringField(TEXT("picture"), Picture))
	{
		UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(FPaths::Combine(SceneContent, Picture));
		UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, TEXT("/SwcGameContent/Base/M_SwcSurface.M_SwcSurface"), nullptr, LOAD_NoWarn);
		if (Texture != nullptr && Surface != nullptr)
		{
			// The parameters of the Illum parent (docs/MATERIALS.md M2): the picture as the diffuse map, a dielectric F0 of 0.04, and a
			// rough surface (a choice of this client: the macro map has no specular or gloss map).
			Material = UMaterialInstanceDynamic::Create(Surface, this);
			Material->SetTextureParameterValue(TEXT("BaseColor"), Texture);
			Material->SetVectorParameterValue(TEXT("DiffuseColor"), FLinearColor::White);
			Material->SetVectorParameterValue(TEXT("SpecularColor"), FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
			Material->SetScalarParameterValue(TEXT("Smoothness"), 0.2f);
		}
		UE_LOG(LogSwcBackend, Log, TEXT("menu: the ground: 301 x 301 m, the picture %s %s"), *Picture, Material != nullptr ? TEXT("on M_SwcSurface") : TEXT("did not load (one color)"));
	}
	UMaterialInterface* Base = Material == nullptr ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")) : nullptr;
	if (Base != nullptr)
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(Color[0]->AsNumber() * 255.0), static_cast<uint8>(Color[1]->AsNumber() * 255.0), static_cast<uint8>(Color[2]->AsNumber() * 255.0))));
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(this);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Ground")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions({&Description}, Params);
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->SetMobility(EComponentMobility::Movable);   // a static mesh component is static by default (see BuildScene)
	Component->SetupAttachment(Root);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetWorldLocation(Center);
	Component->RegisterComponent();
	SceneParts.Add(Component);
}

void ASwcMenuPreview::BuildLights(const TArray<TSharedPtr<FJsonValue>>& Lights)
{
	namespace env = swc::environment;
	if (FParse::Param(FCommandLine::Get(), TEXT("SwcNoMenuLights")))
	{
		CVarMenuLights->Set(0, ECVF_SetByCommandline);
	}
	const bool bLightsOn = CVarMenuLights.GetValueOnGameThread() != 0;
	int32 Made = 0;
	for (const TSharedPtr<FJsonValue>& Value : Lights)
	{
		const TSharedPtr<FJsonObject> Item = Value->AsObject();
		const auto Text = [&Item](const TCHAR* Field) { return Item->HasField(Field) ? Item->GetStringField(Field) : FString(); };
		const auto Number = [&Text](const TCHAR* Field, double Default) { const FString V = Text(Field); return V.IsEmpty() ? Default : FCString::Atod(*V); };
		const int32 Type = static_cast<int32>(Number(TEXT("LightType"), 0.0));
		// The point (0) and projector (2) lights; a probe (3) and an area light (1) are not in this subset.
		if ((Type != 0 && Type != 2) || Text(TEXT("Visible")) == TEXT("false") || Text(TEXT("OnInitially")) == TEXT("false"))
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>& W = Item->GetArrayField(TEXT("world"));
		if (W.Num() != 12)
		{
			continue;
		}
		env::LocalLight Light;
		Light.type = Type == 2 ? env::LocalLight::Type::spot : env::LocalLight::Type::point;
		Light.p = env::Vec3{W[9]->AsNumber(), W[10]->AsNumber(), W[11]->AsNumber()};
		Light.x = env::Vec3{W[0]->AsNumber(), W[1]->AsNumber(), W[2]->AsNumber()};
		Light.z = env::Vec3{W[6]->AsNumber(), W[7]->AsNumber(), W[8]->AsNumber()};
		TArray<FString> Rgb;
		Text(TEXT("Color")).ParseIntoArrayWS(Rgb);
		for (int32 C = 0; C < 3 && C < Rgb.Num(); ++C)
		{
			Light.color[C] = FCString::Atod(*Rgb[C]);
		}
		Light.diffuse = Number(TEXT("DiffuseMultiplier"), 1.0);
		Light.spec = Number(TEXT("SpecMultiplier"), 1.0);
		Light.range = Type == 2 ? Number(TEXT("ProjectorDistance"), 1.0) : Number(TEXT("PointMaxDistance"), 1.0);
		Light.bulb = Type == 2 ? Number(TEXT("ProjectorAttenuationBulbSize"), 0.05) : Number(TEXT("PointAttenuationBulbSize"), 0.05);
		Light.fov = Number(TEXT("ProjectorFOV"), 90.0);
		UPointLightComponent* Point = nullptr;
		if (Light.type == env::LocalLight::Type::spot)
		{
			USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
			Spot->SetOuterConeAngle(static_cast<float>(FMath::Clamp(Light.fov * 0.5, 1.0, 80.0)));   // ProjectorFOV is the full angle
			Spot->SetInnerConeAngle(0.0f);
			Point = Spot;
		}
		else
		{
			Point = NewObject<UPointLightComponent>(this);
		}
		// The candela of the source attenuation at the reference distance (Menu/SwcMenuLights.h: the body, 1 m above the Player
		// Entity, when the light reaches it); a spot points along the +X axis of its entity (the world rule).
		const double Body = FVector::Dist(FVector(Light.p.x, Light.p.y, Light.p.z), PlayerWorld + FVector(0.0, 0.0, 1.0));
		const double Reference = swc::menu::LightReference(Body, Light.range);
		Point->SetupAttachment(Root);
		Point->SetMobility(EComponentMobility::Movable);
		Point->SetSourceRadius(static_cast<float>(swc::menu::LightSourceRadius(Light.bulb, Reference) * 100.0));
		Point->SetAttenuationRadius(static_cast<float>(Light.range * 100.0));
		Point->SetIntensityUnits(ELightUnits::Candelas);
		Point->SetIntensity(static_cast<float>(swc::menu::LightCandela(Light.diffuse, Light.bulb, Reference)));
		Point->SetLightColor(FLinearColor(static_cast<float>(Light.color[0]), static_cast<float>(Light.color[1]), static_cast<float>(Light.color[2])));
		Point->SetCastShadows(false);
		Point->SetVolumetricScatteringIntensity(Text(TEXT("VolumetricFog")) == TEXT("true") ? 1.0f : 0.0f);   // the world rule
		Point->SetVisibility(bLightsOn);
		Point->SetWorldLocationAndRotation(ToUnreal(FVector(Light.p.x, Light.p.y, Light.p.z)),
			RotationOf(FVector(Light.x.x, Light.x.y, Light.x.z), FVector(Light.z.x, Light.z.y, Light.z.z)));
		Point->RegisterComponent();
		SceneParts.Add(Point);
		++Made;
	}
	UE_LOG(LogSwcBackend, Log, TEXT("menu: the scene lights: %d of %d records (point and projector lights), %s"), Made, Lights.Num(),
		bLightsOn ? TEXT("on") : TEXT("off (swc.MenuLights 0)"));
}

void ASwcMenuPreview::PlaceCamera(const FVector& GameWorld, const FVector& EulerDegrees)
{
	// The rotation of the game: Rz(z) Ry(y) Rx(x) (Ang3 of the scene in degrees). Its Y column is the view direction of the
	// camera, its Z column the up direction.
	const double X = FMath::DegreesToRadians(EulerDegrees.X);
	const double Y = FMath::DegreesToRadians(EulerDegrees.Y);
	const double Z = FMath::DegreesToRadians(EulerDegrees.Z);
	const double Cx = FMath::Cos(X), Sx = FMath::Sin(X), Cy = FMath::Cos(Y), Sy = FMath::Sin(Y), Cz = FMath::Cos(Z), Sz = FMath::Sin(Z);
	// The columns 1 and 2 of Rz Ry Rx.
	const FVector Forward(Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx);
	const FVector Up(Cz * Sy * Cx + Sz * Sx, Sz * Sy * Cx - Cz * Sx, Cy * Cx);
	Camera->SetWorldLocation(ToUnreal(GameWorld));
	Camera->SetWorldRotation(RotationOf(Forward, Up));
}

bool ASwcMenuPreview::PlaySequence(const FString& Name)
{
	if (!Sequences.Contains(Name))
	{
		return false;
	}
	Playing = Name;
	PlayTime = 0.0f;
	SetActorTickEnabled(true);
	Tick(0.0f);
	return true;
}

void ASwcMenuPreview::StopSequence(const FString& Name)
{
	if (Playing == Name)
	{
		Playing.Reset();
		SetActorTickEnabled(false);
	}
}

void ASwcMenuPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FSequence* Sequence = Sequences.Find(Playing);
	if (Sequence == nullptr)
	{
		SetActorTickEnabled(false);
		return;
	}
	PlayTime = FMath::Min(PlayTime + DeltaSeconds, Sequence->End);
	float V[6];
	for (int32 Track = 0; Track < 6; ++Track)
	{
		V[Track] = swc::menu::Evaluate(Sequence->Tracks[Track], PlayTime);
	}
	// The positions are local to the parent of MainCamera (Main_layer: a translation).
	PlaceCamera(FVector(V[0], V[1], V[2]) + CameraParent, FVector(V[3], V[4], V[5]));
	if (PlayTime >= Sequence->End)
	{
		// The end: the camera keeps the last pose (no other driver of MainCamera).
		Playing.Reset();
		SetActorTickEnabled(false);
	}
}

void ASwcMenuPreview::UpdateFov(float Aspect)
{
	if (Aspect > 0.0f)
	{
		const double Half = FMath::DegreesToRadians(VerticalFov * 0.5);
		Camera->SetFieldOfView(static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(Half) * Aspect))));
	}
}

void ASwcMenuPreview::ShowLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation)
{
	const int32 Generation = ++Requested;
	TArray<FSoftObjectPath> Paths;
	const auto Add = [&Paths](const FString& Path)
	{
		if (!Path.IsEmpty())
		{
			Paths.AddUnique(FSoftObjectPath(Path));
		}
	};
	Add(BaseMesh);
	Add(IdleAnimation);
	for (const FSwcLookPart& Part : Look.Parts)
	{
		Add(Part.MeshPath);
	}
	if (Loading.IsValid())
	{
		Loading->CancelHandle();
		Loading.Reset();
	}
	TWeakObjectPtr<ASwcMenuPreview> Self(this);
	Loading = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		[Self, Generation, Look, BaseMesh, IdleAnimation]()
		{
			if (Self.IsValid())
			{
				Self->Build(Generation, Look, BaseMesh, IdleAnimation);
			}
		});
	if (!Loading.IsValid())
	{
		// No asset to load (each path is empty, or each one is in memory): the body now.
		Build(Generation, Look, BaseMesh, IdleAnimation);
	}
}

void ASwcMenuPreview::Build(int32 Generation, FSwcPlayerLook Look, FString BaseMesh, FString IdleAnimation)
{
	// A newer request: this load is old (A must not replace B).
	if (Generation != Requested)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("menu: the preview body %d is old (the request %d is newer): not shown"), Generation, Requested);
		return;
	}
	Clear();
	Shown = Generation;
	USkeletalMesh* Base = BaseMesh.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *BaseMesh);
	if (Base == nullptr)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("menu: no base mesh %s (tools/convert-player.sh base, tools/import-player.sh)"), *BaseMesh);
		return;
	}
	Leader->SetSkeletalMesh(Base);
	if (UAnimSequence* Idle = IdleAnimation.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *IdleAnimation))
	{
		Leader->PlayAnimation(Idle, true);
	}
	for (const FSwcLookPart& Part : Look.Parts)
	{
		USkeletalMesh* Mesh = Part.MeshPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *Part.MeshPath);
		if (Mesh == nullptr)
		{
			continue;
		}
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this);
		Component->SetupAttachment(Leader);
		Component->RegisterComponent();
		Component->SetSkeletalMesh(Mesh);
		Component->SetLeaderPoseComponent(Leader);
		Component->SetVisibility(bBodyVisible);
		if (Part.Tint.A > 0.0f)
		{
			// The hair color: a tint of the base color of each material of the part (as the world character).
			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
			{
				if (UMaterialInstanceDynamic* Tinted = Component->CreateAndSetMaterialInstanceDynamic(Index))
				{
					Tinted->SetVectorParameterValue(TEXT("BaseColorFactor"), Part.Tint);
				}
			}
		}
		Parts.Add(Component);
	}
	UE_LOG(LogSwcBackend, Log, TEXT("menu: the preview body %d: %d part(s) of %d (%s)"), Generation, Parts.Num(), Look.Parts.Num(),
		Look.bFemale ? TEXT("female") : TEXT("male"));
}

void ASwcMenuPreview::Clear()
{
	for (USkeletalMeshComponent* Part : Parts)
	{
		if (Part != nullptr)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	Leader->SetSkeletalMesh(nullptr);
}

void ASwcMenuPreview::SetBodyVisible(bool bVisible)
{
	bBodyVisible = bVisible;
	for (USkeletalMeshComponent* Part : Parts)
	{
		if (Part != nullptr)
		{
			Part->SetVisibility(bVisible);
		}
	}
}
