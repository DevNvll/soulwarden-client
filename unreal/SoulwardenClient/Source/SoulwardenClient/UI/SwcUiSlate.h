#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

#include "SwcUi/Draw.h"

class UTexture2D;
struct FStandaloneCompositeFont;

/**
 * The Slate resources of the UI runtime (docs/UI.md): the textures of the converted content (PNG files loaded at run
 * time, kept alive while the UI runs), the fonts (the OTF and TTF files of the game as runtime composite fonts), and
 * the text metrics of Slate for the layout of the texts.
 *
 * Font sizes: LyShine gives the size of a font in pixels of the canvas; a Slate font size is in points at 96 DPI. The
 * factor 0.75 gives the same pixels (open point O3 of the research: a side-by-side picture checks it).
 */
class FSwcUiResources : public swc::ui::TextMeasure
{
public:
	static constexpr float FontPointsPerPixel = 0.75f;

	/** @return The brush of a PNG file, or null when the file does not load (one log line). */
	const FSlateBrush* Brush(const std::string& File);
	/** @return The font of a font file at a size in canvas pixels. */
	FSlateFontInfo Font(const std::string& File, float PixelSize, float LetterSpacing);

	// swc::ui::TextMeasure
	virtual float width(const std::string& FontFile, float Size, float LetterSpacing, std::string_view Text) override;
	virtual float line_height(const std::string& FontFile, float Size) override;

	int32 TextureCount() const { return Textures.Num(); }

private:
	struct FTexture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	TMap<FString, FTexture> Textures;
	TSet<FString> FailedTextures;
	TMap<FString, TSharedPtr<FStandaloneCompositeFont>> Fonts;
};

/**
 * The widget of the UI: it fills the game viewport and draws the draw list of the runtime (meshes of textured triangles,
 * text lines, and clip quads) in its order. It has no input in this stage.
 */
class SSwcUiCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSwcUiCanvas) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FSwcUiResources> InResources);

	/** The draw list to draw (the subsystem owns it and fills it each frame). */
	void SetDrawList(const swc::ui::DrawList* InList) { List = InList; }
	/** @return The size of the widget in Slate units at the last paint (the viewport of the runtime). */
	FVector2f GetLastSize() const { return LastSize; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

private:
	TSharedPtr<FSwcUiResources> Resources;
	const swc::ui::DrawList* List = nullptr;
	mutable FVector2f LastSize = FVector2f(1920.0f, 1080.0f);
};
