/* preferences_c.h - Preferences 'c' interface
 header  For the Macintosh OS X SDL port of Atari800
 Mark Grebe <atarimacosx@gmail.com>
 
 */

#ifndef INCLUDE_PREFERENCESCH
#define INCLUDE_PREFERENCESCH

#include "stdio.h"

typedef struct ATARI800MACX_PREF {
                int scaleFactor;
                double scaleFactorFloat;
                int widthMode;
				int scaleMode;
                int tvMode; 
                int refreshRatio; 
				int spriteCollisions;
                int artifactingMode;
                int artifactNew;
                int useBuiltinPalette;
                int blackLevel;
                int whiteLevel;
                int intensity;
                int colorShift;
                int adjustPalette;
                char paletteFile[FILENAME_MAX]; 
                int showFPS;
                int onlyIntegralScaling;
                int fixAspectFullscreen;
				int ledStatus;
                int ledSector;
                int ledHDSector;
                int ledFKeys;
                int ledCapsLock;
				int ledStatusMedia;
				int ledSectorMedia;
                int atariType; 
                int atariSwitchType; 
                int disableBasic;
				int disableAllBasic; 
                int enableSioPatch; 
				int enableHPatch; 
				int enableDPatch; 
                int enablePPatch;
                int enableRPatch;
                int rPatchPort;
				int rPatchSerialEnabled;
				char rPatchSerialPort[FILENAME_MAX];
                char printCommand[FILENAME_MAX];
                int bootFromCassette; 
                int speedLimit; 
                int enableSound; 
				double soundVolume;
                int enableStereo; 
#if 0 /* enableHifiSound is deprecated from 4.2.2 on */    	
                int enableHifiSound; 
#endif
				int enable16BitSound;
                int enableConsoleSound; 
                int enableSerioSound; 
                int dontMuteAudio;
                int diskDriveSound;
				int enableInternational;
                int ignoreHeaderWriteprotect; 
				int axlonBankMask;
				int mosaicMaxBank;
				int blackBoxEnabled;
				int mioEnabled;
                char af80CharsetFile[FILENAME_MAX];
                char af80RomFile[FILENAME_MAX];
                char bit3CharsetFile[FILENAME_MAX];
                char bit3RomFile[FILENAME_MAX];
                char blackBoxRomFile[FILENAME_MAX];
                char ultimate1MBFlashFileName[FILENAME_MAX];
                char ultimate1MBNVRAMFileName[FILENAME_MAX];
                char side2FlashFileName[FILENAME_MAX];
                char side2NVRAMFileName[FILENAME_MAX];
                int  side2SDXMode;
                int  side2UltimateFlashType;
                char side2CFFileName[FILENAME_MAX];
				char mioRomFile[FILENAME_MAX];
				char blackBoxScsiDiskFile[FILENAME_MAX]; 
				char mioScsiDiskFile[FILENAME_MAX]; 
				char imageDir[FILENAME_MAX]; 
                char printDir[FILENAME_MAX]; 
                char hardDiskDir[4][FILENAME_MAX]; 
                int hardDrivesReadOnly;
                char hPath[FILENAME_MAX];
                char xegsRomFile[FILENAME_MAX];
                char xegsGameRomFile[FILENAME_MAX];
                char a1200XLRomFile[FILENAME_MAX];
                char osBRomFile[FILENAME_MAX];
                char xlRomFile[FILENAME_MAX];
                char basicRomFile[FILENAME_MAX];
                char a5200RomFile[FILENAME_MAX];
                int  useAltirraXEGSRom;
                int  useAltirra1200XLRom;
                int  useAltirraOSBRom;
                int  useAltirraXLRom;
                int  useAltirra5200Rom;
                int  useAltirraBasicRom;
                char diskImageDir[FILENAME_MAX];
                char diskSetDir[FILENAME_MAX];
                char cartImageDir[FILENAME_MAX];
                char cassImageDir[FILENAME_MAX];
                char exeFileDir[FILENAME_MAX];
				char savedStateDir[FILENAME_MAX]; 
				char configDir[FILENAME_MAX]; 
                char dFile[8][FILENAME_MAX]; 
                char cartFile[FILENAME_MAX]; 
                char cart2File[FILENAME_MAX]; 
                char exeFile[FILENAME_MAX]; 
                char cassFile[FILENAME_MAX]; 
                int dFileEnabled[8];
                int cartFileEnabled;
                int cart2FileEnabled;
                int exeFileEnabled;
                int cassFileEnabled;
                int joystickMode[4];
                int joystickAutofire[4];
                int enableMultijoy;
                int mouseDevice;
                int mouseSpeed;
                int mouseMinVal;
                int mouseMaxVal;
                int mouseHOffset;
                int mouseVOffset;
                int mouseYInvert;
                int mouseInertia;
                int joystick1Type;
                int joystick2Type;
                int joystick3Type;
                int joystick4Type;
                int joystick1Num;
                int joystick2Num;
                int joystick3Num;
                int joystick4Num;
				int cx85enabled;
				int cx85port;
                int buttonAssignment[4][24];
                int button5200Assignment[4][24];
                int paddlesXAxisOnly;
                int leftJoyUp;
                int leftJoyDown;
                int leftJoyLeft;
                int leftJoyRight;
                int leftJoyUpLeft;
                int leftJoyUpRight;
                int leftJoyDownLeft;
                int leftJoyDownRight;
                int leftJoyFire;
                int leftJoyAltFire;
                int padJoyUp;
                int padJoyDown;
                int padJoyLeft;
                int padJoyRight;
                int padJoyUpLeft;
                int padJoyUpRight;
                int padJoyDownLeft;
                int padJoyDownRight;
                int padJoyFire;
                int padJoyAltFire;
				int mediaStatusDisplayed;
				int functionKeysDisplayed;
				int currPrinter;
				int saveCurrentMedia;
				int clearCurrentMedia;
				int keyjoyEnable;
				double emulationSpeed;
                int af80_enabled;
                int bit3_enabled;
                int xep80_enabled;
                int COL80_autoswitch;
				int xep80;
				int xep80_port;
				int xep80_oncolor;
				int xep80_offcolor;
                int a1200xlJumper;
                int xegsKeyboard;
				int useAtariCursorKeys;
} ATARI800MACX_PREF;

typedef struct ATARI800MACX_PREFSAVE {
                int scaleFactor;
                double scaleFactorFloat;
                int widthMode; 
				int scaleMode;
                int showFPS;
				int ledStatus;
				int ledSector;
                int speedLimit; 
                int enableSound; 
				double soundVolume;
				int mediaStatusDisplayed;
				int functionKeysDisplayed;
				int disableBasic;
				int bootFromCassette; 
				int enableSioPatch; 
				int enableHPatch; 
				int enableDPatch; 
				int hardDrivesReadOnly;
				int enablePPatch;
				int enableRPatch;
				int atariType;
				int currPrinter;
				int artifactingMode;
				int keyjoyEnable;
				int cx85Enable;
				int enableStereo; 
				int xep80;
                int af80_enabled;
                int bit3_enabled;
                int xep80_enabled;
                int COL80_autoswitch;
				int xep80_port;
                int a1200xlJumper;
                int xegsKeyboard;
				int blackBoxEnabled;
				int mioEnabled;
				int useAtariCursorKeys;
                char side2CFFileName[FILENAME_MAX];
                char side2FlashFileName[FILENAME_MAX];
                char side2NVRAMFileName[FILENAME_MAX];
                int side2SDXMode;
                char ultimate1MBFlashFileName[FILENAME_MAX];
                char ultimate1MBNVRAMFileName[FILENAME_MAX];
} ATARI800MACX_PREFSAVE;

extern int clearCurrentMedia;

ATARI800MACX_PREF *getPrefStorage(void);
void commitPrefs(void);
void saveMediaPrefs(void);
void savePrefs(void);
#endif
