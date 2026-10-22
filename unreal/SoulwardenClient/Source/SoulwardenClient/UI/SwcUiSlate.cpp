#include "UI/SwcUiSlate.h"

#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/SecureHash.h"
#include "TextureCompiler.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
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

namespace
{
/** A PNG file as a transient texture: the settings come before the only UpdateResource (one render resource).
 *  bPremultiply: each color times its alpha. */
UTexture2D* LoadTexture(const FString& Path, bool bPremultiply)
{
	FImage Loaded;
	if (!FImageUtils::LoadImage(*Path, Loaded))
	{
		return nullptr;
	}
	FImage Image;
	Loaded.CopyTo(Image, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	if (bPremultiply)
	{
		for (FColor& Pixel : Image.AsBGRA8())
		{
			Pixel.R = static_cast<uint8>((Pixel.R * Pixel.A + 127) / 255);
			Pixel.G = static_cast<uint8>((Pixel.G * Pixel.A + 127) / 255);
			Pixel.B = static_cast<uint8>((Pixel.B * Pixel.A + 127) / 255);
		}
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(Image.SizeX, Image.SizeY, PF_B8G8R8A8);
	if (Texture == nullptr)
	{
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->bNotOfflineProcessed = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Image.RawData.GetData(), FMath::Min<int64>(Image.RawData.Num(), Mip.BulkData.GetBulkDataSize()));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
} // namespace

const FSlateBrush* FSwcUiResources::Brush(const std::string& File, bool bPremultiplied)
{
	const FString Path = ToFString(File);
	const FString Key = bPremultiplied ? Path + TEXT("|premultiplied") : Path;
	if (const FTexture* Found = Textures.Find(Key))
	{
		return Found->Brush.Get();
	}
	if (FailedTextures.Contains(Path))
	{
		return nullptr;
	}
	UTexture2D* Texture = LoadTexture(Path, bPremultiplied);
	if (Texture == nullptr)
	{
		UE_LOG(LogSwcUiSlate, Warning, TEXT("ui: the texture %s did not load"), *Path);
		FailedTextures.Add(Path);
		return nullptr;
	}
	FTexture Entry;
	Entry.Texture.Reset(Texture);
	Entry.LoadTime = FPlatformTime::Seconds();
	Entry.Brush = MakeShared<FSlateBrush>();
	Entry.Brush->SetResourceObject(Texture);
	Entry.Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Entry.Brush->DrawAs = ESlateBrushDrawType::Image;
	return Textures.Add(Key, MoveTemp(Entry)).Brush.Get();
}

UTexture2D* FSwcUiResources::MaterialTexture(const std::string& File)
{
	const FString Path = ToFString(File);
	if (const FTexture* Found = MaterialTextures.Find(Path))
	{
		return Found->Texture.Get();
	}
	if (FailedTextures.Contains(Path))
	{
		return nullptr;
	}
	// The textures of the mask material: built as an asset from source data (uncompressed, no mips). Open point O11:
	// the material still gets the default textures for these and for the transient textures of LoadTexture.
	FImage Loaded;
	if (!FImageUtils::LoadImage(*Path, Loaded))
	{
		UE_LOG(LogSwcUiSlate, Warning, TEXT("ui: the texture %s did not load"), *Path);
		FailedTextures.Add(Path);
		return nullptr;
	}
	FImage Image;
	Loaded.CopyTo(Image, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	TArray<FColor> Colors;
	Colors.SetNumUninitialized(Image.SizeX * Image.SizeY);
	FMemory::Memcpy(Colors.GetData(), Image.RawData.GetData(), FMath::Min<int64>(Image.RawData.Num(), Colors.Num() * sizeof(FColor)));
	FCreateTexture2DParameters Parameters;
	Parameters.bUseAlpha = true;
	Parameters.CompressionSettings = TC_EditorIcon;
	Parameters.MipGenSettings = TMGS_NoMipmaps;
	Parameters.TextureGroup = TEXTUREGROUP_UI;
	Parameters.bSRGB = true;
	FMD5 Md5;
	const FTCHARToUTF8 Utf8Path(*Path);
	Md5.Update(reinterpret_cast<const uint8*>(Utf8Path.Get()), Utf8Path.Length());
	Md5.Update(Image.RawData.GetData(), Image.RawData.Num());
	uint32 Digest[4];
	Md5.Final(reinterpret_cast<uint8*>(Digest));
	Parameters.SourceGuidHash = FGuid(Digest[0], Digest[1], Digest[2], Digest[3]);
	UTexture2D* Texture = FImageUtils::CreateTexture2D(Image.SizeX, Image.SizeY, Colors, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("SwcUiMaterialTexture")).ToString(), RF_Transient, Parameters);
	if (Texture == nullptr)
	{
		FailedTextures.Add(Path);
		return nullptr;
	}
#if WITH_EDITOR
	// An editor build compiles the texture async; a cooked game has its platform data at once.
	UTexture* Built[] = {Texture};
	FTextureCompilingManager::Get().FinishCompilation(Built);
#endif
	FTexture Entry;
	Entry.Texture.Reset(Texture);
	Entry.LoadTime = FPlatformTime::Seconds();
	return MaterialTextures.Add(Path, MoveTemp(Entry)).Texture.Get();
}

UMaterialInterface* FSwcUiResources::MaskMaterial(bool bAdditive)
{
	const int32 Index = bAdditive ? 1 : 0;
	if (!MaskMaterials[Index].IsValid() && !bMaskMaterialFailed[Index])
	{
		const TCHAR* Path = bAdditive ? TEXT("/SwcGameContent/SwcUi/M_SwcUiMaskedAdditive.M_SwcUiMaskedAdditive")
									  : TEXT("/SwcGameContent/SwcUi/M_SwcUiMasked.M_SwcUiMasked");
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
		if (Material == nullptr)
		{
			UE_LOG(LogSwcUiSlate, Warning, TEXT("ui: the mask material %s did not load (tools/editor-script.sh ui_materials.py): images draw with no sprite mask"), Path);
			bMaskMaterialFailed[Index] = true;
			return nullptr;
		}
		MaskMaterials[Index].Reset(Material);
		// The client is an uncooked game run: a shader map from the cache can miss the Slate shaders of a custom
		// vertex mesh, and the engine compiles a missing shader only while a compile of the map runs. Then a recompile
		// makes the full map (the next runs load it from the cache).
#if WITH_EDITOR   // a cooked game has the shaders of the cook
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (Resource != nullptr && Resource->IsCompilationFinished() && !Resource->IsGameThreadShaderMapComplete())
		{
			UE_LOG(LogSwcUiSlate, Log, TEXT("ui: the mask material %s has an incomplete shader map: it compiles again"), Path);
			Material->ForceRecompileForRendering();
		}
#endif
	}
	return MaskMaterials[Index].Get();
}

double FSwcUiResources::LoadTime(const std::string& File) const
{
	const FTexture* Found = MaterialTextures.Find(ToFString(File));
	return Found != nullptr ? Found->LoadTime : 0.0;
}

const FSlateBrush* FSwcUiResources::MaskedBrush(const std::string& File, const std::string& MaskFile, float AlphaTest, bool bAdditive, bool& bOutWait)
{
	bOutWait = false;
	const FString Key = FString::Printf(TEXT("%s|%s|%.3f|%d"), *ToFString(File), *ToFString(MaskFile), AlphaTest, bAdditive ? 1 : 0);
	if (const FMasked* Found = Masked.Find(Key))
	{
		return Found->Brush.Get();
	}
	UMaterialInterface* Base = MaskMaterial(bAdditive);
	UTexture2D* Image = File.empty() ? nullptr : MaterialTexture(File);
	UTexture2D* Mask = MaterialTexture(MaskFile);
	if (Base == nullptr || Mask == nullptr || (!File.empty() && Image == nullptr))
	{
		return nullptr;
	}
	// The instance comes when its textures are 0.5 s old (the texture reference of a new texture follows it some frames
	// after its load); until then the item does not draw.
	const double Now = FPlatformTime::Seconds();
	if ((Image != nullptr && Now - LoadTime(File) < 0.5) || Now - LoadTime(MaskFile) < 0.5)
	{
		bOutWait = true;
		return nullptr;
	}
	FMasked Entry;
	Entry.Material.Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
	if (Image != nullptr)
	{
		Entry.Material->SetTextureParameterValue(TEXT("Texture"), Image);
	}
	Entry.Material->SetTextureParameterValue(TEXT("Mask"), Mask);
	Entry.Material->SetScalarParameterValue(TEXT("AlphaTest"), AlphaTest);
	Entry.Brush = MakeShared<FSlateBrush>();
	Entry.Brush->SetResourceObject(Entry.Material.Get());
	Entry.Brush->ImageSize = FVector2D(1.0, 1.0);
	Entry.Brush->DrawAs = ESlateBrushDrawType::Image;
	return Masked.Add(Key, MoveTemp(Entry)).Brush.Get();
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

FShapedGlyphSequenceRef FSwcUiResources::Shape(const std::string& FontFile, float PixelSize, float LetterSpacing, const FString& Text, float Scale)
{
	const FString Key = FString::Printf(TEXT("%s|%.3f|%.3f|%.4f|"), *ToFString(FontFile), PixelSize, LetterSpacing, Scale) + Text;
	if (const FShapedGlyphSequenceRef* Found = ShapedLines.Find(Key))
	{
		// AST-258: a flush of the Slate font cache (an atlas out of space) frees the font faces of the cached lines: such a line draws
		// nothing (the key hints and the health number went empty after some minutes). A dirty line is shaped again.
		if (!(*Found)->IsDirty())
		{
			return *Found;
		}
		static double LastNote = -100.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastNote > 10.0)
		{
			LastNote = Now;
			UE_LOG(LogTemp, Log, TEXT("ui: a cached text line lost its font faces (a flush of the Slate font cache): shaped again (%d cached lines)"),
				ShapedLines.Num());
		}
		ShapedLines.Remove(Key);
	}
	const TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
	FShapedGlyphSequenceRef Line = FontCache->ShapeBidirectionalText(Text, Font(FontFile, PixelSize, 0.0f), Scale,
		TextBiDi::ETextDirection::LeftToRight, GetDefaultTextShapingMethod());
	const TArray<FShapedGlyphEntry>& Glyphs = Line->GetGlyphsToRender();
	if (!FMath::IsNearlyZero(LetterSpacing) && Glyphs.Num() > 1)
	{
		TArray<FShapedGlyphEntry> Spaced = Glyphs;
		const float Step = LetterSpacing / 1000.0f * PixelSize * Scale;
		for (int32 Index = 0; Index + 1 < Spaced.Num(); ++Index)
		{
			const int32 Add = FMath::RoundToInt(Step * static_cast<float>(Index + 1)) - FMath::RoundToInt(Step * static_cast<float>(Index));
			Spaced[Index].XAdvance = static_cast<int16>(FMath::Clamp(Spaced[Index].XAdvance + Add, -32768, 32767));
		}
		Line = MakeShared<const FShapedGlyphSequence>(MoveTemp(Spaced), Line->GetTextBaseline(), Line->GetMaxTextHeight(), Line->GetFontMaterial(),
			Line->IsMaterialStencil(), Line->GetFontOutlineSettings(), FShapedGlyphSequence::FSourceTextRange(0, Text.Len()));
	}
	// A line of a font that still loads has no glyphs yet: it shapes again in the next frame.
	if (!Line->IsDirty() && Line->GetGlyphsToRender().Num() > 0)
	{
		if (ShapedLines.Num() >= 4096)
		{
			ShapedLines.Reset();
		}
		ShapedLines.Add(Key, Line);
	}
	return Line;
}

float FSwcUiResources::width(const std::string& FontFile, float Size, float LetterSpacing, std::string_view Text)
{
	if (!FSlateApplication::IsInitialized() || Text.empty())
	{
		return static_cast<float>(Text.size()) * Size * 0.5f;
	}
	const FString Line = UTF8_TO_TCHAR(std::string(Text).c_str());
	// The width at the drawn scale, in canvas units.
	if (!FMath::IsNearlyZero(LetterSpacing))
	{
		return static_cast<float>(Shape(FontFile, Size, LetterSpacing, Line, MeasureScale)->GetMeasuredWidth()) / MeasureScale;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return static_cast<float>(Measure->Measure(Line, Font(FontFile, Size, 0.0f), MeasureScale).X) / MeasureScale;
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
	LastGeometry = AllottedGeometry;
	if (List == nullptr || !Resources.IsValid() || !FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	const FSlateRenderTransform ToWindow = AllottedGeometry.GetAccumulatedRenderTransform();
	Resources->SetMeasureScale(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale());
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
			const bool bAdditive = Item.blend == swc::ui::kBlendAdd;
			// An image under a sprite mask: the mask material (the mesh UVs, then the mask UVs of each vertex).
			// The sprite mask with the mask material only with -SwcUiMaskMaterial (open point O11 of docs/UI.md: the
			// material draws the default textures); else the mask clips with its quad, as the push_clip items give.
			static const bool bMaskMaterial = FParse::Param(FCommandLine::Get(), TEXT("SwcUiMaskMaterial"));
			bool bWait = false;
			const bool bMasked = bMaskMaterial && !Item.maskTexture.empty() &&
								 (Brush = Resources->MaskedBrush(Item.texture, Item.maskTexture, Item.maskAlphaTest, bAdditive, bWait)) != nullptr;
			if (bWait)
			{
				break;
			}
			if (!bMasked && !Item.texture.empty())
			{
				Brush = Resources->Brush(Item.texture, bAdditive);
				if (Brush == nullptr)
				{
					break;
				}
			}
			const FSlateResourceHandle Handle = Brush != nullptr ? Renderer->GetResourceHandle(*Brush) : FSlateResourceHandle();
			Vertices.Reset(static_cast<int32>(Item.vertices.size()));
			Indices.Reset(static_cast<int32>(Item.indices.size()));
			for (const swc::ui::Vertex& V : Item.vertices)
			{
				FColor Color = ToColor(V.rgba);
				if (bMasked)
				{
					// The material multiplies the vertex color and gives the blend (an additive one adds color x alpha).
					Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow, FVector2f(V.x, V.y), FVector2f(V.u, V.v),
						FVector2f(V.mu, V.mv), Color));
					continue;
				}
				if (bAdditive)
				{
					// An additive image (LyShine blend mode Add): the premultiplied texture and color with alpha 0 add color x
					// alpha to the target.
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
				ESlateDrawEffect::None, bAdditive && !bMasked ? ESlateBatchDrawFlag::PreMultipliedAlpha : ESlateBatchDrawFlag::None);
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
			if (FMath::IsNearlyZero(Item.letterSpacing))
			{
				FSlateDrawElement::MakeText(OutDrawElements, ++Layer, Line.ToPaintGeometry(), UTF8_TO_TCHAR(Item.text.c_str()), FontInfo,
					ESlateDrawEffect::None, Tint);
				break;
			}
			// The character spacing: the glyphs shaped at the drawn scale (the font rasterizes at that size).
			const FShapedGlyphSequenceRef Glyphs = Resources->Shape(Item.fontFile, Item.fontSize, Item.letterSpacing, UTF8_TO_TCHAR(Item.text.c_str()),
				Line.GetAccumulatedLayoutTransform().GetScale());
			if (Glyphs->GetGlyphsToRender().Num() > 0)
			{
				FSlateDrawElement::MakeShapedText(OutDrawElements, ++Layer, Line.ToPaintGeometry(), Glyphs, ESlateDrawEffect::None, Tint,
					FLinearColor::Transparent);
			}
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
	if (bMarker)
	{
		// The pointer of the harness input: a white arrow with a dark edge (no texture).
		const FVector2f P = MarkerPosition;
		const auto Arrow = [&](float Scale, FColor Color)
		{
			const FVector2f Points[4] = {P, P + FVector2f(0.0f, 22.0f) * Scale, P + FVector2f(6.0f, 16.0f) * Scale, P + FVector2f(15.0f, 15.0f) * Scale};
			Vertices.Reset();
			Indices.Reset();
			for (const FVector2f& Point : Points)
			{
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow, Point, FVector2f::ZeroVector, Color));
			}
			Indices.Append({0, 1, 2, 0, 2, 3});
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, ++Layer, FSlateResourceHandle(), Vertices, Indices, nullptr, 0, 0);
		};
		Arrow(1.25f, FColor(20, 20, 20, 230));
		Arrow(1.0f, FColor(255, 255, 255, 255));
	}
	return Layer;
}
