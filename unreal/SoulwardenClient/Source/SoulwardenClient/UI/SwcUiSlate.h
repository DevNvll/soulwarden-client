#pragma once

#include "CoreMinimal.h"
#include "Fonts/ShapedTextFwd.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

#include "SwcUi/Draw.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
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

	/**
	 * @return The brush of a PNG file, or null when the file does not load (one log line). bPremultiplied: a copy with
	 * the color times the alpha (an additive image: the premultiplied blend of Slate then adds color x alpha).
	 */
	const FSlateBrush* Brush(const std::string& File, bool bPremultiplied = false);
	/**
	 * @return The brush of an image under a sprite mask (only with -SwcUiMaskMaterial; open point O11 of docs/UI.md): a
	 * dynamic instance of the UI mask material
	 * (/SwcGameContent/SwcUi/M_SwcUiMasked, Scripts/ui_materials.py) with the texture (empty: white), the mask texture,
	 * and the alpha test. Null when the material or a texture does not load (then the image draws with no mask), or
	 * with bOutWait while a texture is new (then the item does not draw in this frame).
	 */
	const FSlateBrush* MaskedBrush(const std::string& File, const std::string& MaskFile, float AlphaTest, bool bAdditive, bool& bOutWait);
	/** @return The font of a font file at a size in canvas pixels. */
	FSlateFontInfo Font(const std::string& File, float PixelSize, float LetterSpacing);
	/**
	 * @return The glyphs of a text line (a font size in canvas pixels, shaped at a layout scale) with the character spacing
	 * (LetterSpacing: 1/1000 of the font size) after each glyph but the last, as the layout of the runtime measures it.
	 * Slate applies FSlateFontInfo::LetterSpacing only in its shaper, in points (0.75 of the pixel value) and in whole
	 * pixels for each glyph (a small spacing is 0); the font measure and MakeText do not apply it. Thus this client
	 * shapes with no spacing and adds the spacing to the advances (the part of a pixel goes on to the next glyph). The
	 * lines stay for the next frames (cleared at 4096 lines).
	 */
	FShapedGlyphSequenceRef Shape(const std::string& FontFile, float PixelSize, float LetterSpacing, const FString& Text, float Scale);

	// swc::ui::TextMeasure
	virtual float width(const std::string& FontFile, float Size, float LetterSpacing, std::string_view Text) override;
	virtual float line_height(const std::string& FontFile, float Size) override;
	virtual float ascent(const std::string& FontFile, float Size) override;

	int32 TextureCount() const { return Textures.Num(); }

private:
	struct FTexture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
		double LoadTime = 0.0;
	};
	struct FMasked
	{
		TStrongObjectPtr<UMaterialInstanceDynamic> Material;
		TSharedPtr<FSlateBrush> Brush;
	};
	UTexture2D* MaterialTexture(const std::string& File);
	double LoadTime(const std::string& File) const;
	UMaterialInterface* MaskMaterial(bool bAdditive);

	TMap<FString, FTexture> Textures;
	TSet<FString> FailedTextures;
	TMap<FString, FMasked> Masked;
	TMap<FString, FTexture> MaterialTextures;
	TStrongObjectPtr<UMaterialInterface> MaskMaterials[2];
	bool bMaskMaterialFailed[2] = {false, false};
	TMap<FString, TSharedPtr<FStandaloneCompositeFont>> Fonts;
	TMap<FString, FShapedGlyphSequenceRef> ShapedLines;
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
	/** @return The geometry of the widget at the last paint (screen space to the viewport of the runtime). */
	const FGeometry& GetLastGeometry() const { return LastGeometry; }
	/** The marker of the pointer of the harness input (viewport pixels), or none. */
	void SetPointerMarker(bool bShow, FVector2f Position) { bMarker = bShow; MarkerPosition = Position; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

private:
	TSharedPtr<FSwcUiResources> Resources;
	const swc::ui::DrawList* List = nullptr;
	mutable FVector2f LastSize = FVector2f(1920.0f, 1080.0f);
	mutable FGeometry LastGeometry;
	bool bMarker = false;
	FVector2f MarkerPosition = FVector2f::ZeroVector;
};
