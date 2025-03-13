#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <stdio.h>
#include "UI.h"

// Ajout des entêtes spécifiques pour Prospero (PS5) si nécessaire
#ifdef _SCE_TARGET_OS_PROSPERO
#include <prospero_sdk.h> // Ceci est un exemple, le nom exact peut varier
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1900)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

int main( int argc, char** argv )
{
#ifdef _SCE_TARGET_OS_PROSPERO
    // Initialisation spécifique PS5 si nécessaire
    sceKernelRegisterCallbackHandler(); // Exemple, à remplacer par l'API correcte
#endif

	// Configuration de SDL
	if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ) != 0 )
	{
		printf( "Erreur: %s\n", SDL_GetError() );
		return -1;
	}

	// Configuration de la fenêtre
	SDL_WindowFlags window_flags = ( SDL_WindowFlags )( SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI );

#ifdef _SCE_TARGET_OS_PROSPERO
    // Configurations spécifiques pour PS5 si nécessaire
    window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_FULLSCREEN);
#endif

	SDL_Window* window = SDL_CreateWindow( "Input Test",
										   SDL_WINDOWPOS_CENTERED,
										   SDL_WINDOWPOS_CENTERED,
										   800, 600,
										   window_flags );
	if( window == nullptr )
	{
		printf( "Erreur: %s\n", SDL_GetError() );
		SDL_Quit();
		return -1;
	}

	// Configuration du renderer
	SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED );
	if( renderer == nullptr )
	{
		SDL_DestroyWindow( window );
		SDL_Quit();
		printf( "Erreur: %s\n", SDL_GetError() );
		return -1;
	}

	// Configuration d'ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Activer le clavier
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Activer le gamepad

#ifdef _SCE_TARGET_OS_PROSPERO
    // Configuration spécifique pour les manettes PS5
    io.ConfigFlags |= ImGuiConfigFlags_GamepadNavActive; // Activer la navigation par gamepad par défaut
#endif

	// Style d'ImGui
	ImGui::StyleColorsDark();

	// Initialiser l'implémentation d'ImGui pour SDL et SDL Renderer
	ImGui_ImplSDL2_InitForSDLRenderer( window, renderer );
	ImGui_ImplSDLRenderer2_Init( renderer );

	// Couleur de fond claire
	ImVec4 clear_color = ImVec4( 0.45f, 0.55f, 0.60f, 1.00f );

	// Créer l'application
	WheelTestApp app;
	if( app.initialize() != SUCCESS )
	{
		fprintf( stderr, "Erreur d'initialisation de l'application\n" );
		// Nettoyage
		ImGui_ImplSDLRenderer2_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		SDL_DestroyRenderer( renderer );
		SDL_DestroyWindow( window );
		SDL_Quit();

		return ERROR_SDL_INIT_FAILED;
	}

	// Boucle principale
	bool done = false;
	while( !done )
	{
		// Gérer les événements SDL
		SDL_Event event;
		while( SDL_PollEvent( &event ) )
		{
			ImGui_ImplSDL2_ProcessEvent( &event );
			if( event.type == SDL_QUIT )
				done = true;
			if( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID( window ) )
				done = true;

#ifdef _SCE_TARGET_OS_PROSPERO
            // Gestion des événements spécifiques à la manette PS5
            if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
                done = true; // Quitter avec le bouton OPTIONS de la manette
#endif
		}

		// Mettre à jour la logique de l'application
		app.update();

		// Débuter une nouvelle frame ImGui
		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		// Rendre l'interface utilisateur
		app.renderUI( window );

		// Rendu
		ImGui::Render();
		SDL_SetRenderDrawColor( renderer, ( Uint8 )( clear_color.x * 255 ), ( Uint8 )( clear_color.y * 255 ), ( Uint8 )( clear_color.z * 255 ), ( Uint8 )( clear_color.w * 255 ) );
		SDL_RenderClear( renderer );
		ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer );
		SDL_RenderPresent( renderer );
	}

	// Nettoyage
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer( renderer );
	SDL_DestroyWindow( window );
	SDL_Quit();

#ifdef _SCE_TARGET_OS_PROSPERO
    // Nettoyage spécifique PS5 si nécessaire
    sceKernelUnregisterCallbackHandler(); // Exemple, à remplacer par l'API correcte
#endif

	return 0;
}
