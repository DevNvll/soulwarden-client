#include "Menu/SwcMenuPreview.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"

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
} // namespace

ASwcMenuPreview::ASwcMenuPreview()
{
	PrimaryActorTick.bCanEverTick = false;
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
