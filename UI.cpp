#include "UI.h"
#include "imgui_internal.h"

// Version de l'application
const char* APP_VERSION = "1.0.0";

// Définition des modes de test
const char* WheelTestApp::testModes[] = {
	"Mode Manuel", "Mode Automatique", "Mode Enregistrement"
};

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE LOGGER
// =============================================================================

Logger::Logger() : toFile( false ), verbose( false )
{
}

Logger::~Logger()
{
	if( logFile.is_open() )
	{
		logFile.close();
	}
}

std::string Logger::getCurrentTimestamp()
{
	auto              now  = std::chrono::system_clock::now();
	auto              time = std::chrono::system_clock::to_time_t( now );
	std::stringstream ss;
	ss << std::put_time( std::localtime( &time ), "%Y-%m-%d %H:%M:%S" );
	return ss.str();
}

ResultCode Logger::initialize( const std::string& filename, bool logToFile, bool verboseLogging )
{
	toFile  = logToFile;
	verbose = verboseLogging;

	if( toFile )
	{
		if( logFile.is_open() )
		{
			logFile.close();
		}

		logFile.open( filename, std::ios::out | std::ios::app );
		if( !logFile.is_open() )
		{
			return ERROR_FILE_OPEN_FAILED;
		}

		log( INFO, "Journalisation démarrée" );
	}

	return SUCCESS;
}

void Logger::log( LogLevel level, const std::string& message )
{
	if( level == DEBUG && !verbose )
	{
		return;
	}

	std::string levelStr;
	switch( level )
	{
		case INFO:
			levelStr = "INFO";
			break;
		case WARNING:
			levelStr = "WARN";
			break;
		case ERROR:
			levelStr = "ERROR";
			break;
		case DEBUG:
			levelStr = "DEBUG";
			break;
	}

	std::string logMessage = getCurrentTimestamp() + " [" + levelStr + "] " + message;

	// Affichage console
	printf( "%s\n", logMessage.c_str() );

	// Écriture dans le fichier
	if( toFile && logFile.is_open() )
	{
		logFile << logMessage << std::endl;
		logFile.flush();
	}
}

void Logger::clearLog()
{
	if( toFile )
	{
		if( logFile.is_open() )
		{
			logFile.close();
		}

		logFile.open( logFile.getloc().name().c_str(), std::ios::out | std::ios::trunc );
		if( logFile.is_open() )
		{
			log( INFO, "Journal effacé" );
		}
	}
}

void Logger::setVerbose( bool verboseLogging )
{
	verbose = verboseLogging;
}

void Logger::setLogToFile( bool logToFile, const std::string& filename )
{
	toFile = logToFile;

	if( toFile )
	{
		if( logFile.is_open() && logFile.getloc().name() != filename )
		{
			logFile.close();
		}

		if( !logFile.is_open() )
		{
			logFile.open( filename, std::ios::out | std::ios::app );
			if( logFile.is_open() )
			{
				log( INFO, "Journalisation vers fichier démarrée: " + filename );
			}
		}
	}
	else if( logFile.is_open() )
	{
		log( INFO, "Journalisation vers fichier arrêtée" );
		logFile.close();
	}
}

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE RECORDINGMANAGER
// =============================================================================

RecordingManager::RecordingManager() : isRecording( false ), currentTime( 0.0f )
{
}

ResultCode RecordingManager::startRecording()
{
	if( isRecording )
	{
		return ERROR_RECORDING_ALREADY_ACTIVE;
	}

	recordedData.clear();
	startTime   = std::chrono::steady_clock::now();
	isRecording = true;
	currentTime = 0.0f;
	return SUCCESS;
}

ResultCode RecordingManager::stopRecording()
{
	if( !isRecording )
	{
		return ERROR_RECORDING_NOT_ACTIVE;
	}

	isRecording = false;
	return SUCCESS;
}

bool RecordingManager::getIsRecording() const
{
	return isRecording;
}

float RecordingManager::getRecordingDuration() const
{
	return currentTime;
}

void RecordingManager::addSnapshot( const std::vector<AxisInfo>& axes, const std::vector<bool>& buttons )
{
	if( !isRecording )
	{
		return;
	}

	auto   now     = std::chrono::steady_clock::now();
	double elapsed = std::chrono::duration<double>( now - startTime ).count();
	currentTime    = static_cast<float>( elapsed );

	InputSnapshot snapshot;
	snapshot.timestamp = elapsed;

	// Enregistrer les valeurs des axes
	for( const auto& axis : axes )
	{
		snapshot.axisValues.push_back( axis.value );
	}

	// Enregistrer les états des boutons
	snapshot.buttonStates = buttons;

	recordedData.push_back( snapshot );
}

bool RecordingManager::getSnapshotAtTime( float time, InputSnapshot& outSnapshot )
{
	if( recordedData.empty() )
	{
		return false;
	}

	// Trouver le snapshot le plus proche du temps demandé
	auto it = std::lower_bound(
		recordedData.begin(),
		recordedData.end(),
		time,
		[]( const InputSnapshot& snapshot, double t )
		{
			return snapshot.timestamp < t;
		}
	);

	if( it == recordedData.end() )
	{
		outSnapshot = recordedData.back();
	}
	else if( it == recordedData.begin() )
	{
		outSnapshot = *it;
	}
	else
	{
		auto prevIt = it - 1;

		// Interpolation linéaire entre les deux snapshots les plus proches
		double t1     = prevIt->timestamp;
		double t2     = it->timestamp;
		double factor = ( time - t1 ) / ( t2 - t1 );

		outSnapshot.timestamp = time;
		outSnapshot.axisValues.resize( it->axisValues.size() );
		outSnapshot.buttonStates = it->buttonStates; // Les boutons ne sont pas interpolés

		for( size_t i = 0; i < it->axisValues.size(); ++i )
		{
			outSnapshot.axisValues[i] = static_cast<float>(
				prevIt->axisValues[i] + factor * ( it->axisValues[i] - prevIt->axisValues[i] )
			);
		}
	}

	return true;
}

ResultCode RecordingManager::saveRecording( const std::string& filename )
{
	if( recordedData.empty() )
	{
		return ERROR_INVALID_PARAMETER;
	}

	std::ofstream file( filename, std::ios::binary );
	if( !file.is_open() )
	{
		return ERROR_FILE_OPEN_FAILED;
	}

	// Écrire le nombre d'instantanés
	size_t count = recordedData.size();
	file.write( reinterpret_cast<const char*>( &count ), sizeof( count ) );

	// Écrire les dimensions (nombre d'axes et de boutons)
	if( !recordedData.empty() )
	{
		size_t axisCount   = recordedData[0].axisValues.size();
		size_t buttonCount = recordedData[0].buttonStates.size();
		file.write( reinterpret_cast<const char*>( &axisCount ), sizeof( axisCount ) );
		file.write( reinterpret_cast<const char*>( &buttonCount ), sizeof( buttonCount ) );
	}

	// Écrire les données
	for( const auto& snapshot : recordedData )
	{
		file.write( reinterpret_cast<const char*>( &snapshot.timestamp ), sizeof( snapshot.timestamp ) );

		for( const auto& axisValue : snapshot.axisValues )
		{
			file.write( reinterpret_cast<const char*>( &axisValue ), sizeof( axisValue ) );
		}

		for( const auto& buttonState : snapshot.buttonStates )
		{
			file.write( reinterpret_cast<const char*>( &buttonState ), sizeof( buttonState ) );
		}
	}

	if( file.fail() )
	{
		return ERROR_FILE_WRITE_FAILED;
	}

	file.close();
	return SUCCESS;
}

ResultCode RecordingManager::loadRecording( const std::string& filename )
{
	std::ifstream file( filename, std::ios::binary );
	if( !file.is_open() )
	{
		return ERROR_FILE_OPEN_FAILED;
	}

	recordedData.clear();

	// Lire le nombre d'instantanés
	size_t count = 0;
	file.read( reinterpret_cast<char*>( &count ), sizeof( count ) );

	// Lire les dimensions
	size_t axisCount   = 0;
	size_t buttonCount = 0;
	file.read( reinterpret_cast<char*>( &axisCount ), sizeof( axisCount ) );
	file.read( reinterpret_cast<char*>( &buttonCount ), sizeof( buttonCount ) );

	// Lire les données
	for( size_t i = 0; i < count; ++i )
	{
		InputSnapshot snapshot;

		file.read( reinterpret_cast<char*>( &snapshot.timestamp ), sizeof( snapshot.timestamp ) );

		snapshot.axisValues.resize( axisCount );
		for( size_t j = 0; j < axisCount; ++j )
		{
			file.read( reinterpret_cast<char*>( &snapshot.axisValues[j] ), sizeof( float ) );
		}

		snapshot.buttonStates.resize( buttonCount );
		for( size_t j = 0; j < buttonCount; ++j )
		{
			file.read( reinterpret_cast<char*>( &snapshot.buttonStates[j] ), sizeof( bool ) );
		}

		recordedData.push_back( snapshot );
	}

	if( file.fail() )
	{
		recordedData.clear();
		return ERROR_FILE_READ_FAILED;
	}

	file.close();

	if( !recordedData.empty() )
	{
		currentTime = static_cast<float>( recordedData.back().timestamp );
	}

	return SUCCESS;
}

float RecordingManager::getTotalDuration() const
{
	if( recordedData.empty() )
	{
		return 0.0f;
	}
	return static_cast<float>( recordedData.back().timestamp );
}

bool RecordingManager::hasRecording() const
{
	return !recordedData.empty();
}

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE AUTOTESTMANAGER
// =============================================================================

AutoTestManager::AutoTestManager() : isRunning( false ), startTime( 0.0f ), currentTime( 0.0f ),
									 duration( 30.0f ), testButtons( true ), testAxes( true ), testFFB( false )
{
}

ResultCode AutoTestManager::startTest( float testDuration, bool buttons, bool axes, bool ffb )
{
	duration    = testDuration;
	testButtons = buttons;
	testAxes    = axes;
	testFFB     = ffb;

	isRunning   = true;
	startTime   = ImGui::GetTime();
	currentTime = 0.0f;

	return SUCCESS;
}

ResultCode AutoTestManager::stopTest()
{
	isRunning = false;
	return SUCCESS;
}

bool AutoTestManager::isTestRunning() const
{
	return isRunning;
}

float AutoTestManager::getTestDuration() const
{
	return duration;
}

float AutoTestManager::getCurrentTime() const
{
	return currentTime;
}

float AutoTestManager::getAxisValue( int axisIndex, float time )
{
	switch( axisIndex )
	{
		case 0: // Volant - mouvement sinusoïdal
			return sinf( time * 2.0f ) * 0.8f;

		case 1: // Accélérateur - accélération progressive
			return std::min( 1.0f, time / ( duration * 0.2f ) );

		case 2: // Frein - pression périodique
			return ( sinf( time * 1.5f ) + 1.0f ) * 0.5f;

		case 3: // Embrayage - plusieurs embrayages rapides
			return ( time - floorf( time * 0.5f ) * 2.0f ) < 1.0f ? 0.0f : 1.0f;

		default: // Autres axes - mouvement aléatoire
			return sinf( time * 0.5f + axisIndex * 1.3f ) * 0.8f;
	}
}

bool AutoTestManager::getButtonState( int buttonIndex, float time )
{
	// Différents patterns selon le groupe de boutons
	if( buttonIndex < 6 )
	{
		// Groupe 1: séquence
		int cycle = static_cast<int>( time * 2.0f ) % 6;
		return buttonIndex == cycle;
	}
	else if( buttonIndex < 12 )
	{
		// Groupe 2: alternance
		return fmodf( time + buttonIndex * 0.7f, 2.0f ) < 1.0f;
	}
	else if( buttonIndex < 18 )
	{
		// Groupe 3: courts appuis périodiques
		return fmodf( time * 3.0f + buttonIndex, 10.0f ) < 0.3f;
	}
	else
	{
		// Groupe 4: longs appuis périodiques
		return fmodf( time + buttonIndex * 0.5f, 5.0f ) < 2.5f;
	}
}

void AutoTestManager::update( std::vector<AxisInfo>& axes, std::vector<bool>& buttons, std::vector<ForceEffect>& effects )
{
	if( !isRunning )
	{
		return;
	}

	float now   = ImGui::GetTime();
	currentTime = now - startTime;

	// Vérifier si le test est terminé
	if( currentTime >= duration )
	{
		isRunning = false;
		return;
	}

	// Mettre à jour les axes
	if( testAxes )
	{
		for( size_t i = 0; i < axes.size(); ++i )
		{
			if( i < axes.size() )
			{
				axes[i].value = getAxisValue( i, currentTime );
			}
		}
	}

	// Mettre à jour les boutons
	if( testButtons )
	{
		for( size_t i = 0; i < buttons.size(); ++i )
		{
			if( i < buttons.size() )
			{
				buttons[i] = getButtonState( i, currentTime );
			}
		}
	}

	// Mettre à jour les effets de force
	if( testFFB )
	{
		// Cycle à travers les différents effets
		size_t effectIndex = static_cast<size_t>( floorf( currentTime / 2.0f ) ) % effects.size();

		// Désactiver tous les effets
		for( auto& effect : effects )
		{
			effect.enabled = false;
		}

		// Activer l'effet courant
		if( effectIndex < effects.size() )
		{
			effects[effectIndex].enabled = true;
			// Moduler la force (0-100%)
			effects[effectIndex].strength = fabs( sinf( currentTime * 2.0f ) ) * 100.0f;
		}
	}
}

void AutoTestManager::setTestOptions( bool buttons, bool axes, bool ffb, float testDuration )
{
	testButtons = buttons;
	testAxes    = axes;
	testFFB     = ffb;
	duration    = testDuration;
}

bool AutoTestManager::getTestButtons() const
{
	return testButtons;
}

bool AutoTestManager::getTestAxes() const
{
	return testAxes;
}

bool AutoTestManager::getTestFFB() const
{
	return testFFB;
}

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE FORCEMANAGER
// =============================================================================

ForceManager::ForceManager() : ffbEnabled( true ), masterStrength( 100.0f ), haptic( nullptr )
{
	// Initialiser les effets par défaut
	effects = {
		{ "Constant", false, 50.0f, 1.0f, -1 },
		{ "Ressort", false, 70.0f, 0.0f, -1 },
		{ "Amortissement", false, 60.0f, 0.0f, -1 },
		{ "Friction", false, 40.0f, 0.0f, -1 },
		{ "Sinusoïdal", false, 30.0f, 2.0f, -1 },
		{ "Dent de scie", false, 50.0f, 1.5f, -1 },
		{ "Rumbble", false, 80.0f, 0.5f, -1 },
		{ "Choc", false, 100.0f, 0.2f, -1 }
	};
}

ForceManager::~ForceManager()
{
	cleanup();
}

void ForceManager::cleanup()
{
	if( haptic )
	{
		// Désactiver tous les effets
		for( auto& effect : effects )
		{
			if( effect.effectId >= 0 )
			{
				SDL_HapticDestroyEffect( haptic, effect.effectId );
				effect.effectId = -1;
			}
		}

		SDL_HapticClose( haptic );
		haptic = nullptr;
	}
}

ResultCode ForceManager::initialize( SDL_Joystick* joystick, Logger& logger )
{
	cleanup();

	if( !joystick )
	{
		logger.log( Logger::ERROR, "Impossible d'initialiser le retour de force: joystick invalide" );
		return ERROR_JOYSTICK_NOT_FOUND;
	}

	// Vérifier si le joystick supporte le haptic
	if( !SDL_JoystickIsHaptic( joystick ) )
	{
		logger.log( Logger::WARNING, "Le joystick ne supporte pas le retour de force" );
		return ERROR_JOYSTICK_NOT_FOUND;
	}

	// Ouvrir le dispositif haptic
	haptic = SDL_HapticOpenFromJoystick( joystick );
	if( !haptic )
	{
		logger.log( Logger::ERROR, "Impossible d'ouvrir le dispositif haptic: " + std::string( SDL_GetError() ) );
		return ERROR_JOYSTICK_NOT_FOUND;
	}

	// Vérifier les capacités
	unsigned int supportedEffects = SDL_HapticQuery( haptic );

	logger.log( Logger::INFO, "Initialisation du retour de force réussie" );
	logger.log( Logger::DEBUG, "Effets supportés: " + std::to_string( supportedEffects ) );

	// Initialiser les effets supportés
	if( supportedEffects & SDL_HAPTIC_CONSTANT )
	{
		initConstantEffect( 0 );
		logger.log( Logger::DEBUG, "Effet constant initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_SPRING )
	{
		initSpringEffect( 1 );
		logger.log( Logger::DEBUG, "Effet ressort initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_DAMPER )
	{
		initDamperEffect( 2 );
		logger.log( Logger::DEBUG, "Effet amortissement initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_FRICTION )
	{
		initFrictionEffect( 3 );
		logger.log( Logger::DEBUG, "Effet friction initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_SINE )
	{
		initSineEffect( 4 );
		logger.log( Logger::DEBUG, "Effet sinusoïdal initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_SAWTOOTHUP )
	{
		initSawtoothEffect( 5 );
		logger.log( Logger::DEBUG, "Effet dent de scie initialisé" );
	}

	if( supportedEffects & SDL_HAPTIC_LEFTRIGHT )
	{
		initRumbleEffect( 6 );
		logger.log( Logger::DEBUG, "Effet rumble initialisé" );
	}

	return SUCCESS;
}

void ForceManager::initConstantEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                      = SDL_HAPTIC_CONSTANT;
	effect.constant.direction.type   = SDL_HAPTIC_POLAR;
	effect.constant.direction.dir[0] = 0;
	effect.constant.length           = static_cast<Uint32>( effects[index].duration * 1000 );
	effect.constant.level            = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
	effect.constant.attack_length    = 0;
	effect.constant.fade_length      = 0;

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initSpringEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                       = SDL_HAPTIC_SPRING;
	effect.condition.direction.type   = SDL_HAPTIC_POLAR;
	effect.condition.direction.dir[0] = 0;
	effect.condition.length           = SDL_HAPTIC_INFINITY;

	// Paramètres spécifiques à l'effet ressort
	for( int i = 0; i < SDL_HapticNumAxes( haptic ) && i < 3; ++i )
	{
		effect.condition.right_sat[i]   = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_sat[i]    = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.right_coeff[i] = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_coeff[i]  = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.center[i]      = 0;
		effect.condition.deadband[i]    = 0;
	}

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initDamperEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                       = SDL_HAPTIC_DAMPER;
	effect.condition.direction.type   = SDL_HAPTIC_POLAR;
	effect.condition.direction.dir[0] = 0;
	effect.condition.length           = SDL_HAPTIC_INFINITY;

	// Paramètres spécifiques à l'effet amortissement
	for( int i = 0; i < SDL_HapticNumAxes( haptic ) && i < 3; ++i )
	{
		effect.condition.right_sat[i]   = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_sat[i]    = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.right_coeff[i] = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_coeff[i]  = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.center[i]      = 0;
		effect.condition.deadband[i]    = 0;
	}

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initFrictionEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                       = SDL_HAPTIC_FRICTION;
	effect.condition.direction.type   = SDL_HAPTIC_POLAR;
	effect.condition.direction.dir[0] = 0;
	effect.condition.length           = SDL_HAPTIC_INFINITY;

	// Paramètres spécifiques à l'effet friction
	for( int i = 0; i < SDL_HapticNumAxes( haptic ) && i < 3; ++i )
	{
		effect.condition.right_sat[i]   = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_sat[i]    = static_cast<Uint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.right_coeff[i] = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.left_coeff[i]  = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
		effect.condition.center[i]      = 0;
		effect.condition.deadband[i]    = 0;
	}

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initSineEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                      = SDL_HAPTIC_SINE;
	effect.periodic.direction.type   = SDL_HAPTIC_POLAR;
	effect.periodic.direction.dir[0] = 0;
	effect.periodic.length           = static_cast<Uint32>( effects[index].duration * 1000 );
	effect.periodic.period           = 100;
	effect.periodic.magnitude        = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
	effect.periodic.attack_length    = 0;
	effect.periodic.fade_length      = 0;

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initSawtoothEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                      = SDL_HAPTIC_SAWTOOTHUP;
	effect.periodic.direction.type   = SDL_HAPTIC_POLAR;
	effect.periodic.direction.dir[0] = 0;
	effect.periodic.length           = static_cast<Uint32>( effects[index].duration * 1000 );
	effect.periodic.period           = 250;
	effect.periodic.magnitude        = static_cast<Sint16>( 32767.0f * effects[index].strength / 100.0f );
	effect.periodic.attack_length    = 0;
	effect.periodic.fade_length      = 0;

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::initRumbleEffect( int index )
{
	if( !haptic || index >= effects.size() )
		return;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                      = SDL_HAPTIC_LEFTRIGHT;
	effect.leftright.length          = static_cast<Uint32>( effects[index].duration * 1000 );
	effect.leftright.large_magnitude = static_cast<Uint16>( 65535.0f * effects[index].strength / 100.0f );
	effect.leftright.small_magnitude = static_cast<Uint16>( 65535.0f * effects[index].strength / 100.0f );

	// Créer l'effet
	if( effects[index].effectId >= 0 )
	{
		SDL_HapticDestroyEffect( haptic, effects[index].effectId );
	}

	effects[index].effectId = SDL_HapticNewEffect( haptic, &effect );
}

void ForceManager::simulateCollision( float strength, Logger& logger )
{
	if( !haptic || !ffbEnabled )
	{
		logger.log( Logger::DEBUG, "Simulation de collision ignorée: haptic non disponible ou désactivé" );
		return;
	}

	// Multiplier par la force globale
	strength = strength * masterStrength / 100.0f;

	SDL_HapticEffect effect;
	memset( &effect, 0, sizeof( SDL_HapticEffect ) );

	effect.type                      = SDL_HAPTIC_CONSTANT;
	effect.constant.direction.type   = SDL_HAPTIC_POLAR;
	effect.constant.direction.dir[0] = 0;
	effect.constant.length           = 200; // 200ms
	effect.constant.level            = static_cast<Sint16>( 32767.0f * strength / 100.0f );
	effect.constant.attack_length    = 0;
	effect.constant.fade_length      = 100;

	int effectId = SDL_HapticNewEffect( haptic, &effect );
	if( effectId < 0 )
	{
		logger.log( Logger::ERROR, "Impossible de créer l'effet de collision: " + std::string( SDL_GetError() ) );
		return;
	}

	SDL_HapticRunEffect( haptic, effectId, 1 );
	SDL_Delay( 10 ); // Petit délai pour s'assurer que l'effet démarre

	// L'effet sera détruit après sa durée
	logger.log( Logger::DEBUG, "Collision simulée avec force: " + std::to_string( strength ) );

	// Destruction différée de l'effet (après sa durée + marge)
	SDL_TimerID timer = SDL_AddTimer( 300, []( Uint32 interval, void* param ) -> Uint32
	{
		int* pEffectId = static_cast<int*>( param );
		SDL_HapticDestroyEffect( SDL_HapticOpen( 0 ), *pEffectId );
		delete pEffectId;
		return 0; // Ne pas répéter le timer
	}, new int( effectId ) );
}

void ForceManager::playEffect( int index, Logger& logger )
{
	if( !haptic || !ffbEnabled || index >= effects.size() )
	{
		logger.log( Logger::DEBUG, "Lecture d'effet ignorée: haptic non disponible ou désactivé" );
		return;
	}

	auto& effect = effects[index];

	if( effect.effectId < 0 )
	{
		logger.log( Logger::WARNING, "Effet " + effect.name + " non initialisé" );
		return;
	}

	// Mettre à jour l'effet avec les paramètres actuels
	updateEffect( index );

	// Jouer l'effet
	SDL_HapticRunEffect( haptic, effect.effectId, 1 );
	logger.log( Logger::DEBUG, "Effet " + effect.name + " joué avec force: " + std::to_string( effect.strength * masterStrength / 100.0f ) );
}

void ForceManager::updateEffect( int index )
{
	if( !haptic || index >= effects.size() || effects[index].effectId < 0 )
	{
		return;
	}

	auto& effect           = effects[index];
	float adjustedStrength = effect.strength * masterStrength / 100.0f;

	SDL_HapticEffect sdlEffect;
	memset( &sdlEffect, 0, sizeof( SDL_HapticEffect ) );

	if( SDL_HapticGetEffectStatus( haptic, effect.effectId ) >= 0 )
	{
		switch( index )
		{
			case 0: // Constant
				sdlEffect.type = SDL_HAPTIC_CONSTANT;
				sdlEffect.constant.direction.type   = SDL_HAPTIC_POLAR;
				sdlEffect.constant.direction.dir[0] = 0;
				sdlEffect.constant.length           = static_cast<Uint32>( effect.duration * 1000 );
				sdlEffect.constant.level            = static_cast<Sint16>( 32767.0f * adjustedStrength / 100.0f );
				sdlEffect.constant.attack_length    = 0;
				sdlEffect.constant.fade_length      = 0;
				break;

			case 1: // Ressort
			case 2: // Amortissement
			case 3: // Friction
			{
				Uint16 type;
				switch( index )
				{
					case 1:
						type = SDL_HAPTIC_SPRING;
						break;
					case 2:
						type = SDL_HAPTIC_DAMPER;
						break;
					case 3:
						type = SDL_HAPTIC_FRICTION;
						break;
					default:
						type = SDL_HAPTIC_SPRING;
						break;
				}

				sdlEffect.type                       = type;
				sdlEffect.condition.direction.type   = SDL_HAPTIC_POLAR;
				sdlEffect.condition.direction.dir[0] = 0;
				sdlEffect.condition.length           = SDL_HAPTIC_INFINITY;

				for( int i = 0; i < SDL_HapticNumAxes( haptic ) && i < 3; ++i )
				{
					sdlEffect.condition.right_sat[i]   = static_cast<Uint16>( 32767.0f * adjustedStrength / 100.0f );
					sdlEffect.condition.left_sat[i]    = static_cast<Uint16>( 32767.0f * adjustedStrength / 100.0f );
					sdlEffect.condition.right_coeff[i] = static_cast<Sint16>( 32767.0f * adjustedStrength / 100.0f );
					sdlEffect.condition.left_coeff[i]  = static_cast<Sint16>( 32767.0f * adjustedStrength / 100.0f );
				}
				break;
			}

			case 4: // Sinusoïdal
			case 5: // Dent de scie
			{
				Uint16 type;
				switch( index )
				{
					case 4:
						type = SDL_HAPTIC_SINE;
						break;
					case 5:
						type = SDL_HAPTIC_SAWTOOTHUP;
						break;
					default:
						type = SDL_HAPTIC_SINE;
						break;
				}

				sdlEffect.type                      = type;
				sdlEffect.periodic.direction.type   = SDL_HAPTIC_POLAR;
				sdlEffect.periodic.direction.dir[0] = 0;
				sdlEffect.periodic.length           = static_cast<Uint32>( effect.duration * 1000 );
				sdlEffect.periodic.period           = index == 4 ? 100 : 250;
				sdlEffect.periodic.magnitude        = static_cast<Sint16>( 32767.0f * adjustedStrength / 100.0f );
				break;
			}

			case 6: // Rumble
				sdlEffect.type = SDL_HAPTIC_LEFTRIGHT;
				sdlEffect.leftright.length          = static_cast<Uint32>( effect.duration * 1000 );
				sdlEffect.leftright.large_magnitude = static_cast<Uint16>( 65535.0f * adjustedStrength / 100.0f );
				sdlEffect.leftright.small_magnitude = static_cast<Uint16>( 65535.0f * adjustedStrength / 100.0f );
				break;

			case 7: // Choc
				sdlEffect.type = SDL_HAPTIC_CONSTANT;
				sdlEffect.constant.direction.type   = SDL_HAPTIC_POLAR;
				sdlEffect.constant.direction.dir[0] = 0;
				sdlEffect.constant.length           = static_cast<Uint32>( effect.duration * 1000 );
				sdlEffect.constant.level            = static_cast<Sint16>( 32767.0f * adjustedStrength / 100.0f );
				sdlEffect.constant.attack_length    = 0;
				sdlEffect.constant.fade_length      = static_cast<Uint16>( effect.duration * 500 );
				break;
		}

		SDL_HapticUpdateEffect( haptic, effect.effectId, &sdlEffect );
	}
}

void ForceManager::stopAllEffects()
{
	if( !haptic )
	{
		return;
	}

	SDL_HapticStopAll( haptic );

	// Désactiver tous les effets dans le modèle
	for( auto& effect : effects )
	{
		effect.enabled = false;
	}
}

void ForceManager::update( Logger& logger )
{
	if( !haptic || !ffbEnabled )
	{
		return;
	}

	// Jouer tous les effets actifs
	for( size_t i = 0; i < effects.size(); ++i )
	{
		auto& effect = effects[i];

		if( effect.enabled && effect.effectId >= 0 )
		{
			// Vérifier si l'effet est déjà en cours
			if( SDL_HapticGetEffectStatus( haptic, effect.effectId ) == 0 )
			{
				// Mettre à jour et jouer l'effet
				updateEffect( i );
				SDL_HapticRunEffect( haptic, effect.effectId, 1 );
				logger.log( Logger::DEBUG, "Effet " + effect.name + " activé" );
			}
		}
		else if( !effect.enabled && effect.effectId >= 0 )
		{
			// Arrêter l'effet s'il est actif
			if( SDL_HapticGetEffectStatus( haptic, effect.effectId ) > 0 )
			{
				SDL_HapticStopEffect( haptic, effect.effectId );
				logger.log( Logger::DEBUG, "Effet " + effect.name + " désactivé" );
			}
		}
	}
}

bool ForceManager::isFFBEnabled() const
{
	return ffbEnabled;
}

void ForceManager::setFFBEnabled( bool enabled )
{
	ffbEnabled = enabled;

	if( !enabled && haptic )
	{
		SDL_HapticStopAll( haptic );
	}
}

float ForceManager::getMasterStrength() const
{
	return masterStrength;
}

void ForceManager::setMasterStrength( float strength )
{
	masterStrength = strength;

	// Mettre à jour tous les effets
	for( size_t i = 0; i < effects.size(); ++i )
	{
		updateEffect( i );
	}
}

std::vector<ForceEffect>& ForceManager::getEffects()
{
	return effects;
}

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE DEVICEMANAGER
// =============================================================================

DeviceManager::DeviceManager() : joystick( nullptr ), haptic( nullptr ), usingDemo( true )
{
	// Initialiser les informations de démo
	wheelInfo = {
		"Logitech G29 Racing Wheel", 0, 24, 6, true
	};

	// Axes de démo
	axes = {
		{ "Volant", 0.0f, -1.0f, 1.0f, 0.05f, 0.0f },
		{ "Accélérateur", 0.0f, 0.0f, 1.0f, 0.1f, 0.0f },
		{ "Frein", 0.0f, 0.0f, 1.0f, 0.1f, 0.0f },
		{ "Embrayage", 0.0f, 0.0f, 1.0f, 0.1f, 0.0f },
		{ "Levier de vitesse X", 0.0f, -1.0f, 1.0f, 0.1f, 0.0f },
		{ "Levier de vitesse Y", 0.0f, -1.0f, 1.0f, 0.1f, 0.0f }
	};

	// Boutons de démo
	buttons.resize( 24, false );
}

DeviceManager::~DeviceManager()
{
	cleanup();
}

void DeviceManager::cleanup()
{
	if( haptic )
	{
		SDL_HapticClose( haptic );
		haptic = nullptr;
	}

	if( joystick )
	{
		SDL_JoystickClose( joystick );
		joystick = nullptr;
	}
}

ResultCode DeviceManager::initialize( Logger& logger )
{
	cleanup();

	// Rechercher des joysticks
	int numJoysticks = SDL_NumJoysticks();
	if( numJoysticks <= 0 )
	{
		logger.log( Logger::WARNING, "Aucun joystick détecté, utilisation du mode démo" );
		usingDemo = true;
		return SUCCESS;
	}

	// Trouver un joystick qui ressemble à un volant
	for( int i = 0; i < numJoysticks; ++i )
	{
		if( SDL_IsGameController( i ) )
		{
			continue; // Ignorer les contrôleurs de jeu standard
		}

		joystick = SDL_JoystickOpen( i );
		if( !joystick )
		{
			logger.log( Logger::WARNING, "Impossible d'ouvrir le joystick " + std::to_string( i ) + ": " + SDL_GetError() );
			continue;
		}

		const char* name         = SDL_JoystickName( joystick );
		std::string joystickName = name ? name : "Unknown";

		// Rechercher des mots-clés de volant dans le nom
		std::string lowerName = joystickName;
		std::transform( lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower );

		if( lowerName.find( "wheel" ) != std::string::npos ||
			lowerName.find( "volant" ) != std::string::npos ||
			lowerName.find( "racing" ) != std::string::npos ||
			lowerName.find( "driving" ) != std::string::npos ||
			lowerName.find( "g29" ) != std::string::npos ||
			lowerName.find( "g920" ) != std::string::npos ||
			lowerName.find( "t300" ) != std::string::npos ||
			lowerName.find( "t500" ) != std::string::npos )
		{
			// C'est probablement un volant
			logger.log( Logger::INFO, "Volant détecté: " + joystickName );

			// Initialiser les informations du volant
			wheelInfo.name            = joystickName;
			wheelInfo.id              = i;
			wheelInfo.buttonCount     = SDL_JoystickNumButtons( joystick );
			wheelInfo.axisCount       = SDL_JoystickNumAxes( joystick );
			wheelInfo.hasForceFeeback = SDL_JoystickIsHaptic( joystick );

			// Initialiser les axes et boutons
			axes.clear();
			for( int j = 0; j < wheelInfo.axisCount; ++j )
			{
				std::string axisName;

				// Nommer les axes en fonction de leur position habituelle
				switch( j )
				{
					case 0:
						axisName = "Volant";
						break;
					case 1:
						axisName = "Accélérateur";
						break;
					case 2:
						axisName = "Frein";
						break;
					case 3:
						axisName = "Embrayage";
						break;
					case 4:
						axisName = "Levier de vitesse X";
						break;
					case 5:
						axisName = "Levier de vitesse Y";
						break;
					default:
						axisName = "Axe " + std::to_string( j );
						break;
				}

				axes.push_back( { axisName, 0.0f, -1.0f, 1.0f, 0.05f, 0.0f } );
			}

			buttons.resize( wheelInfo.buttonCount, false );

			// Initialiser le retour de force
			if( wheelInfo.hasForceFeeback )
			{
				forceManager.initialize( joystick, logger );
			}

			usingDemo = false;
			return SUCCESS;
		}

		// Ce n'est pas un volant, fermer et passer au suivant
		SDL_JoystickClose( joystick );
		joystick = nullptr;
	}

	logger.log( Logger::WARNING, "Aucun volant détecté, utilisation du mode démo" );
	usingDemo = true;
	return SUCCESS;
}

void DeviceManager::updateInputs( RecordingManager& recorder, AutoTestManager& autoTest, Logger& logger )
{
	if( !usingDemo && joystick )
	{
		// Lire les entrées réelles du joystick
		for( int i = 0; i < wheelInfo.axisCount && i < axes.size(); ++i )
		{
			float rawValue   = SDL_JoystickGetAxis( joystick, i ) / 32767.0f;
			axes[i].rawValue = rawValue;

			// Appliquer la zone morte
			if( fabs( rawValue ) < axes[i].deadzone )
			{
				rawValue = 0.0f;
			}
			else
			{
				// Remapper la valeur après la zone morte
				float sign = rawValue < 0.0f ? -1.0f : 1.0f;
				rawValue   = sign * ( fabs( rawValue ) - axes[i].deadzone ) / ( 1.0f - axes[i].deadzone );
			}

			axes[i].value = rawValue;
		}

		for( int i = 0; i < wheelInfo.buttonCount && i < buttons.size(); ++i )
		{
			buttons[i] = SDL_JoystickGetButton( joystick, i ) == 1;
		}

		// Mettre à jour le retour de force
		forceManager.update( logger );
	}
	else if( autoTest.isTestRunning() )
	{
		// Mode test automatique
		autoTest.update( axes, buttons, forceManager.getEffects() );
	}
	else
	{
		// Mode démo - animation des axes
		static float time = 0.0f;
		time += ImGui::GetIO().DeltaTime;

		// Animation des axes
		if( !axes.empty() )
			axes[0].value = sinf( time ) * 0.8f; // Volant
		if( axes.size() > 1 )
			axes[1].value = ( sinf( time * 0.7f ) + 1.0f ) * 0.5f; // Accélérateur
		if( axes.size() > 2 )
			axes[2].value = ( cosf( time * 0.5f ) + 1.0f ) * 0.5f; // Frein

		// Mettre à jour les valeurs brutes
		for( auto& axis : axes )
		{
			axis.rawValue = axis.value;
		}

		// Animation des boutons
		static float lastToggleTime = 0.0f;
		float        currentTime    = ImGui::GetTime();
		if( currentTime - lastToggleTime > 1.0f )
		{
			int randomButton      = rand() % buttons.size();
			buttons[randomButton] = !buttons[randomButton];
			lastToggleTime        = currentTime;
		}
	}

	// Enregistrer les entrées si l'enregistrement est actif
	if( recorder.getIsRecording() )
	{
		recorder.addSnapshot( axes, buttons );
	}
}

ResultCode DeviceManager::reloadDevice( Logger& logger )
{
	return initialize( logger );
}

WheelInfo& DeviceManager::getWheelInfo()
{
	return wheelInfo;
}

std::vector<AxisInfo>& DeviceManager::getAxes()
{
	return axes;
}

std::vector<bool>& DeviceManager::getButtons()
{
	return buttons;
}

ForceManager& DeviceManager::getForceManager()
{
	return forceManager;
}

bool DeviceManager::isUsingDemo() const
{
	return usingDemo;
}

void DeviceManager::calibrateAxis( int axisIndex, Logger& logger )
{
	if( axisIndex < 0 || axisIndex >= axes.size() )
	{
		logger.log( Logger::ERROR, "Index d'axe invalide pour la calibration" );
		return;
	}

	logger.log( Logger::INFO, "Calibration de l'axe " + axes[axisIndex].name );

	if( usingDemo )
	{
		logger.log( Logger::WARNING, "Mode démo: calibration simulée" );
		return;
	}

	// Enregistrer les valeurs min/max pendant quelques secondes
	float minValue = 0.0f;
	float maxValue = 0.0f;
	bool  first    = true;

	// Durée de calibration: 5 secondes
	float startTime = ImGui::GetTime();
	float endTime   = startTime + 5.0f;

	logger.log( Logger::INFO, "Déplacez l'axe " + axes[axisIndex].name + " dans toutes les positions pendant 5 secondes..." );

	while( ImGui::GetTime() < endTime )
	{
		SDL_Event event;
		while( SDL_PollEvent( &event ) )
		{
			// Traiter les événements pour éviter que SDL ne se bloque
		}

		if( joystick )
		{
			float value = SDL_JoystickGetAxis( joystick, axisIndex ) / 32767.0f;

			if( first )
			{
				minValue = maxValue = value;
				first    = false;
			}
			else
			{
				minValue = std::min( minValue, value );
				maxValue = std::max( maxValue, value );
			}
		}

		SDL_Delay( 10 );
	}

	// Appliquer les nouvelles valeurs avec une marge
	if( !first )
	{
		axes[axisIndex].min = minValue - 0.05f;
		axes[axisIndex].max = maxValue + 0.05f;
		logger.log( Logger::INFO, "Calibration terminée: min=" + std::to_string( axes[axisIndex].min ) + ", max=" + std::to_string( axes[axisIndex].max ) );
	}
	else
	{
		logger.log( Logger::WARNING, "Calibration échouée: aucune donnée collectée" );
	}
}

// =============================================================================
// IMPLÉMENTATION DE LA CLASSE WHEELTESTAPP
// =============================================================================

WheelTestApp::WheelTestApp() : testMode( 0 ), collisionStrength( 80.0f ), configFilename( "wheel_config.json" )
{
	// Initialiser la configuration par défaut
	config.logFilename         = "wheel_test.log";
	config.logToFile           = false;
	config.verboseLogging      = false;
	config.updateFrequency     = 0.0f;
	config.masterForceStrength = 100.0f;
	config.ffbEnabled          = true;

	strncpy( logFilename, config.logFilename.c_str(), sizeof( logFilename ) - 1 );
}

WheelTestApp::~WheelTestApp()
{
	deviceManager.cleanup();
}

ResultCode WheelTestApp::initialize()
{
	// Initialiser le logger
	ResultCode result = logger.initialize( config.logFilename, config.logToFile, config.verboseLogging );
	if( result != SUCCESS )
	{
		return result;
	}

	logger.log( Logger::INFO, "Test de volant démarré" );

	// Initialiser le gestionnaire de périphérique
	result = deviceManager.initialize( logger );
	if( result != SUCCESS )
	{
		return result;
	}

	// Initialiser le retour de force avec les paramètres de config
	deviceManager.getForceManager().setFFBEnabled( config.ffbEnabled );
	deviceManager.getForceManager().setMasterStrength( config.masterForceStrength );

	// Charger la configuration si elle existe
	loadConfig();

	return SUCCESS;
}

void WheelTestApp::update()
{
	// Mettre à jour les entrées du périphérique
	deviceManager.updateInputs( recordingManager, autoTestManager, logger );
}

ResultCode WheelTestApp::loadConfig()
{
	std::ifstream file( configFilename );
	if( !file.is_open() )
	{
		logger.log( Logger::WARNING, "Fichier de configuration non trouvé: " + configFilename );
		return ERROR_FILE_OPEN_FAILED;
	}

	// Format simplifié pour cet exemple
	std::string line;
	while( std::getline( file, line ) )
	{
		size_t pos = line.find( '=' );
		if( pos != std::string::npos )
		{
			std::string key   = line.substr( 0, pos );
			std::string value = line.substr( pos + 1 );

			if( key == "logFilename" )
			{
				config.logFilename = value;
				strncpy( logFilename, value.c_str(), sizeof( logFilename ) - 1 );
			}
			else if( key == "logToFile" )
			{
				config.logToFile = ( value == "true" || value == "1" );
			}
			else if( key == "verboseLogging" )
			{
				config.verboseLogging = ( value == "true" || value == "1" );
			}
			else if( key == "updateFrequency" )
			{
				config.updateFrequency = std::stof( value );
			}
			else if( key == "masterForceStrength" )
			{
				config.masterForceStrength = std::stof( value );
				deviceManager.getForceManager().setMasterStrength( config.masterForceStrength );
			}
			else if( key == "ffbEnabled" )
			{
				config.ffbEnabled = ( value == "true" || value == "1" );
				deviceManager.getForceManager().setFFBEnabled( config.ffbEnabled );
			}
		}
	}

	logger.log( Logger::INFO, "Configuration chargée depuis " + configFilename );
	logger.setLogToFile( config.logToFile, config.logFilename );
	logger.setVerbose( config.verboseLogging );

	return SUCCESS;
}

ResultCode WheelTestApp::saveConfig()
{
	std::ofstream file( configFilename );
	if( !file.is_open() )
	{
		logger.log( Logger::ERROR, "Impossible de créer le fichier de configuration: " + configFilename );
		return ERROR_FILE_OPEN_FAILED;
	}

	// Mettre à jour la config avec les valeurs actuelles
	config.logFilename         = logFilename;
	config.logToFile           = logger.isLogToFile();
	config.verboseLogging      = logger.isVerbose();
	config.masterForceStrength = deviceManager.getForceManager().getMasterStrength();
	config.ffbEnabled          = deviceManager.getForceManager().isFFBEnabled();

	// Format simplifié pour cet exemple
	file << "logFilename=" << config.logFilename << std::endl;
	file << "logToFile=" << ( config.logToFile ? "true" : "false" ) << std::endl;
	file << "verboseLogging=" << ( config.verboseLogging ? "true" : "false" ) << std::endl;
	file << "updateFrequency=" << config.updateFrequency << std::endl;
	file << "masterForceStrength=" << config.masterForceStrength << std::endl;
	file << "ffbEnabled=" << ( config.ffbEnabled ? "true" : "false" ) << std::endl;

	logger.log( Logger::INFO, "Configuration sauvegardée dans " + configFilename );

	return SUCCESS;
}

std::string WheelTestApp::getCurrentTimeString()
{
	auto              now  = std::chrono::system_clock::now();
	auto              time = std::chrono::system_clock::to_time_t( now );
	std::stringstream ss;
	ss << std::put_time( std::localtime( &time ), "%Y%m%d_%H%M%S" );
	return ss.str();
}

void WheelTestApp::renderUI( SDL_Window* window )
{
	// Récupérer la taille de la fenêtre SDL
	int windowWidth, windowHeight;
	SDL_GetWindowSize( window, &windowWidth, &windowHeight );

	// Définir les couleurs de l'application
	ImVec4 primaryColor     = ImVec4( 0.07f, 0.13f, 0.17f, 1.0f ); // Bleu foncé
	ImVec4 secondaryColor   = ImVec4( 0.11f, 0.22f, 0.33f, 1.0f ); // Bleu intermédiaire
	ImVec4 accentColor      = ImVec4( 0.0f, 0.47f, 0.84f, 1.0f );  // Bleu vif
	ImVec4 accentLightColor = ImVec4( 0.2f, 0.6f, 1.0f, 1.0f );    // Bleu plus clair
	ImVec4 textColor        = ImVec4( 0.9f, 0.9f, 0.9f, 1.0f );    // Blanc cassé
	ImVec4 mutedTextColor   = ImVec4( 0.7f, 0.7f, 0.7f, 1.0f );    // Gris clair
	ImVec4 activeColor      = ImVec4( 0.1f, 0.75f, 0.4f, 1.0f );   // Vert
	ImVec4 activeHoverColor = ImVec4( 0.2f, 0.85f, 0.5f, 1.0f );   // Vert plus clair
	ImVec4 warningColor     = ImVec4( 1.0f, 0.6f, 0.0f, 1.0f );    // Orange
	ImVec4 errorColor       = ImVec4( 0.9f, 0.2f, 0.2f, 1.0f );    // Rouge

	// Appliquons un style global
	ImGuiStyle& style = ImGui::GetStyle();

	// Arrondir les éléments
	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 5.0f;
	style.PopupRounding     = 5.0f;
	style.ScrollbarRounding = 5.0f;
	style.GrabRounding      = 5.0f;
	style.TabRounding       = 5.0f;

	// Espacement et taille
	style.WindowPadding    = ImVec2( 12, 12 );
	style.FramePadding     = ImVec2( 8, 4 );
	style.ItemSpacing      = ImVec2( 10, 8 );
	style.ItemInnerSpacing = ImVec2( 8, 6 );
	style.IndentSpacing    = 22.0f;
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;

	// Bordures et couleurs
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize  = 1.0f;
	style.PopupBorderSize  = 1.0f;
	style.FrameBorderSize  = 1.0f;
	style.TabBorderSize    = 1.0f;

	// Couleurs du style
	style.Colors[ImGuiCol_Text]                 = textColor;
	style.Colors[ImGuiCol_TextDisabled]         = mutedTextColor;
	style.Colors[ImGuiCol_WindowBg]             = primaryColor;
	style.Colors[ImGuiCol_ChildBg]              = ImVec4( primaryColor.x + 0.03f, primaryColor.y + 0.03f, primaryColor.z + 0.03f, 1.0f );
	style.Colors[ImGuiCol_PopupBg]              = secondaryColor;
	style.Colors[ImGuiCol_Border]               = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.4f );
	style.Colors[ImGuiCol_BorderShadow]         = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
	style.Colors[ImGuiCol_FrameBg]              = secondaryColor;
	style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f );
	style.Colors[ImGuiCol_FrameBgActive]        = ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f );
	style.Colors[ImGuiCol_TitleBg]              = secondaryColor;
	style.Colors[ImGuiCol_TitleBgActive]        = accentColor;
	style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.5f );
	style.Colors[ImGuiCol_MenuBarBg]            = ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f );
	style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4( secondaryColor.x - 0.05f, secondaryColor.y - 0.05f, secondaryColor.z - 0.05f, 1.0f );
	style.Colors[ImGuiCol_ScrollbarGrab]        = accentColor;
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = accentLightColor;
	style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4( accentLightColor.x + 0.1f, accentLightColor.y + 0.1f, accentLightColor.z + 0.1f, 1.0f );
	style.Colors[ImGuiCol_CheckMark]            = accentLightColor;
	style.Colors[ImGuiCol_SliderGrab]           = accentColor;
	style.Colors[ImGuiCol_SliderGrabActive]     = accentLightColor;
	style.Colors[ImGuiCol_Button]               = accentColor;
	style.Colors[ImGuiCol_ButtonHovered]        = accentLightColor;
	style.Colors[ImGuiCol_ButtonActive]         = ImVec4( accentLightColor.x - 0.1f, accentLightColor.y - 0.1f, accentLightColor.z - 0.1f, 1.0f );
	style.Colors[ImGuiCol_Header]               = accentColor;
	style.Colors[ImGuiCol_HeaderHovered]        = accentLightColor;
	style.Colors[ImGuiCol_HeaderActive]         = ImVec4( accentLightColor.x - 0.1f, accentLightColor.y - 0.1f, accentLightColor.z - 0.1f, 1.0f );
	style.Colors[ImGuiCol_Separator]            = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.5f );
	style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4( accentLightColor.x, accentLightColor.y, accentLightColor.z, 0.5f );
	style.Colors[ImGuiCol_SeparatorActive]      = ImVec4( accentLightColor.x, accentLightColor.y, accentLightColor.z, 0.7f );
	style.Colors[ImGuiCol_ResizeGrip]           = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.25f );
	style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.67f );
	style.Colors[ImGuiCol_ResizeGripActive]     = accentColor;
	style.Colors[ImGuiCol_Tab]                  = secondaryColor;
	style.Colors[ImGuiCol_TabHovered]           = accentColor;
	style.Colors[ImGuiCol_TabActive]            = ImVec4( accentColor.x + 0.1f, accentColor.y + 0.1f, accentColor.z + 0.1f, 1.0f );
	style.Colors[ImGuiCol_TabUnfocused]         = ImVec4( secondaryColor.x - 0.1f, secondaryColor.y - 0.1f, secondaryColor.z - 0.1f, 1.0f );
	style.Colors[ImGuiCol_TabUnfocusedActive]   = secondaryColor;
	style.Colors[ImGuiCol_PlotLines]            = accentColor;
	style.Colors[ImGuiCol_PlotLinesHovered]     = accentLightColor;
	style.Colors[ImGuiCol_PlotHistogram]        = accentColor;
	style.Colors[ImGuiCol_PlotHistogramHovered] = accentLightColor;
	style.Colors[ImGuiCol_TableHeaderBg]        = ImVec4( secondaryColor.x + 0.05f, secondaryColor.y + 0.05f, secondaryColor.z + 0.05f, 1.0f );
	style.Colors[ImGuiCol_TableBorderStrong]    = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.7f );
	style.Colors[ImGuiCol_TableBorderLight]     = ImVec4( accentColor.x, accentColor.y, accentColor.z, 0.3f );
	style.Colors[ImGuiCol_TableRowBg]           = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
	style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4( 1.0f, 1.0f, 1.0f, 0.06f );
	//style.Colors[ImGuiCol_ProgressBar]          = accentColor;
	//style.Colors[ImGuiCol_ProgressBarFill]      = accentLightColor;

	// Définir la fenêtre ImGui pour qu'elle prenne toute la taille
	ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
	ImGui::SetNextWindowSize( ImVec2( windowWidth, windowHeight ) );

	// Créer la fenêtre principale sans bordures et sans options de redimensionnement
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin( "Test Volant", nullptr, window_flags );

	// Barre de menu avec style amélioré
	if( ImGui::BeginMenuBar() )
	{
		if( ImGui::BeginMenu( "Fichier" ) )
		{
			if( ImGui::MenuItem( "Recharger les périphériques", NULL, false, true ) )
			{
				deviceManager.reloadDevice( logger );
				logger.log( Logger::INFO, "Périphériques rechargés" );
			}
			ImGui::Separator();
			if( ImGui::MenuItem( "Quitter", NULL, false, true ) )
			{
				logger.log( Logger::INFO, "Application fermée par l'utilisateur" );
				SDL_Event event;
				event.type = SDL_QUIT;
				SDL_PushEvent( &event );
			}
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Options" ) )
		{
			if( ImGui::MenuItem( "Sauvegarder la configuration", NULL, false, true ) )
			{
				saveConfig();
			}
			if( ImGui::MenuItem( "Charger une configuration", NULL, false, true ) )
			{
				loadConfig();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Titre principal de l'application
	ImGui::PushFont( ImGui::GetIO().Fonts->Fonts[0] ); // Utiliser la police par défaut
	ImGui::PushStyleColor( ImGuiCol_Text, accentLightColor );
	float titleWidth = ImGui::CalcTextSize( "TEST DE VOLANT" ).x;
	ImGui::SetCursorPosX( ( windowWidth - titleWidth ) * 0.5f );
	ImGui::Text( "TEST DE VOLANT" );
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::Spacing();
	ImGui::Spacing();

	// Zone d'informations générales
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Informations du périphérique", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::PopStyleColor( 3 );

		// Ajouter une bordure et un fond pour cette section
		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		auto& info = deviceManager.getWheelInfo();

		// Utiliser un layout en deux colonnes pour les informations
		ImGui::Columns( 2, "DeviceInfoColumns", false );
		ImGui::SetColumnWidth( 0, 200 );

		ImGui::TextColored( textColor, "Nom:" );
		ImGui::NextColumn();
		ImGui::TextColored( accentLightColor, "%s", info.name.c_str() );
		ImGui::NextColumn();

		ImGui::TextColored( textColor, "ID:" );
		ImGui::NextColumn();
		ImGui::Text( "%d", info.id );
		ImGui::NextColumn();

		ImGui::TextColored( textColor, "Nombre de boutons:" );
		ImGui::NextColumn();
		ImGui::Text( "%d", info.buttonCount );
		ImGui::NextColumn();

		ImGui::TextColored( textColor, "Nombre d'axes:" );
		ImGui::NextColumn();
		ImGui::Text( "%d", info.axisCount );
		ImGui::NextColumn();

		ImGui::TextColored( textColor, "Force Feedback:" );
		ImGui::NextColumn();
		if( info.hasForceFeeback )
		{
			ImGui::TextColored( activeColor, "Oui" );
		}
		else
		{
			ImGui::TextColored( mutedTextColor, "Non" );
		}
		ImGui::NextColumn();

		ImGui::TextColored( textColor, "Mode:" );
		ImGui::NextColumn();
		if( deviceManager.isUsingDemo() )
		{
			ImGui::TextColored( warningColor, "Démo (Simulé)" );
		}
		else
		{
			ImGui::TextColored( activeColor, "Périphérique réel" );
		}
		ImGui::Columns( 1 );

		ImGui::Spacing();
		ImGui::Spacing();

		// Bouton d'actualisation avec un design amélioré
		ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 250 ) * 0.5f );
		if( ImGui::Button( "Actualiser les informations", ImVec2( 250, 30 ) ) )
		{
			deviceManager.reloadDevice( logger );
			logger.log( Logger::INFO, "Informations du périphérique actualisées" );
		}

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Zone des axes avec graphiques
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Axes", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::PopStyleColor( 3 );

		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		auto& axes = deviceManager.getAxes();

		for( int i = 0; i < axes.size(); ++i )
		{
			auto& axis = axes[i];

			// Cadre pour chaque axe
			ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( primaryColor.x + 0.02f, primaryColor.y + 0.02f, primaryColor.z + 0.02f, 1.0f ) );

			// Nom et valeur de l'axe
			ImGui::TextColored( accentLightColor, "%s:", axis.name.c_str() );
			ImGui::SameLine( 150 );
			ImGui::TextColored( textColor, "%.2f", axis.value );

			// Barre de progression pour l'axe avec couleurs personnalisées
			char overlay[32];
			sprintf( overlay, "%.2f", axis.value );

			// Normaliser la valeur pour la barre de progression
			float normalized = ( axis.value - axis.min ) / ( axis.max - axis.min );
			normalized       = std::max( 0.0f, std::min( 1.0f, normalized ) ); // Limiter entre 0 et 1

			// Choisir la couleur en fonction de la valeur
			ImVec4 barColor;
			if( i == 0 )
			{
				// Volant
				// Rouge à gauche, vert à droite, bleu au centre
				if( axis.value < 0 )
				{
					barColor = ImVec4( 0.9f, 0.3f, 0.3f, 1.0f ); // Rouge
				}
				else if( axis.value > 0 )
				{
					barColor = ImVec4( 0.3f, 0.9f, 0.3f, 1.0f ); // Vert
				}
				else
				{
					barColor = ImVec4( 0.3f, 0.6f, 0.9f, 1.0f ); // Bleu
				}
			}
			else if( i == 1 )
			{
				// Accélérateur
				barColor = ImVec4( 0.3f, 0.9f, 0.3f, 1.0f ); // Vert
			}
			else if( i == 2 )
			{
				// Frein
				barColor = ImVec4( 0.9f, 0.3f, 0.3f, 1.0f ); // Rouge
			}
			else if( i == 3 )
			{
				// Embrayage
				barColor = ImVec4( 0.9f, 0.7f, 0.1f, 1.0f ); // Orange
			}
			else
			{
				barColor = accentColor;
			}

			ImGui::PushStyleColor( ImGuiCol_PlotHistogram, barColor );
			ImGui::ProgressBar( normalized, ImVec2( -1, 20 ), overlay );
			ImGui::PopStyleColor();

			// Afficher des boutons pour les options
			ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 12.0f );
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( secondaryColor.x + 0.05f, secondaryColor.y + 0.05f, secondaryColor.z + 0.05f, 1.0f ) );

			if( ImGui::Button( "Options", ImVec2( 80, 20 ) ) )
			{
				ImGui::OpenPopup( ( "Options##" + std::to_string( i ) ).c_str() );
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			if( ImGui::BeginPopup( ( "Options##" + std::to_string( i ) ).c_str() ) )
			{
				ImGui::SliderFloat( "Zone morte", &axis.deadzone, 0.0f, 0.5f );
				ImGui::DragFloat( "Valeur minimum", &axis.min, 0.1f );
				ImGui::DragFloat( "Valeur maximum", &axis.max, 0.1f );
				ImGui::Text( "Valeur brute: %.2f", axis.rawValue );

				if( ImGui::Button( "Calibrer", ImVec2( 100, 24 ) ) )
				{
					deviceManager.calibrateAxis( i, logger );
				}

				ImGui::EndPopup();
			}

			ImGui::PopStyleColor();
		}

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Zone des boutons
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Boutons", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::PopStyleColor( 3 );

		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		auto&       buttons       = deviceManager.getButtons();
		const char* buttonNames[] = {
			"X", "Square", "Circle", "Triangle",
			"L1", "R1", "L2", "R2",
			"Share", "Options", "L3", "R3",
			"PS", "TouchPad", "Up", "Down",
			"Left", "Right", "1", "2",
			"3", "4", "5", "6", "7", "8", "9", "10", "11", "12"
		};

		ImGui::TextColored( accentLightColor, "État des boutons:" );
		ImGui::Spacing();

		// Grille de boutons avec un style amélioré
		ImGui::Columns( 6, "ButtonColumns", false );
		ImGui::SetColumnWidth( 0, windowWidth / 6 );
		ImGui::SetColumnWidth( 1, windowWidth / 6 );
		ImGui::SetColumnWidth( 2, windowWidth / 6 );
		ImGui::SetColumnWidth( 3, windowWidth / 6 );
		ImGui::SetColumnWidth( 4, windowWidth / 6 );

		for( int i = 0; i < buttons.size(); ++i )
		{
			// Obtenir le nom du bouton
			std::string name;
			if( i < IM_ARRAYSIZE( buttonNames ) )
				name = buttonNames[i];
			else
				name = std::to_string( i + 1 );

			// Style pour les boutons actifs/inactifs
			if( buttons[i] )
			{
				ImGui::PushStyleColor( ImGuiCol_Button, activeColor );
				ImGui::PushStyleColor( ImGuiCol_ButtonHovered, activeHoverColor );
				ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( activeColor.x - 0.1f, activeColor.y - 0.1f, activeColor.z - 0.1f, 1.0f ) );
				ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
			}
			else
			{
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.2f, 0.2f, 1.0f ) );
				ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
				ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.25f, 0.25f, 0.25f, 1.0f ) );
				ImGui::PushStyleColor( ImGuiCol_Text, mutedTextColor );
			}

			// Bouton avec une taille fixe et des coins arrondis
			ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 15.0f );
			ImGui::Button( ( name + "##" + std::to_string( i ) ).c_str(), ImVec2( -1, 30 ) );
			ImGui::PopStyleVar();
			ImGui::PopStyleColor( 4 );

			ImGui::NextColumn();
		}
		ImGui::Columns( 1 );

		// Affichage des boutons actuellement pressés
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextColored( accentLightColor, "Boutons actifs:" );
		ImGui::SameLine();

		bool anyButtonActive = false;
		for( int i = 0; i < buttons.size(); ++i )
		{
			if( buttons[i] )
			{
				std::string name;
				if( i < IM_ARRAYSIZE( buttonNames ) )
					name = buttonNames[i];
				else
					name = std::to_string( i + 1 );

				if( anyButtonActive )
				{
					ImGui::SameLine( 0, 5 );
					ImGui::TextColored( activeColor, " | %s", name.c_str() );
				}
				else
				{
					ImGui::SameLine( 0, 5 );
					ImGui::TextColored( activeColor, "%s", name.c_str() );
				}
				anyButtonActive = true;
			}
		}

		if( !anyButtonActive )
		{
			ImGui::SameLine();
			ImGui::TextColored( mutedTextColor, "Aucun" );
		}

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Zone de retour de force
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Retour de Force", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::PopStyleColor( 3 );

		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		auto& forceManager = deviceManager.getForceManager();
		auto& effects      = forceManager.getEffects();

		// Interrupteur principal pour le FFB avec style amélioré
		bool ffbEnabled = forceManager.isFFBEnabled();
		ImGui::PushStyleColor( ImGuiCol_CheckMark, activeColor );
		ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

		if( ImGui::Checkbox( "Activer le retour de force", &ffbEnabled ) )
		{
			forceManager.setFFBEnabled( ffbEnabled );
			logger.log( Logger::INFO, "Retour de force " + std::string( ffbEnabled ? "activé" : "désactivé" ) );
		}

		ImGui::PopStyleColor( 2 );

		// Réglage de la force globale avec un curseur amélioré
		float masterStrength = forceManager.getMasterStrength();
		ImGui::PushStyleColor( ImGuiCol_SliderGrab, accentColor );
		ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, accentLightColor );

		if( ImGui::SliderFloat( "Force globale", &masterStrength, 0.0f, 100.0f, "%.0f%%" ) )
		{
			forceManager.setMasterStrength( masterStrength );
		}

		ImGui::PopStyleColor( 2 );

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextColored( accentLightColor, "Effets disponibles:" );
		ImGui::Spacing();

		// Tableau pour les effets avec style amélioré
		ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( secondaryColor.x + 0.05f, secondaryColor.y + 0.05f, secondaryColor.z + 0.05f, 1.0f ) );
		ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( primaryColor.x + 0.03f, primaryColor.y + 0.03f, primaryColor.z + 0.03f, 1.0f ) );

		if( ImGui::BeginTable( "EffetsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
		{
			ImGui::TableSetupColumn( "Effet", ImGuiTableColumnFlags_WidthFixed, 120.0f );
			ImGui::TableSetupColumn( "Activer", ImGuiTableColumnFlags_WidthFixed, 70.0f );
			ImGui::TableSetupColumn( "Force", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Durée", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableHeadersRow();

			for( int i = 0; i < effects.size(); ++i )
			{
				auto& effect = effects[i];
				ImGui::TableNextRow();

				// Nom de l'effet
				ImGui::TableNextColumn();
				ImGui::TextColored( textColor, "%s", effect.name.c_str() );

				// Activer/Désactiver
				ImGui::TableNextColumn();
				ImGui::PushStyleColor( ImGuiCol_CheckMark, activeColor );

				if( ImGui::Checkbox( ( "##effect" + std::to_string( i ) ).c_str(), &effect.enabled ) )
				{
					logger.log( Logger::DEBUG, "Effet " + effect.name + ( effect.enabled ? " activé" : " désactivé" ) );
				}

				ImGui::PopStyleColor();

				// Force
				ImGui::TableNextColumn();
				ImGui::PushStyleColor( ImGuiCol_SliderGrab, accentColor );
				ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, accentLightColor );

				ImGui::SliderFloat( ( "##strength" + std::to_string( i ) ).c_str(), &effect.strength, 0.0f, 100.0f, "%.0f%%" );

				ImGui::PopStyleColor( 2 );

				// Durée (si applicable)
				ImGui::TableNextColumn();
				if( effect.duration > 0.0f )
				{
					ImGui::PushStyleColor( ImGuiCol_SliderGrab, accentColor );
					ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, accentLightColor );

					ImGui::SliderFloat( ( "##duration" + std::to_string( i ) ).c_str(), &effect.duration, 0.1f, 5.0f, "%.1fs" );

					ImGui::PopStyleColor( 2 );
				}
				else
				{
					ImGui::TextColored( mutedTextColor, "N/A" );
				}
			}
			ImGui::EndTable();
		}

		ImGui::PopStyleColor( 2 );

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Boutons pour tester les effets avec style amélioré
		ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 450 ) * 0.5f );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );

		if( ImGui::Button( "Tester tous les effets actifs", ImVec2( 220, 30 ) ) )
		{
			logger.log( Logger::INFO, "Test de tous les effets actifs" );
			int testedCount = 0;

			for( int i = 0; i < effects.size(); ++i )
			{
				if( effects[i].enabled )
				{
					forceManager.playEffect( i, logger );
					testedCount++;
				}
			}

			if( testedCount == 0 )
			{
				logger.log( Logger::WARNING, "Aucun effet actif à tester" );
			}
		}

		ImGui::SameLine();
		ImGui::PushStyleColor( ImGuiCol_Button, errorColor );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( errorColor.x + 0.1f, errorColor.y + 0.1f, errorColor.z + 0.1f, 1.0f ) );

		if( ImGui::Button( "Arrêter tous les effets", ImVec2( 220, 30 ) ) )
		{
			forceManager.stopAllEffects();
			logger.log( Logger::INFO, "Tous les effets arrêtés" );
		}

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Test de collision avec style amélioré
		ImGui::TextColored( accentLightColor, "Test de collision:" );
		ImGui::PushStyleColor( ImGuiCol_SliderGrab, warningColor );
		ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, ImVec4( warningColor.x + 0.1f, warningColor.y + 0.1f, warningColor.z + 0.1f, 1.0f ) );

		ImGui::SliderFloat( "Force de collision", &collisionStrength, 0.0f, 100.0f, "%.0f%%" );

		ImGui::PopStyleColor( 2 );

		ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 200 ) * 0.5f );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
		ImGui::PushStyleColor( ImGuiCol_Button, warningColor );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( warningColor.x + 0.1f, warningColor.y + 0.1f, warningColor.z + 0.1f, 1.0f ) );

		if( ImGui::Button( "Simuler une collision", ImVec2( 200, 30 ) ) )
		{
			forceManager.simulateCollision( collisionStrength, logger );
			logger.log( Logger::INFO, "Collision simulée avec force " + std::to_string( collisionStrength ) + "%" );
		}

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar();

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Zone des réglages de test
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Réglages de test" ) )
	{
		ImGui::PopStyleColor( 3 );

		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		ImGui::TextColored( accentLightColor, "Sélection du mode de test:" );
		ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( secondaryColor.x + 0.05f, secondaryColor.y + 0.05f, secondaryColor.z + 0.05f, 1.0f ) );

		if( ImGui::BeginCombo( "Mode", testModes[testMode] ) )
		{
			for( int i = 0; i < IM_ARRAYSIZE( testModes ); ++i )
			{
				if( ImGui::Selectable( testModes[i], i == testMode ) )
				{
					testMode = i;
					logger.log( Logger::INFO, "Mode de test changé: " + std::string( testModes[testMode] ) );
				}
			}
			ImGui::EndCombo();
		}

		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Options spécifiques au mode
		switch( testMode )
		{
			case 0: // Manuel
				ImGui::PushStyleColor( ImGuiCol_Text, accentLightColor );
				ImGui::TextWrapped( "Mode Manuel: Testez les entrées vous-même en manipulant le volant et les commandes." );
				ImGui::PopStyleColor();
				break;

			case 1: // Automatique
			{
				ImGui::PushStyleColor( ImGuiCol_Text, accentLightColor );
				ImGui::TextWrapped( "Mode Automatique: Simule des entrées automatiquement pour tester le périphérique." );
				ImGui::PopStyleColor();

				ImGui::Spacing();
				ImGui::Spacing();

				bool  autoButtonTest = autoTestManager.getTestButtons();
				bool  autoAxisTest   = autoTestManager.getTestAxes();
				bool  autoFFBTest    = autoTestManager.getTestFFB();
				float testDuration   = autoTestManager.getTestDuration();

				bool changed = false;

				ImGui::PushStyleColor( ImGuiCol_CheckMark, activeColor );
				changed |= ImGui::Checkbox( "Tester les boutons", &autoButtonTest );
				changed |= ImGui::Checkbox( "Tester les axes", &autoAxisTest );
				changed |= ImGui::Checkbox( "Tester le retour de force", &autoFFBTest );
				ImGui::PopStyleColor();

				ImGui::PushStyleColor( ImGuiCol_SliderGrab, accentColor );
				ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, accentLightColor );
				changed |= ImGui::SliderFloat( "Durée du test (secondes)", &testDuration, 5.0f, 120.0f, "%.0fs" );
				ImGui::PopStyleColor( 2 );

				if( changed )
				{
					autoTestManager.setTestOptions( autoButtonTest, autoAxisTest, autoFFBTest, testDuration );
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if( autoTestManager.isTestRunning() )
				{
					// Afficher la progression
					float progress = autoTestManager.getCurrentTime() / autoTestManager.getTestDuration();
					ImGui::TextColored( textColor, "Test en cours: %.1f / %.1f secondes",
										autoTestManager.getCurrentTime(),
										autoTestManager.getTestDuration() );

					ImGui::PushStyleColor( ImGuiCol_PlotHistogram, accentColor );
					ImGui::ProgressBar( progress, ImVec2( -1, 15 ) );
					ImGui::PopStyleColor();

					ImGui::Spacing();

					ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 150 ) * 0.5f );
					ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
					ImGui::PushStyleColor( ImGuiCol_Button, errorColor );
					ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( errorColor.x + 0.1f, errorColor.y + 0.1f, errorColor.z + 0.1f, 1.0f ) );

					if( ImGui::Button( "Arrêter le test", ImVec2( 150, 30 ) ) )
					{
						autoTestManager.stopTest();
						logger.log( Logger::INFO, "Test automatique arrêté manuellement" );
					}

					ImGui::PopStyleColor( 2 );
					ImGui::PopStyleVar();
				}
				else
				{
					ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 200 ) * 0.5f );
					ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
					ImGui::PushStyleColor( ImGuiCol_Button, activeColor );
					ImGui::PushStyleColor( ImGuiCol_ButtonHovered, activeHoverColor );

					if( ImGui::Button( "Démarrer le test automatique", ImVec2( 200, 30 ) ) )
					{
						autoTestManager.startTest( testDuration, autoButtonTest, autoAxisTest, autoFFBTest );
						logger.log( Logger::INFO, "Test automatique démarré pour " + std::to_string( testDuration ) + " secondes" );
					}

					ImGui::PopStyleColor( 2 );
					ImGui::PopStyleVar();
				}
			}
			break;

			case 2: // Enregistrement
			{
				ImGui::PushStyleColor( ImGuiCol_Text, accentLightColor );
				ImGui::TextWrapped( "Mode Enregistrement: Enregistre vos entrées pour une lecture ultérieure." );
				ImGui::PopStyleColor();

				ImGui::Spacing();
				ImGui::Spacing();

				if( recordingManager.getIsRecording() )
				{
					float recordDuration = recordingManager.getRecordingDuration();

					ImGui::TextColored( textColor, "Enregistrement en cours: %.1f / %.1f secondes", recordDuration, MAX_RECORD_TIME );

					ImGui::PushStyleColor( ImGuiCol_PlotHistogram, warningColor );
					ImGui::ProgressBar( recordDuration / MAX_RECORD_TIME, ImVec2( -1, 15 ) );
					ImGui::PopStyleColor();

					ImGui::Spacing();

					ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 180 ) * 0.5f );
					ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
					ImGui::PushStyleColor( ImGuiCol_Button, errorColor );
					ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( errorColor.x + 0.1f, errorColor.y + 0.1f, errorColor.z + 0.1f, 1.0f ) );

					if( ImGui::Button( "Arrêter l'enregistrement", ImVec2( 180, 30 ) ) )
					{
						recordingManager.stopRecording();
						logger.log( Logger::INFO, "Enregistrement arrêté après " + std::to_string( recordDuration ) + " secondes" );
					}

					ImGui::PopStyleColor( 2 );
					ImGui::PopStyleVar();
				}
				else
				{
					ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
					ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - 380 ) * 0.5f );

					if( ImGui::Button( "Commencer l'enregistrement", ImVec2( 180, 30 ) ) )
					{
						recordingManager.startRecording();
						logger.log( Logger::INFO, "Enregistrement démarré" );
					}

					if( recordingManager.hasRecording() )
					{
						float duration = recordingManager.getTotalDuration();

						ImGui::SameLine();
						ImGui::PushStyleColor( ImGuiCol_Button, accentColor );
						ImGui::PushStyleColor( ImGuiCol_ButtonHovered, accentLightColor );

						if( ImGui::Button( "Lire l'enregistrement", ImVec2( 120, 30 ) ) )
						{
							logger.log( Logger::INFO, "Lecture de l'enregistrement (" + std::to_string( duration ) + " secondes)" );
							// TODO: Implémenter la lecture
						}

						ImGui::PopStyleColor( 2 );

						ImGui::SameLine();
						ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.6f, 0.4f, 1.0f ) );
						ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.7f, 0.5f, 1.0f ) );

						if( ImGui::Button( "Sauvegarder", ImVec2( 100, 30 ) ) )
						{
							std::string filename = "record_" + getCurrentTimeString() + ".dat";
							ResultCode  result   = recordingManager.saveRecording( filename );
							if( result == SUCCESS )
							{
								logger.log( Logger::INFO, "Enregistrement sauvegardé dans " + filename );
							}
							else
							{
								logger.log( Logger::ERROR, "Erreur lors de la sauvegarde de l'enregistrement" );
							}
						}

						ImGui::PopStyleColor( 2 );
					}

					ImGui::PopStyleVar();
				}
			}
			break;
		}

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Statistiques et débogage
	ImGui::PushStyleColor( ImGuiCol_Header, secondaryColor );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( secondaryColor.x + 0.1f, secondaryColor.y + 0.1f, secondaryColor.z + 0.1f, 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( secondaryColor.x + 0.15f, secondaryColor.y + 0.15f, secondaryColor.z + 0.15f, 1.0f ) );

	if( ImGui::CollapsingHeader( "Statistiques" ) )
	{
		ImGui::PopStyleColor( 3 );

		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( secondaryColor.x - 0.02f, secondaryColor.y - 0.02f, secondaryColor.z - 0.02f, 1.0f ) );

		ImGui::Columns( 2, "StatsColumns", false );
		ImGui::SetColumnWidth( 0, ImGui::GetWindowWidth() * 0.5f );

		// Colonne de gauche - Performances
		ImGui::TextColored( accentLightColor, "Performances:" );
		ImGui::TextColored( textColor, "FPS: %.1f", ImGui::GetIO().Framerate );
		ImGui::TextColored( textColor, "Temps par image: %.3f ms", 1000.0f / ImGui::GetIO().Framerate );

		ImGui::PushStyleColor( ImGuiCol_SliderGrab, accentColor );
		ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, accentLightColor );
		ImGui::SliderFloat( "Fréquence de mise à jour", &config.updateFrequency, 0.0f, 100.0f, "%.0f Hz" );
		ImGui::PopStyleColor( 2 );

		ImGui::NextColumn();

		// Colonne de droite - Journalisation
		ImGui::TextColored( accentLightColor, "Journalisation:" );

		bool logToFile      = config.logToFile;
		bool verboseLogging = config.verboseLogging;

		ImGui::PushStyleColor( ImGuiCol_CheckMark, activeColor );

		if( ImGui::Checkbox( "Enregistrer dans un fichier", &logToFile ) )
		{
			config.logToFile = logToFile;
			logger.setLogToFile( logToFile, logFilename );
		}

		ImGui::SameLine();

		if( ImGui::Checkbox( "Mode verbeux", &verboseLogging ) )
		{
			config.verboseLogging = verboseLogging;
			logger.setVerbose( verboseLogging );
		}

		ImGui::PopStyleColor();

		ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( primaryColor.x + 0.05f, primaryColor.y + 0.05f, primaryColor.z + 0.05f, 1.0f ) );

		if( ImGui::InputText( "Nom du fichier journal", logFilename, IM_ARRAYSIZE( logFilename ) ) )
		{
			config.logFilename = logFilename;
			if( config.logToFile )
			{
				logger.setLogToFile( true, logFilename );
			}
		}

		ImGui::PopStyleColor();

		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
		ImGui::PushStyleColor( ImGuiCol_Button, errorColor );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( errorColor.x + 0.1f, errorColor.y + 0.1f, errorColor.z + 0.1f, 1.0f ) );

		if( ImGui::Button( "Effacer le journal", ImVec2( 150, 24 ) ) )
		{
			logger.clearLog();
			logger.log( Logger::INFO, "Journal effacé" );
		}

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar();

		ImGui::Columns( 1 );

		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PopStyleColor( 3 );
	}

	// Barre d'état en bas
	ImGui::PushStyleColor( ImGuiCol_ChildBg, secondaryColor );

	ImGui::Text( "État: " );
	ImGui::SameLine();

	if( deviceManager.isUsingDemo() )
	{
		ImGui::TextColored( warningColor, "Mode démo (aucun périphérique détecté)" );
	}
	else
	{
		ImGui::TextColored( activeColor, "Périphérique connecté" );
	}

	ImGui::SameLine( ImGui::GetWindowWidth() - 120 );
	ImGui::TextColored( mutedTextColor, "v%s", APP_VERSION );

	ImGui::PopStyleColor();

	ImGui::End();
}
