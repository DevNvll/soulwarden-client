#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The loading gate (the rule of the user, 2026-10-20, AST-203; docs/WORLD.md "The loading"): when a loading screen ends (the
 * world entry, a travel, a respawn), all content near the character is there and visible. While a screen shows, the
 * content systems (the ground cover, the surface layer, the persons, the creatures, the objects) load at the place where
 * the character will be (GetView: that place during a screen, else the camera of the player), in parallel with the cells.
 * Each system registers a check "ready near this place"; SwcWorldStreaming::IsReadyAt ends the wait (and the screen) only
 * when the cells, the ground, every check, and the engine work near the picture are done (or the limit of the wait).
 */
namespace SwcLoadingGate
{
	/** The radius of "near the character" for the checks: the content within it is there when the screen ends. */
	constexpr double NearCm = 15000.0;

	/** @return The check of a system: true when its content near Place is there; Why gets the reason when not. */
	using FCheck = TFunction<bool(const FVector& Place, FString& Why)>;

	/** The place of the character while a loading screen shows (SwcWorldStreaming::IsReadyAt). */
	SOULWARDENCLIENT_API void SetPlace(const UWorld* World, const FVector& Place);
	SOULWARDENCLIENT_API void ClearPlace(const UWorld* World);
	SOULWARDENCLIENT_API bool GetPlace(const UWorld* World, FVector& Out);

	/** The frames after the start of a screen in which the content systems do no heavy work (AST-273 P8): the UI shows the screen in
	 *  the first tick after the request, and that frame must draw before a long frame (a travel froze the old view 0.8 to 3.1 s). */
	constexpr uint64 SettleFrames = 3;

	/** @return False in the first SettleFrames frames of a loading screen (the heavy work waits); true with no screen. */
	SOULWARDENCLIENT_API bool IsSettled(const UWorld* World);

	/** @return The view of the content systems: the place of the screen, else the camera of the first player (false: none). */
	SOULWARDENCLIENT_API bool GetView(const UWorld* World, FVector& Out);

	SOULWARDENCLIENT_API void Register(const UWorld* World, FName Name, FCheck Check);
	SOULWARDENCLIENT_API void Unregister(const UWorld* World, FName Name);

	/** @return True when every check is ready near Place; NotReady gets "name: reason" of the others. */
	SOULWARDENCLIENT_API bool ChecksReady(const UWorld* World, const FVector& Place, FString& NotReady);

	/** @return True when the engine work that changes the picture is done: the shader jobs, the meshes and the textures
	 *  that compile (an uncooked client builds them), and the textures that stream to their needed size. */
	SOULWARDENCLIENT_API bool EngineReady(FString& NotReady);

	/** The light of the game view in one frame: the exposure of the eye adaptation and the mean scene luminance. */
	struct FLight
	{
		float Exposure = 0.0f;
		float Luminance = 0.0f;
		uint64 Frame = 0;
	};

	/** @return The light of the last game view (false before the first frame; the first call starts the watch). */
	SOULWARDENCLIENT_API bool GetLight(FLight& Out);
}
