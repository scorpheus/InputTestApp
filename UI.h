#pragma once

#include <SDL.h>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <memory>
#include <ctime>
#include <filesystem>

// =============================================================================
// D�FINITION DES CODES DE RETOUR ET CONSTANTES
// =============================================================================

// Version de l'application
extern const char* APP_VERSION;

// Codes de retour
enum ResultCode
{
	SUCCESS = 0,
	ERROR_FILE_OPEN_FAILED,
	ERROR_FILE_WRITE_FAILED,
	ERROR_FILE_READ_FAILED,
	ERROR_RECORDING_ALREADY_ACTIVE,
	ERROR_RECORDING_NOT_ACTIVE,
	ERROR_SDL_INIT_FAILED,
	ERROR_JOYSTICK_NOT_FOUND,
	ERROR_INVALID_PARAMETER
};

// =============================================================================
// D�FINITION DES STRUCTURES
// =============================================================================

// Structure pour stocker les informations du volant
struct WheelInfo
{
	std::string name;
	int         id;
	int         buttonCount;
	int         axisCount;
	bool        hasForceFeeback;
};

// Structure pour les axes
struct AxisInfo
{
	std::string name;
	float       value;
	float       min;
	float       max;
	float       deadzone;
	float       rawValue; // Valeur brute avant application de la zone morte
};

// Structure pour les effets de force
struct ForceEffect
{
	std::string name;
	bool        enabled;
	float       strength;
	float       duration;
	int         effectId; // ID utilis� par le syst�me de force feedback
};

// Structure pour un �tat instantan� des entr�es
struct InputSnapshot
{
	double             timestamp;
	std::vector<float> axisValues;
	std::vector<bool>  buttonStates;
};

// Structure pour la configuration
struct Config
{
	std::string logFilename;
	bool        logToFile;
	bool        verboseLogging;
	float       updateFrequency;
	float       masterForceStrength;
	bool        ffbEnabled;
};

// =============================================================================
// D�CLARATION DES CLASSES
// =============================================================================

// Gestionnaire de log
class Logger
{
private:
	std::ofstream logFile;
	bool          toFile;
	bool          verbose;

	// Date et heure format�es pour le log
	std::string getCurrentTimestamp();

public:
	Logger();
	~Logger();

	enum LogLevel
	{
		INFO,
		WARNING,
		ERROR,
		DEBUG
	};

	ResultCode initialize( const std::string& filename, bool logToFile, bool verboseLogging );
	void       log( LogLevel level, const std::string& message );
	void       clearLog();
	void       setVerbose( bool verboseLogging );
	void       setLogToFile( bool logToFile, const std::string& filename );

	bool isLogToFile() const { return toFile; }
	bool isVerbose() const { return verbose; }
};

// Gestionnaire d'enregistrement
class RecordingManager
{
private:
	std::vector<InputSnapshot>            recordedData;
	std::chrono::steady_clock::time_point startTime;
	bool                                  isRecording;
	float                                 currentTime;

public:
	RecordingManager();

	ResultCode startRecording();
	ResultCode stopRecording();
	bool       getIsRecording() const;
	float      getRecordingDuration() const;

	// Ajouter un instantan� des entr�es � l'enregistrement
	void addSnapshot( const std::vector<AxisInfo>& axes, const std::vector<bool>& buttons );

	// Obtenir le snapshot pour la lecture � un moment donn�
	bool getSnapshotAtTime( float time, InputSnapshot& outSnapshot );

	// Sauvegarder l'enregistrement dans un fichier
	ResultCode saveRecording( const std::string& filename );

	// Charger un enregistrement depuis un fichier
	ResultCode loadRecording( const std::string& filename );

	// Obtenir la dur�e totale de l'enregistrement
	float getTotalDuration() const;
	bool  hasRecording() const;
};

// Gestionnaire de test automatique
class AutoTestManager
{
private:
	bool  isRunning;
	float startTime;
	float currentTime;
	float duration;
	bool  testButtons;
	bool  testAxes;
	bool  testFFB;

	// Pattern pour l'animation des axes
	float getAxisValue( int axisIndex, float time );

	// Pattern pour l'animation des boutons
	bool getButtonState( int buttonIndex, float time );

public:
	AutoTestManager();

	ResultCode startTest( float testDuration, bool buttons, bool axes, bool ffb );
	ResultCode stopTest();
	bool       isTestRunning() const;
	float      getTestDuration() const;
	float      getCurrentTime() const;

	// Mettre � jour le test et g�n�rer les valeurs actuelles
	void update( std::vector<AxisInfo>& axes, std::vector<bool>& buttons, std::vector<ForceEffect>& effects );

	// Getters/Setters pour les options
	void setTestOptions( bool buttons, bool axes, bool ffb, float testDuration );
	bool getTestButtons() const;
	bool getTestAxes() const;
	bool getTestFFB() const;
};

// Gestionnaire de force feedback
class ForceManager
{
private:
	bool                     ffbEnabled;
	float                    masterStrength;
	std::vector<ForceEffect> effects;
	SDL_Haptic*              haptic;

	// Initialisation des diff�rents types d'effets
	void initConstantEffect( int index );
	void initSpringEffect( int index );
	void initDamperEffect( int index );
	void initFrictionEffect( int index );
	void initSineEffect( int index );
	void initSawtoothEffect( int index );
	void initRumbleEffect( int index );

public:
	ForceManager();
	~ForceManager();

	void       cleanup();
	ResultCode initialize( SDL_Joystick* joystick, Logger& logger );

	// Mettre � jour l'effet avec les param�tres actuels
	void updateEffect( int index );

	// Simuler une collision (une impulsion forte)
	void simulateCollision( float strength, Logger& logger );

	// Jouer un effet
	void playEffect( int index, Logger& logger );

	// Arr�ter tous les effets
	void stopAllEffects();

	// Mettre � jour le retour de force
	void update( Logger& logger );

	// Getters/Setters
	bool                      isFFBEnabled() const;
	void                      setFFBEnabled( bool enabled );
	float                     getMasterStrength() const;
	void                      setMasterStrength( float strength );
	std::vector<ForceEffect>& getEffects();
};

// Gestionnaire de p�riph�rique
class DeviceManager
{
private:
	SDL_Joystick*         joystick;
	SDL_Haptic*           haptic;
	WheelInfo             wheelInfo;
	std::vector<AxisInfo> axes;
	std::vector<bool>     buttons;
	ForceManager          forceManager;
	bool                  usingDemo;

public:
	DeviceManager();
	~DeviceManager();

	void       cleanup();
	ResultCode initialize( Logger& logger );

	// Mise � jour des entr�es
	void updateInputs( RecordingManager& recorder, AutoTestManager& autoTest, Logger& logger );

	// Recharger le p�riph�rique
	ResultCode reloadDevice( Logger& logger );

	// Getters
	WheelInfo&             getWheelInfo();
	std::vector<AxisInfo>& getAxes();
	std::vector<bool>&     getButtons();
	ForceManager&          getForceManager();
	bool                   isUsingDemo() const;

	// Calibrer un axe
	void calibrateAxis( int axisIndex, Logger& logger );
};

// Application principale
class WheelTestApp
{
private:
	Logger           logger;
	DeviceManager    deviceManager;
	RecordingManager recordingManager;
	AutoTestManager  autoTestManager;
	Config           config;

	// Variables pour l'interface
	static constexpr float MAX_RECORD_TIME = 60.0f;
	int                    testMode;
	static const char*     testModes[];
	char                   logFilename[128];
	float                  collisionStrength;
	std::string            configFilename;

	// Obtenir une cha�ne de date/heure format�e pour les noms de fichiers
	std::string getCurrentTimeString();

public:
	WheelTestApp();
	~WheelTestApp();

	// Initialiser l'application
	ResultCode initialize();

	// Boucle de mise � jour principale
	void update();

	// Charger la configuration
	ResultCode loadConfig();

	// Sauvegarder la configuration
	ResultCode saveConfig();

	// Rendre l'interface utilisateur
	void renderUI( SDL_Window* window );
};
