#include "UI/SwcUiSlate.h"

#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiSlate, Log, All);

namespace
{
FString ToFString(const std::string& Text)
{
	return UTF8_TO_TCHAR(Text.c_str());
}

FColor ToColor(uint32 Rgba)
{
	return FColor(static_cast<uint8>(Rgba >> 24), static_cast<uint8>(Rgba >> 16), static_cast<uint8>(Rgba >> 8), static_cast<uint8>(Rgba));
}
} // namespace

// ------------------------------------------------------------------------------------------------------------ resources

const FSlateBrush* FSwcUiResources::Brush(const std::string& File)
{
	const FString Path = ToFString(File);
	if (const FTexture* Found = Textures.Find(Path))
	{
		return Found->Brush.Get();
	}
	if (FailedTextures.Contains(Path))
	{
		return nullptr;
	}
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
	if (Texture == nullptr)
	{
		UE_LOG(LogSwcUiSlate, Warning, TEXT("ui: the texture %s did not load"), *Path);
		FailedTextures.Add(Path);
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->UpdateResource();
	FTexture Entry;
	Entry.Texture.Reset(Texture);
	Entry.Brush = MakeShared<FSlateBrush>();
	Entry.Brush->SetResourceObject(Texture);
	Entry.Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Entry.Brush->DrawAs = ESlateBrushDrawType::Image;
	return Textures.Add(Path, MoveTemp(Entry)).Brush.Get();
}

FSlateFontInfo FSwcUiResources::Font(const std::string& File, float PixelSize, float LetterSpacing)
{
	const FString Path = ToFString(File);
	TSharedPtr<FStandaloneCompositeFont>& Composite = Fonts.FindOrAdd(Path);
	if (!Composite.IsValid())
	{
		// The font file of the game, loaded by Slate from the converted content when a text needs it.
		Composite = MakeShared<FStandaloneCompositeFont>(NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}
	FSlateFontInfo Info(StaticCastSharedPtr<const FCompositeFont>(Composite), FMath::Max(1.0f, PixelSize * FontPointsPerPixel));
	Info.LetterSpacing = FMath::RoundToInt(LetterSpacing);
	return Info;
}

float FSwcUiResources::width(const std::string& FontFile, float Size, float LetterSpacing, std::string_view Text)
{
	if (!FSlateApplication::IsInitialized() || Text.empty())
	{
		return static_cast<float>(Text.size()) * Size * 0.5f;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FString Line = UTF8_TO_TCHAR(std::string(Text).c_str());
	return static_cast<float>(Measure->Measure(Line, Font(FontFile, Size, LetterSpacing)).X);
}

float FSwcUiResources::line_height(const std::string& FontFile, float Size)
{
	if (!FSlateApplication::IsInitialized())
	{
		return Size;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return static_cast<float>(Measure->GetMaxCharacterHeight(Font(FontFile, Size, 0.0f)));
}

float FSwcUiResources::ascent(const std::string& FontFile, float Size)
{
	if (!FSlateApplication::IsInitialized())
	{
		return Size * 0.8f;
	}
	// Slate gives the baseline as a negative offset from the bottom of the line box.
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Info = Font(FontFile, Size, 0.0f);
	return static_cast<float>(Measure->GetMaxCharacterHeight(Info) + Measure->GetBaseline(Info));
}

// --------------------------------------------------------------------------------------------------------------- widget

void SSwcUiCanvas::Construct(const FArguments& InArgs, TSharedRef<FSwcUiResources> InResources)
{
	Resources = InResources;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SSwcUiCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LastSize = AllottedGeometry.GetLocalSize();
	if (List == nullptr || !Resources.IsValid() || !FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	const FSlateRenderTransform ToWindow = AllottedGeometry.GetAccumulatedRenderTransform();
	FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
	int32 Layer = LayerId;
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	for (const swc::ui::DrawItem& Item : List->items)
	{
		switch (Item.kind)
		{
		case swc::ui::DrawItem::Kind::mesh:
		{
			const FSlateBrush* Brush = nullptr;
			if (!Item.texture.empty())
			{
				Brush = Resources->Brush(Item.texture);
				if (Brush == nullptr)
				{
					break;
				}
			}
			const FSlateResourceHandle Handle = Brush != nullptr ? Renderer->GetResourceHandle(*Brush) : FSlateResourceHandle();
			const bool bAdditive = Item.blend == swc::ui::kBlendAdd;
			Vertices.Reset(static_cast<int32>(Item.vertices.size()));
			Indices.Reset(static_cast<int32>(Item.indices.size()));
			for (const swc::ui::Vertex& V : Item.vertices)
			{
				FColor Color = ToColor(V.rgba);
				if (bAdditive)
				{
					// An additive image (LyShine blend mode Add): premultiplied color with alpha 0 adds to the target.
					const float A = Color.A / 255.0f;
					Color = FColor(static_cast<uint8>(Color.R * A), static_cast<uint8>(Color.G * A), static_cast<uint8>(Color.B * A), 0);
				}
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow, FVector2f(V.x, V.y), FVector2f(V.u, V.v), Color));
			}
			for (const std::uint16_t Index : Item.indices)
			{
				Indices.Add(static_cast<SlateIndex>(Index));
			}
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, ++Layer, Handle, Vertices, Indices, nullptr, 0, 0,
				ESlateDrawEffect::None, bAdditive ? ESlateBatchDrawFlag::PreMultipliedAlpha : ESlateBatchDrawFlag::None);
			break;
		}
		case swc::ui::DrawItem::Kind::text:
		{
			// The line box in text space: x right, y down. The uniform scale of the transform goes into the layout transform
			// (the font rasterizes at its drawn size); the rest (a rotation, the width of a shrink) into the render transform.
			const swc::ui::Affine& M = Item.transform;
			const float Scale = FMath::Sqrt(FMath::Abs(M.a * M.d - M.b * M.c));
			if (Scale <= 1e-4f)
			{
				break;
			}
			const FSlateFontInfo FontInfo = Resources->Font(Item.fontFile, Item.fontSize, Item.letterSpacing);
			const FVector2f Translation(M.tx, M.ty);
			// Slate points are row vectors (p * M): the matrix (a, b; c, d) maps x' = a x + c y, y' = b x + d y.
			const FMatrix2x2f Rest(M.a / Scale, M.b / Scale, M.c / Scale, M.d / Scale);
			const bool bPlain = FMath::IsNearlyEqual(M.a / Scale, 1.0f) && FMath::IsNearlyZero(M.b / Scale) && FMath::IsNearlyZero(M.c / Scale) &&
								FMath::IsNearlyEqual(M.d / Scale, 1.0f);
			const FVector2f Box(4096.0f, Item.fontSize * 2.0f);
			const FGeometry Line = bPlain
				? AllottedGeometry.MakeChild(Box, FSlateLayoutTransform(Scale, Translation))
				: AllottedGeometry.MakeChild(Box, FSlateLayoutTransform(Scale, Translation), FSlateRenderTransform(Rest), FVector2f(0.0f, 0.0f));
			const FLinearColor Tint(FColor(static_cast<uint8>(FMath::Clamp(Item.color.r, 0.0f, 1.0f) * 255.0f),
				static_cast<uint8>(FMath::Clamp(Item.color.g, 0.0f, 1.0f) * 255.0f), static_cast<uint8>(FMath::Clamp(Item.color.b, 0.0f, 1.0f) * 255.0f),
				static_cast<uint8>(FMath::Clamp(Item.color.a, 0.0f, 1.0f) * 255.0f)));
			FSlateDrawElement::MakeText(OutDrawElements, ++Layer, Line.ToPaintGeometry(), UTF8_TO_TCHAR(Item.text.c_str()), FontInfo,
				ESlateDrawEffect::None, Tint);
			break;
		}
		case swc::ui::DrawItem::Kind::push_clip:
		{
			const FVector2f TopLeft = ToWindow.TransformPoint(FVector2f(Item.quad[0].x, Item.quad[0].y));
			const FVector2f TopRight = ToWindow.TransformPoint(FVector2f(Item.quad[1].x, Item.quad[1].y));
			const FVector2f BottomRight = ToWindow.TransformPoint(FVector2f(Item.quad[2].x, Item.quad[2].y));
			const FVector2f BottomLeft = ToWindow.TransformPoint(FVector2f(Item.quad[3].x, Item.quad[3].y));
			OutDrawElements.PushClip(FSlateClippingZone(TopLeft, TopRight, BottomLeft, BottomRight));
			break;
		}
		case swc::ui::DrawItem::Kind::pop_clip:
			OutDrawElements.PopClip();
			break;
		}
	}
	return Layer;
}
