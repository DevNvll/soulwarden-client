#include "UI/SwcGameSettings.h"

#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Scalability.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/SwcKeyBindings.h"
#include "UnrealClient.h"
#include "UnrealEngine.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcSettings, Log, All);

namespace
{
/** The render scale of each mode of the row Resolution Scaling (the data of the drop-down: 1 Native, 2 Quality, 3 Balanced,
 *  4 Performance, 5 Ultra Performance; options.luac FsrTemporalUpscalingModeData): the scale factors of these modes of the
 *  retail upscaler (1.5x, 1.7x, 2x, 3x of the render resolution), in percent of the output. The type 1 (Off) is 100 %. */
float ScreenPercentageOf(int32 Type, int32 Mode)
{
	if (Type <= 1)
	{
		return 100.0f;
	}
	switch (Mode)
	{
	case 2: return 100.0f / 1.5f;
	case 3: return 100.0f / 1.7f;
	case 4: return 50.0f;
	case 5: return 100.0f / 3.0f;
	default: return 100.0f;
	}
}

/** A quality row (1 Low, 2 Medium, 3 High, 4 Very High; the data of options.luac) -> the Unreal scalability level. The level
 *  is the same number: High (3, the default of the client program) keeps the Epic levels that this client uses now, and Low
 *  is the Unreal level Medium (the Unreal level Low turns off Lumen and changes the look of the world). */
int32 LevelOf(double Value)
{
	return FMath::Clamp(FMath::RoundToInt32(Value), 1, 4);
}

void SetConsoleValue(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(*Value, ECVF_SetByCode);
	}
}
} // namespace

void USwcGameSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	int32 Unused = 0;
	bDisplayFromCommandLine = FParse::Value(FCommandLine::Get(), TEXT("ResX="), Unused) || FParse::Value(FCommandLine::Get(), TEXT("ResY="), Unused)
		|| FParse::Param(FCommandLine::Get(), TEXT("windowed")) || FParse::Param(FCommandLine::Get(), TEXT("fullscreen"));
	Load();
	for (const TPair<FString, FString>& Pair : Values)
	{
		Apply(Pair.Key, true);
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USwcGameSettings::Tick), 0.25f);
	UE_LOG(LogSwcSettings, Log, TEXT("settings: %d saved value(s) of %s%s"), Values.Num(), *SavePath(),
		bDisplayFromCommandLine ? TEXT("; the command line gives the display") : TEXT(""));
}

void USwcGameSettings::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

FString USwcGameSettings::SavePath() const
{
	return FPaths::Combine(USwcKeyBindings::SettingsDir(), TEXT("options.json"));
}

void USwcGameSettings::Load()
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Options = nullptr;
	if (Root->TryGetObjectField(TEXT("options"), Options))
	{
		for (const auto& Pair : (*Options)->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value) && Value.Len() >= 1)
			{
				Values.Add(FString(Pair.Key), Value);
			}
		}
	}
}

void USwcGameSettings::Save() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	TArray<FString> Nodes;
	Values.GetKeys(Nodes);
	Nodes.Sort();
	for (const FString& Node : Nodes)
	{
		Options->SetStringField(Node, Values[Node]);
	}
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetObjectField(TEXT("options"), Options);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	const FString Path = SavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Text, *Path);
	UE_LOG(LogSwcSettings, Log, TEXT("settings: %d value(s) %s %s"), Values.Num(), bSaved ? TEXT("saved to") : TEXT("NOT saved to"), *Path);
}

FString USwcGameSettings::NodeLines() const
{
	FString Lines;
	for (const TPair<FString, FString>& Pair : Values)
	{
		Lines += Pair.Key + TEXT("\t") + Pair.Value + TEXT("\n");
	}
	return Lines;
}

FString USwcGameSettings::ResolutionLines()
{
	FString Lines;
	FScreenResolutionArray Resolutions;
	if (GDynamicRHI != nullptr && RHIGetAvailableResolutions(Resolutions, true))
	{
		for (const FScreenResolutionRHI& Resolution : Resolutions)
		{
			Lines += FString::Printf(TEXT("%ux%u\n"), Resolution.Width, Resolution.Height);
		}
	}
	return Lines;
}

int32 USwcGameSettings::CurrentWindowMode()
{
	const TSharedPtr<SWindow> Window = GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWindow() : nullptr;
	if (!Window.IsValid())
	{
		return 1;
	}
	switch (Window->GetWindowMode())
	{
	case EWindowMode::Fullscreen: return 0;
	case EWindowMode::WindowedFullscreen: return 2;
	default: return 1;
	}
}

double USwcGameSettings::Number(const FString& Node, double Default) const
{
	const FString* Value = Values.Find(Node);
	if (Value == nullptr || Value->IsEmpty())
	{
		return Default;
	}
	if ((*Value)[0] == TEXT('b'))
	{
		return *Value == TEXT("b1") ? 1.0 : 0.0;
	}
	if ((*Value)[0] == TEXT('n'))
	{
		return FCString::Atod(*Value->Mid(1));
	}
	return Default;
}

bool USwcGameSettings::HandleRequest(const FString& Request)
{
	if (Request == TEXT("options-save"))
	{
		Save();
		return true;
	}
	if (!Request.StartsWith(TEXT("options\n")))
	{
		return false;
	}
	TArray<FString> Lines;
	Request.ParseIntoArray(Lines, TEXT("\n"));
	bool bDisplay = false;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		FString Node, Value;
		if (!Lines[Index].Split(TEXT("\t"), &Node, &Value))
		{
			continue;
		}
		Values.Add(Node, Value);
		if (Node == TEXT("Video.WindowMode") || Node.StartsWith(TEXT("Video.Resolution.")))
		{
			bDisplay = true;
		}
		else
		{
			Apply(Node, false);
		}
	}
	if (bDisplay)
	{
		ApplyDisplay();
	}
	return true;
}

void USwcGameSettings::Apply(const FString& Node, bool bStart)
{
	if (Node == TEXT("Video.TemporalUpscalingType") || Node == TEXT("Video.TemporalUpscalingMode"))
	{
		ApplyRenderScale();
	}
	else if (Node == TEXT("Video.MaxFps"))
	{
		// The frames per second of the row (-1: Uncapped).
		const double Fps = Number(Node, -1.0);
		SetConsoleValue(TEXT("t.MaxFPS"), FString::SanitizeFloat(Fps > 0.0 ? Fps : 0.0));
		UE_LOG(LogSwcSettings, Log, TEXT("settings: the frame rate limit %s"), Fps > 0.0 ? *FString::Printf(TEXT("%.0f"), Fps) : TEXT("none (Uncapped)"));
	}
	else if (Node == TEXT("Video.VsyncEnabled"))
	{
		const bool bOn = Number(Node, 0.0) != 0.0;
		SetConsoleValue(TEXT("r.VSync"), bOn ? TEXT("1") : TEXT("0"));
		UE_LOG(LogSwcSettings, Log, TEXT("settings: V-Sync %s"), bOn ? TEXT("on") : TEXT("off"));
	}
	else if (Node.EndsWith(TEXT("Quality")) && Node.StartsWith(TEXT("Video.")))
	{
		ApplyQuality();
	}
	else if (Node == TEXT("Video.Fov"))
	{
		// The range of the row: 40 to 70 (swc/options.lua, the retail picture of set 1).
		Fov = FMath::Clamp(static_cast<float>(Number(Node, 57.0)), 40.0f, 70.0f);
		UE_LOG(LogSwcSettings, Log, TEXT("settings: the field of view %.0f degrees (vertical)"), Fov);
	}
	else if (Node == TEXT("Controls.InvertLook"))
	{
		bInvertLook = Number(Node, 0.0) != 0.0;
		UE_LOG(LogSwcSettings, Log, TEXT("settings: the invert of the mouse look %s"), bInvertLook ? TEXT("on") : TEXT("off"));
	}
}

void USwcGameSettings::ApplyRenderScale()
{
	const int32 Type = FMath::RoundToInt32(Number(TEXT("Video.TemporalUpscalingType"), 2.0));
	const int32 Mode = FMath::RoundToInt32(Number(TEXT("Video.TemporalUpscalingMode"), 2.0));
	const float Percentage = ScreenPercentageOf(Type, Mode);
	SetConsoleValue(TEXT("r.ScreenPercentage"), FString::SanitizeFloat(Percentage));
	UE_LOG(LogSwcSettings, Log, TEXT("settings: the render scale %.1f %% (type %d, mode %d)"), Percentage, Type, Mode);
}

void USwcGameSettings::ApplyQuality()
{
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	const auto Level = [this](const TCHAR* Node) { return LevelOf(Number(Node, 3.0)); };
	// The rows of the quality and their Unreal groups (the detail rows of options.luac; Water has no group: its row is hidden).
	Levels.EffectsQuality = Level(TEXT("Video.EffectsQuality"));
	Levels.GlobalIlluminationQuality = Level(TEXT("Video.LightingQuality"));
	Levels.ReflectionQuality = Level(TEXT("Video.LightingQuality"));
	Levels.ViewDistanceQuality = Level(TEXT("Video.ObjectsQuality"));
	Levels.PostProcessQuality = Level(TEXT("Video.PostProcessingQuality"));
	Levels.ShadowQuality = Level(TEXT("Video.ShadowsQuality"));
	Levels.LandscapeQuality = Level(TEXT("Video.TerrainQuality"));
	Levels.FoliageQuality = Level(TEXT("Video.TerrainQuality"));
	Levels.TextureQuality = Level(TEXT("Video.TexturesQuality"));
	Scalability::SetQualityLevels(Levels);
	UE_LOG(LogSwcSettings, Log, TEXT("settings: the quality levels: effects %d, lighting %d, objects %d, post %d, shadows %d, terrain %d, textures %d"),
		Levels.EffectsQuality, Levels.GlobalIlluminationQuality, Levels.ViewDistanceQuality, Levels.PostProcessQuality, Levels.ShadowQuality,
		Levels.LandscapeQuality, Levels.TextureQuality);
}

void USwcGameSettings::ApplyDisplay()
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr || GEngine->GameViewport->Viewport == nullptr)
	{
		return;
	}
	const int32 Width = FMath::RoundToInt32(Number(TEXT("Video.Resolution.Width"), 0.0));
	const int32 Height = FMath::RoundToInt32(Number(TEXT("Video.Resolution.Height"), 0.0));
	const int32 Mode = FMath::RoundToInt32(Number(TEXT("Video.WindowMode"), 1.0));
	const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
	const int32 Current = CurrentWindowMode();
	// The drop-down has Fullscreen (0) and Windowed (1); the windowed fullscreen of Unreal shows as Fullscreen.
	const bool bModeSame = Mode == 1 ? Current == 1 : Current != 1;
	// The UI gives the size of its widget in pixels (Swc.HostDisplay): a size within 2 px of the viewport is the same window.
	const bool bSizeSame = Width <= 0 || Height <= 0 || (FMath::Abs(Size.X - Width) <= 2 && FMath::Abs(Size.Y - Height) <= 2)
		|| (Mode != 1 && Current == 2);
	if (bModeSame && bSizeSame)
	{
		return;
	}
	const FIntPoint Target(Width > 0 ? Width : Size.X, Height > 0 ? Height : Size.Y);
	UE_LOG(LogSwcSettings, Log, TEXT("settings: the display %dx%d %s (was %dx%d, mode %d)"), Target.X, Target.Y,
		Mode == 1 ? TEXT("windowed") : TEXT("fullscreen"), Size.X, Size.Y, Current);
	FSystemResolution::RequestResolutionChange(Target.X, Target.Y, Mode == 1 ? EWindowMode::Windowed : EWindowMode::Fullscreen);
}

bool USwcGameSettings::Tick(float DeltaTime)
{
	if (!bStartApplied && GEngine != nullptr && GEngine->GameViewport != nullptr && GEngine->GameViewport->Viewport != nullptr)
	{
		bStartApplied = true;
		// The engine applies its own scalability state at its start: the saved levels and the render scale again.
		if (Values.Contains(TEXT("Video.GraphicsQuality")) || Values.Contains(TEXT("Video.EffectsQuality")))
		{
			ApplyQuality();
		}
		ApplyRenderScale();
		if (!bDisplayFromCommandLine && Values.Contains(TEXT("Video.Resolution.Width")))
		{
			ApplyDisplay();
		}
	}
	return true;
}
