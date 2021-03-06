/*
 * atari.c - main high-level routines
 *
 * Copyright (c) 1995-1998 David Firth
 * Copyright (c) 1998-2005 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# elif defined(HAVE_TIME_H)
#  include <time.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef WIN32
#include <windows.h>
#endif
#ifdef __EMX__
#define INCL_DOS
#include <os2.h>
#endif
#ifdef __BEOS__
#include <OS.h>
#endif

#include "atari.h"
#include "cpu.h"
#include "memory.h"
#include "antic.h"
#include "gtia.h"
#include "pia.h"
#include "pokey.h"
#include "cartridge.h"
#include "devices.h"
#include "sio.h"
#include "monitor.h"
#include "platform.h"
#include "prompts.h"
#include "rt-config.h"
#include "log.h"
#include "input.h"
#if !defined(BASIC) && !defined(CURSES_BASIC)
#include "colours.h"
#include "screen.h"
#endif
#ifndef BASIC
#include "statesav.h"
#include "ui.h"
#endif
#include "binload.h"
#include "rtime.h"
#include "cassette.h"
#ifdef SOUND
#include "sound.h"
#include "sndsave.h"
#endif

int machine_switch_type = 7;
int machine_type = MACHINE_OSB;
int ram_size = 48;
int tv_mode = TV_PAL;

int verbose = FALSE;

int display_screen = FALSE;
int nframes = 0;
int sprite_collisions_in_skipped_frames = FALSE;

static double frametime = 0.1;
double deltatime;
double fps;
int percent_atari_speed = 100;

#ifdef BENCHMARK
static double benchmark_start_time;
#endif

int emuos_mode = 1;	/* 0 = never use EmuOS, 1 = use EmuOS if real OS not available, 2 = always use EmuOS */

static double Atari_time(void);

#ifdef HAVE_SIGNAL
RETSIGTYPE sigint_handler(int num)
{
	int restart;

	restart = Atari800_Exit(TRUE);
	if (restart) {
		signal(SIGINT, sigint_handler);
		return;
	}

	exit(0);
}
#endif

/* Now we check address of every escape code, to make sure that the patch
   has been set by the emulator and is not a CIM in Atari program.
   Also switch() for escape codes has been changed to array of pointers
   to functions. This allows adding port-specific patches (e.g. modem device)
   using Atari800_AddEsc, Device_UpdateHATABSEntry etc. without modifying
   atari.c/devices.c. Unfortunately it can't be done for patches in Atari OS,
   because the OS in XL/XE can be disabled.
*/
static UWORD esc_address[256];
static EscFunctionType esc_function[256];

void Atari800_ClearAllEsc(void)
{
	int i;
	for (i = 0; i < 256; i++)
		esc_function[i] = NULL;
}

void Atari800_AddEsc(UWORD address, UBYTE esc_code, EscFunctionType function)
{
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xf2);			/* ESC */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
}

void Atari800_AddEscRts(UWORD address, UBYTE esc_code, EscFunctionType function)
{
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xf2);			/* ESC */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
	dPutByte(address + 2, 0x60);		/* RTS */
}

/* 0xd2 is ESCRTS, which works same as pair of ESC and RTS (I think so...).
   So this function does effectively the same as Atari800_AddEscRts,
   except that it modifies 2, not 3 bytes in Atari memory.
   I don't know why it is done that way, so I simply leave it
   unchanged (0xf2/0xd2 are used as in previous versions).
*/
void Atari800_AddEscRts2(UWORD address, UBYTE esc_code, EscFunctionType function)
{
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xd2);			/* ESCRTS */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
}

void Atari800_RemoveEsc(UBYTE esc_code)
{
	esc_function[esc_code] = NULL;
}

void Atari800_RunEsc(UBYTE esc_code)
{
	if (esc_address[esc_code] == regPC - 2 && esc_function[esc_code] != NULL) {
		esc_function[esc_code]();
		return;
	}
#ifdef CRASH_MENU
	regPC -= 2;
	crash_address = regPC;
	crash_afterCIM = regPC + 2;
	crash_code = dGetByte(crash_address);
	ui();
#else
	Aprint("Invalid ESC code %02x at address %04x", esc_code, regPC - 2);
	if (!Atari800_Exit(TRUE))
		exit(0);
#endif
}

void Atari800_PatchOS(void)
{
	int patched = Device_PatchOS();
	if (enable_sio_patch) {
		UWORD addr_l;
		UWORD addr_s;
		UBYTE save_check_bytes[2];
		/* patch Open() of C: so we know when a leader is processed */
		switch (machine_type) {
		case MACHINE_OSA:
		case MACHINE_OSB:
			addr_l = 0xef74;
			addr_s = 0xefbc;
			save_check_bytes[0] = 0xa0;
			save_check_bytes[1] = 0x80;
			break;
		case MACHINE_XLXE:
			addr_l = 0xfd13;
			addr_s = 0xfd60;
			save_check_bytes[0] = 0xa9;
			save_check_bytes[1] = 0x03;
			break;
		default:
			Aprint("Fatal Error in Atari800_PatchOS(): Unknown machine");
			return;
		}
		/* don't hurt non-standard OSes that may not support cassette at all  */
		if (dGetByte(addr_l)     == 0xa9 && dGetByte(addr_l + 1) == 0x03
		 && dGetByte(addr_l + 2) == 0x8d && dGetByte(addr_l + 3) == 0x2a
		 && dGetByte(addr_l + 4) == 0x02
		 && dGetByte(addr_s)     == save_check_bytes[0]
		 && dGetByte(addr_s + 1) == save_check_bytes[1]
		 && dGetByte(addr_s + 2) == 0x20 && dGetByte(addr_s + 3) == 0x5c
		 && dGetByte(addr_s + 4) == 0xe4) {
			Atari800_AddEsc(addr_l, ESC_COPENLOAD, CASSETTE_LeaderLoad);
			Atari800_AddEsc(addr_s, ESC_COPENSAVE, CASSETTE_LeaderSave);
		}
		Atari800_AddEscRts(0xe459, ESC_SIOV, SIO);
		patched = TRUE;
	}
	else {
		Atari800_RemoveEsc(ESC_COPENLOAD);
		Atari800_RemoveEsc(ESC_COPENSAVE);
		Atari800_RemoveEsc(ESC_SIOV);
	};
	if (patched && machine_type == MACHINE_XLXE) {
		/* Disable Checksum Test */
		dPutByte(0xc314, 0x8e);
		dPutByte(0xc315, 0xff);
		dPutByte(0xc319, 0x8e);
		dPutByte(0xc31a, 0xff);
	}
}

extern void MacSoundReset(void);

void Warmstart(void)
{
	MacSoundReset();
	if (machine_type == MACHINE_OSA || machine_type == MACHINE_OSB) {
		/* RESET key in 400/800 does not reset chips,
		   but only generates RNMI interrupt */
		NMIST = 0x3f;
		NMI();
	}
	else {
		PIA_Reset();
		ANTIC_Reset();
		/* CPU_Reset() must be after PIA_Reset(),
		   because Reset routine vector must be read from OS ROM */
		CPU_Reset();
		/* note: POKEY and GTIA have no Reset pin */
	}
}

void Coldstart(void)
{
	MacSoundReset();
	PIA_Reset();
	ANTIC_Reset();
	/* CPU_Reset() must be after PIA_Reset(),
	   because Reset routine vector must be read from OS ROM */
	CPU_Reset();
	/* note: POKEY and GTIA have no Reset pin */
	/* reset cartridge to power-up state */
	CART_Start();
	/* set Atari OS Coldstart flag */
	dPutByte(0x244, 1);
	/* handle Option key (disable BASIC in XL/XE)
	   and Start key (boot from cassette) */
	consol_index = 2;
	consol_table[2] = 0x0f;
	if (disable_basic && !loading_basic) {
		/* hold Option during reboot */
		consol_table[2] &= ~CONSOL_OPTION;
	}
	if (hold_start) {
		/* hold Start during reboot */
		consol_table[2] &= ~CONSOL_START;
	}
	consol_table[1] = consol_table[2];
}

static int load_image(const char *filename, UBYTE *buffer, int nbytes)
{
	FILE *f;
	int len;

	f = fopen(filename, "rb");
	if (f == NULL) {
		Aprint("Error loading rom: %s", filename);
		return FALSE;
	}
	len = fread(buffer, 1, nbytes, f);
	fclose(f);
	if (len != nbytes) {
		Aprint("Error reading %s", filename);
		return FALSE;
	}
	return TRUE;
}

#include "emuos.h"

#define COPY_EMUOS(padding) do { \
		memset(atari_os, 0, padding); \
		memcpy(atari_os + (padding), emuos_h, 0x2000); \
	} while (0)

static int load_roms(void)
{
	switch (machine_type) {
	case MACHINE_OSA:
		if (emuos_mode == 2)
			COPY_EMUOS(0x0800);
		else if (!load_image(atari_osa_filename, atari_os, 0x2800)) {
			if (emuos_mode == 1)
				COPY_EMUOS(0x0800);
			else
				return FALSE;
		}
		else
			have_basic = load_image(atari_basic_filename, atari_basic, 0x2000);
		break;
	case MACHINE_OSB:
		if (emuos_mode == 2)
			COPY_EMUOS(0x0800);
		else if (!load_image(atari_osb_filename, atari_os, 0x2800)) {
			if (emuos_mode == 1)
				COPY_EMUOS(0x0800);
			else
				return FALSE;
		}
		else
			have_basic = load_image(atari_basic_filename, atari_basic, 0x2000);
		break;
	case MACHINE_XLXE:
		if (emuos_mode == 2)
			COPY_EMUOS(0x2000);
		else if (!load_image(atari_xlxe_filename, atari_os, 0x4000)) {
			if (emuos_mode == 1)
				COPY_EMUOS(0x2000);
			else
				return FALSE;
		}
		else if (!load_image(atari_basic_filename, atari_basic, 0x2000))
			return FALSE;
		xe_bank = 0;
		break;
	case MACHINE_5200:
		if (!load_image(atari_5200_filename, atari_os, 0x800))
			return FALSE;
		break;
	}
	return TRUE;
}

int Atari800_InitialiseMachine(void)
{
	Atari800_ClearAllEsc();
	if (!load_roms())
		return FALSE;
	MEMORY_InitialiseMachine();
	Device_UpdatePatches();
	return TRUE;
}

char *safe_strncpy(char *dest, const char *src, size_t size)
{
	strncpy(dest, src, size);
	dest[size - 1] = '\0';
	return dest;
}

int Atari800_Initialise(int *argc, char *argv[])
{
	int i, j;
	const char *rom_filename = NULL;
	const char *run_direct = NULL;
#ifndef BASIC
	const char *state_file = NULL;
#endif
	const char *rtconfig_filename = NULL;
	int config = FALSE;
	int help_only = FALSE;

	for (i = j = 1; i < *argc; i++) {
		if (strcmp(argv[i], "-configure") == 0) {
			config = TRUE;
		}
		else if (strcmp(argv[i], "-config") == 0) {
			rtconfig_filename = argv[++i];
		}
		else if (strcmp(argv[i], "-v") == 0 ||
				 strcmp(argv[i], "-version") == 0 ||
				 strcmp(argv[i], "--version") == 0) {
			printf("%s\n", ATARI_TITLE);
			return FALSE;
		}
		else if (strcmp(argv[i], "--usage") == 0 ||
				 strcmp(argv[i], "--help") == 0) {
			argv[j++] = "-help";
		}
		else if (strcmp(argv[i], "-verbose") == 0) {
			verbose = TRUE;
		}
		else {
			argv[j++] = argv[i];
		}
	}

	*argc = j;

	if (!RtConfigLoad(rtconfig_filename))
		config = TRUE;

	if (config) {

#ifndef DONT_USE_RTCONFIGUPDATE
		RtConfigUpdate();
#endif /* !DONT_USE_RTCONFIGUPDATE */

		RtConfigSave();
	}

	for (i = j = 1; i < *argc; i++) {
		if (strcmp(argv[i], "-atari") == 0) {
			if (machine_type != MACHINE_OSA) {
				machine_type = MACHINE_OSB;
				ram_size = 48;
			}
		}
		else if (strcmp(argv[i], "-xl") == 0) {
			machine_type = MACHINE_XLXE;
			ram_size = 64;
		}
		else if (strcmp(argv[i], "-xe") == 0) {
			machine_type = MACHINE_XLXE;
			ram_size = 128;
		}
		else if (strcmp(argv[i], "-320xe") == 0) {
			machine_type = MACHINE_XLXE;
			ram_size = RAM_320_COMPY_SHOP;
		}
		else if (strcmp(argv[i], "-rambo") == 0) {
			machine_type = MACHINE_XLXE;
			ram_size = RAM_320_RAMBO;
		}
		else if (strcmp(argv[i], "-5200") == 0) {
			machine_type = MACHINE_5200;
			ram_size = 16;
		}
		else if (strcmp(argv[i], "-nobasic") == 0)
			disable_basic = TRUE;
		else if (strcmp(argv[i], "-basic") == 0)
			disable_basic = FALSE;
		else if (strcmp(argv[i], "-nopatch") == 0)
			enable_sio_patch = FALSE;
		else if (strcmp(argv[i], "-nopatchall") == 0)
			enable_sio_patch = enable_h_patch = enable_p_patch = enable_r_patch = FALSE;
		else if (strcmp(argv[i], "-pal") == 0)
			tv_mode = TV_PAL;
		else if (strcmp(argv[i], "-ntsc") == 0)
			tv_mode = TV_NTSC;
		else if (strcmp(argv[i], "-a") == 0) {
			machine_type = MACHINE_OSA;
			ram_size = 48;
		}
		else if (strcmp(argv[i], "-b") == 0) {
			machine_type = MACHINE_OSB;
			ram_size = 48;
		}
		else if (strcmp(argv[i], "-emuos") == 0)
			emuos_mode = 2;
		else if (strcmp(argv[i], "-c") == 0) {
			if (ram_size == 48)
				ram_size = 52;
		}
		else {
			/* parameters that take additional argument follow here */
			int i_a = (i + 1 < *argc);		/* is argument available? */
			int a_m = FALSE;				/* error, argument missing! */

			if (strcmp(argv[i], "-osa_rom") == 0) {
				if (i_a) safe_strncpy(atari_osa_filename, argv[++i], sizeof(atari_osa_filename)); else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-osb_rom") == 0) {
				if (i_a) safe_strncpy(atari_osb_filename, argv[++i], sizeof(atari_osb_filename)); else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-xlxe_rom") == 0) {
				if (i_a) safe_strncpy(atari_xlxe_filename, argv[++i], sizeof(atari_xlxe_filename)); else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-5200_rom") == 0) {
				if (i_a) safe_strncpy(atari_5200_filename, argv[++i], sizeof(atari_5200_filename)); else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-basic_rom") == 0) {
				if (i_a) safe_strncpy(atari_basic_filename, argv[++i], sizeof(atari_basic_filename)); else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-cart") == 0) {
				if (i_a) rom_filename = argv[++i]; else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-run") == 0) {
				if (i_a) run_direct = argv[++i]; else a_m = TRUE;
			}
#ifndef BASIC
			/* The BASIC version does not support state files, because:
			   1. It has no ability to save state files, because of lack of UI.
			   2. It uses a simplified emulation, so the state files would be
			      incompatible with other versions.
			   3. statesav is not compiled in to make the executable smaller. */
			else if (strcmp(argv[i], "-state") == 0) {
				if (i_a) state_file = argv[++i]; else a_m = TRUE;
			}
			else if (strcmp(argv[i], "-refresh") == 0) {
				if (i_a) {
					sscanf(argv[++i], "%d", &refresh_rate);
					if (refresh_rate < 1)
						refresh_rate = 1;
				}
				else
					a_m = TRUE;
			}
#endif
			else {
				/* all options known to main module tried but none matched */

				if (strcmp(argv[i], "-help") == 0) {
					help_only = TRUE;
					Aprint("\t-configure       Update Configuration File");
					Aprint("\t-config <file>   Specify Alternate Configuration File");
					Aprint("\t-atari           Emulate Atari 800");
					Aprint("\t-xl              Emulate Atari 800XL");
					Aprint("\t-xe              Emulate Atari 130XE");
					Aprint("\t-320xe           Emulate Atari 320XE (COMPY SHOP)");
					Aprint("\t-rambo           Emulate Atari 320XE (RAMBO)");
					Aprint("\t-5200            Emulate Atari 5200 Games System");
					Aprint("\t-nobasic         Turn off Atari BASIC ROM");
					Aprint("\t-basic           Turn on Atari BASIC ROM");
					Aprint("\t-pal             Enable PAL TV mode");
					Aprint("\t-ntsc            Enable NTSC TV mode");
					Aprint("\t-osa_rom <file>  Load OS A ROM from file");
					Aprint("\t-osb_rom <file>  Load OS B ROM from file");
					Aprint("\t-xlxe_rom <file> Load XL/XE ROM from file");
					Aprint("\t-5200_rom <file> Load 5200 ROM from file");
					Aprint("\t-basic_rom <fil> Load BASIC ROM from file");
					Aprint("\t-cart <file>     Install cartridge (raw or CART format)");
					Aprint("\t-run <file>      Run Atari program (COM, EXE, XEX, BAS, LST)");
#ifndef BASIC
					Aprint("\t-state <file>    Load saved-state file");
					Aprint("\t-refresh <rate>  Specify screen refresh rate");
#endif
					Aprint("\t-nopatch         Don't patch SIO routine in OS");
					Aprint("\t-nopatchall      Don't patch OS at all, H: device won't work");
					Aprint("\t-a               Use OS A");
					Aprint("\t-b               Use OS B");
					Aprint("\t-c               Enable RAM between 0xc000 and 0xcfff in Atari 800");
					Aprint("\t-v               Show version/release number");
				}

				/* copy this option for platform/module specific evaluation */
				argv[j++] = argv[i];
			}

			/* this is the end of the additional argument check */
			if (a_m) {
				printf("Missing argument for '%s'\n", argv[i]);
				return FALSE;
			}
		}
	}

	*argc = j;

	if (tv_mode == TV_PAL)
		deltatime = (1.0 / 50.0);
	else
		deltatime = (1.0 / 60.0);

#if !defined(BASIC) && !defined(CURSES_BASIC)
	Palette_Initialise(argc, argv);
#endif
	Device_Initialise(argc, argv);
	RTIME_Initialise(argc, argv);
	SIO_Initialise (argc, argv);
	CASSETTE_Initialise(argc, argv);
#ifndef BASIC
	INPUT_Initialise(argc, argv);
#endif
#ifndef DONT_DISPLAY
	/* Platform Specific Initialisation */
	Atari_Initialise(argc, argv);
#endif
#if !defined(BASIC) && !defined(CURSES_BASIC)
	Screen_Initialise(argc, argv);
#endif
	/* Initialise Custom Chips */
	ANTIC_Initialise(argc, argv);
	GTIA_Initialise(argc, argv);
	PIA_Initialise(argc, argv);
	POKEY_Initialise(argc, argv);

	if (help_only) {
		Atari800_Exit(FALSE);
		return FALSE;
	}

	/* Any parameters left on the command line must be disk images */
	for (i = 1; i < *argc; i++) {
		if (i > 8) {
			Aprint("Too many disk image filenames on the command line (max. 8).");
			break;
		}
		if (!SIO_Mount(i, argv[i], FALSE)) {
			Aprint("Disk image \"%s\" not found", argv[i]);
		}
	}

	/* Configure Atari System */
	Atari800_InitialiseMachine();

	/* Install requested ROM cartridge */
	if (rom_filename) {
		int r = CART_Insert(rom_filename);
		if (r < 0) {
			Aprint("Error inserting cartridge \"%s\": %s", rom_filename,
			r == CART_CANT_OPEN ? "Can't open file" :
			r == CART_BAD_FORMAT ? "Bad format" :
			r == CART_BAD_CHECKSUM ? "Bad checksum" :
			"Unknown error");
		}
		if (r > 0) {
#ifdef BASIC
			Aprint("Raw cartridge images not supported in BASIC version!");
#else
			ui_is_active = TRUE;
			cart_type = SelectCartType(r);
			ui_is_active = FALSE;
			CART_Start();
#endif
		}
		if (cart_type != CART_NONE) {
			int for5200 = CART_IsFor5200(cart_type);
			if (for5200 && machine_type != MACHINE_5200) {
				machine_type = MACHINE_5200;
				ram_size = 16;
				Atari800_InitialiseMachine();
			}
			else if (!for5200 && machine_type == MACHINE_5200) {
				machine_type = MACHINE_XLXE;
				ram_size = 64;
				Atari800_InitialiseMachine();
			}
		}
	}

	/* Load Atari executable, if any */
	if (run_direct != NULL)
		BIN_loader(run_direct);

#ifndef BASIC
	/* Load state file */
	if (state_file != NULL) {
		if (ReadAtariState(state_file, "rb"))
			/* Don't press Option */
			consol_table[1] = consol_table[2] = 0x0f;
	}
#endif

#ifdef HAVE_SIGNAL
	/* Install CTRL-C Handler */
	signal(SIGINT, sigint_handler);
#endif

#ifdef BENCHMARK
	benchmark_start_time = Atari_time();
#endif

	return TRUE;
}

int Atari800_Exit(int run_monitor)
{
	int restart;
	if (verbose) {
		Aprint("Current frames per second: %f", fps);
	}
	restart = Atari_Exit(run_monitor);
	if (!restart) {
		SIO_Exit();	/* umount disks, so temporary files are deleted */
#ifdef SOUND
		CloseSoundFile();
#endif
	}
	return restart;
}

UBYTE Atari800_GetByte(UWORD addr)
{
	UBYTE byte = 0xff;
	switch (addr & 0xff00) {
	case 0x4f00:
	case 0x8f00:
		CART_BountyBob1(addr);
		byte = 0;
		break;
	case 0x5f00:
	case 0x9f00:
		CART_BountyBob2(addr);
		byte = 0;
		break;
	case 0xd000:				/* GTIA */
	case 0xc000:				/* GTIA - 5200 */
		byte = GTIA_GetByte(addr);
		break;
	case 0xd200:				/* POKEY */
	case 0xe800:				/* POKEY - 5200 */
	case 0xeb00:				/* POKEY - 5200 */
		byte = POKEY_GetByte(addr);
		break;
	case 0xd300:				/* PIA */
		byte = PIA_GetByte(addr);
		break;
	case 0xd400:				/* ANTIC */
		byte = ANTIC_GetByte(addr);
		break;
	case 0xd500:				/* bank-switching cartridges, RTIME-8 */
		byte = CART_GetByte(addr);
		break;
	default:
		break;
	}

	return byte;
}

void Atari800_PutByte(UWORD addr, UBYTE byte)
{
	switch (addr & 0xff00) {
	case 0x4f00:
	case 0x8f00:
		CART_BountyBob1(addr);
		break;
	case 0x5f00:
	case 0x9f00:
		CART_BountyBob2(addr);
		break;
	case 0xd000:				/* GTIA */
	case 0xc000:				/* GTIA - 5200 */
		GTIA_PutByte(addr, byte);
		break;
	case 0xd200:				/* POKEY */
	case 0xe800:				/* POKEY - 5200 AAA added other pokey space */
	case 0xeb00:				/* POKEY - 5200 */
		POKEY_PutByte(addr, byte);
		break;
	case 0xd300:				/* PIA */
		PIA_PutByte(addr, byte);
		break;
	case 0xd400:				/* ANTIC */
		ANTIC_PutByte(addr, byte);
		break;
	case 0xd500:				/* bank-switching cartridges, RTIME-8 */
		CART_PutByte(addr, byte);
		break;
	default:
		break;
	}
}

void Atari800_UpdatePatches(void)
{
	switch (machine_type) {
	case MACHINE_OSA:
	case MACHINE_OSB:
		/* Restore unpatched OS */
		dCopyToMem(atari_os, 0xd800, 0x2800);
		/* Set patches */
		Atari800_PatchOS();
		Device_UpdatePatches();
		break;
	case MACHINE_XLXE:
		/* Don't patch if OS disabled */
		if ((PORTB & 1) == 0)
			break;
		/* Restore unpatched OS */
		dCopyToMem(atari_os, 0xc000, 0x1000);
		dCopyToMem(atari_os + 0x1800, 0xd800, 0x2800);
		/* Set patches */
		Atari800_PatchOS();
		Device_UpdatePatches();
		break;
	default:
		break;
	}
}

#ifndef USE_CLOCK

static double Atari_time(void)
{
#ifdef DJGPP
	/* DJGPP has gettimeofday, but it's not more accurate than uclock */
	return uclock() * (1.0 / UCLOCKS_PER_SEC);
#elif defined(WIN32)
	return GetTickCount() * 1e-3;
#elif defined(HAVE_GETTIMEOFDAY)
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec + 1e-6 * tp.tv_usec;
#elif defined(HAVE_UCLOCK)
	return uclock() * (1.0 / UCLOCKS_PER_SEC);
#elif defined(HAVE_CLOCK)
#define USE_CLOCK
	return clock() * (1.0 / CLK_TCK);
#else
#error No function found for Atari_time()
#endif
}

#endif /* USE_CLOCK */

#ifndef USE_CLOCK

static void Atari_sleep(double s)
{
	if (s > 0) {
#ifdef DJGPP
		/* DJGPP has usleep and select, but they don't work that good */
		/* XXX: find out why */
		double curtime = Atari_time();
		while ((curtime + s) > Atari_time());
#elif defined(HAVE_USLEEP)
		usleep(s * 1e6);
#elif defined(__BEOS__)
		/* added by Walter Las for BeOS */
		snooze(s * 1e6);
#elif defined(__EMX__)
		/* added by Brian Smith for os/2 */
		DosSleep(s);
#elif defined(WIN32)
		Sleep((DWORD) (s * 1e3));
#elif defined(HAVE_SELECT)
		/* linux */
		struct timeval tp;
		tp.tv_sec = 0;
		tp.tv_usec = 1e6 * s;
		select(1, NULL, NULL, NULL, &tp);
#else
		double curtime = Atari_time();
		while ((curtime + s) > Atari_time());
#endif
	}
}

#endif /* USE_CLOCK */

void atari_sync(void)
{
#ifdef USE_CLOCK
	static ULONG nextclock = 1;	/* put here a non-zero value to enable speed regulator */
	/* on Atari Falcon CLK_TCK = 200 (i.e. 5 ms granularity) */
	/* on DOS (DJGPP) CLK_TCK = 91 (not too precise, but should work anyway) */
	if (nextclock) {
		ULONG curclock;
		do {
			curclock = clock();
		} while (curclock < nextclock);

		nextclock = curclock + (CLK_TCK / (tv_mode == TV_PAL ? 50 : 60));
	}
#else /* USE_CLOCK */
	static double lasttime = 0, lastcurtime = 0;
	double curtime;

	if (deltatime > 0.0) {
		curtime = Atari_time();
		Atari_sleep(lasttime + deltatime - curtime);
		curtime = Atari_time();

		/* make average time */
		frametime = (frametime * 4.0 + curtime - lastcurtime) * 0.2;
		fps = 1.0 / frametime;
		lastcurtime = curtime;

		lasttime += deltatime;
		if ((lasttime + deltatime) < curtime)
			lasttime = curtime;
	}
	percent_atari_speed = (int) (100.0 * deltatime / frametime + 0.5);
#endif /* USE_CLOCK */
}

#ifdef USE_CURSES
void curses_clear_screen(void);
#endif

#if defined(BASIC) || defined(VERY_SLOW) || defined(CURSES_BASIC)

#ifdef CURSES_BASIC
void curses_display_line(int anticmode, const UBYTE *screendata);
#endif

static int scanlines_to_dl;

/* steal cycles and generate DLI */
static void basic_antic_scanline(void)
{
	static UBYTE IR = 0;
	static const UBYTE mode_scanlines[16] =
		{ 0, 0, 8, 10, 8, 16, 8, 16, 8, 4, 4, 2, 1, 2, 1, 1 };
	static const UBYTE mode_bytes[16] =
		{ 0, 0, 40, 40, 40, 40, 20, 20, 10, 10, 20, 20, 20, 40, 40, 40 };
	static const UBYTE font40_cycles[4] = { 0, 32, 40, 47 };
	static const UBYTE font20_cycles[4] = { 0, 16, 20, 24 };
#ifdef CURSES_BASIC
	static int scanlines_to_curses_display = 0;
	static UWORD screenaddr = 0;
	static UWORD newscreenaddr = 0;
#endif

	int bytes = 0;
	if (--scanlines_to_dl <= 0) {
		if (DMACTL & 0x20) {
			IR = ANTIC_GetDLByte(&dlist);
			xpos++;
		}
		else
			IR &= 0x7f;	/* repeat last instruction, but don't generate DLI */
		switch (IR & 0xf) {
		case 0:
			scanlines_to_dl = ((IR >> 4) & 7) + 1;
			break;
		case 1:
			if (DMACTL & 0x20) {
				dlist = ANTIC_GetDLWord(&dlist);
				xpos += 2;
			}
			scanlines_to_dl = (IR & 0x40) ? 1024 /* no more DL in this frame */ : 1;
			break;
		default:
			if (IR & 0x40 && DMACTL & 0x20) {
#ifdef CURSES_BASIC
				screenaddr =
#endif
					ANTIC_GetDLWord(&dlist);
				xpos += 2;
			}
			/* can't steal cycles now, because DLI must come first */
			/* just an approximation: doesn't check HSCROL */
			switch (DMACTL & 3) {
			case 1:
				bytes = mode_bytes[IR & 0xf] * 8 / 10;
				break;
			case 2:
				bytes = mode_bytes[IR & 0xf];
				break;
			case 3:
				bytes = mode_bytes[IR & 0xf] * 12 / 10;
				break;
			default:
				break;
			}
#ifdef CURSES_BASIC
			/* Normally, we would call curses_display_line here,
			   and not use scanlines_to_curses_display at all.
			   That would however cause incorrect color of the "MEMORY"
			   menu item in Self Test - it isn't set properly
			   in the first scanline. We therefore postpone
			   curses_display_line call to the next scanline. */
			scanlines_to_curses_display = 2;
			newscreenaddr = screenaddr + bytes;
#endif
			/* just an approximation: doesn't check VSCROL */
			scanlines_to_dl = mode_scanlines[IR & 0xf];
			break;
		}
	}
	if (scanlines_to_dl == 1 && (IR & 0x80)) {
		GO(NMIST_C);
		NMIST = 0x9f;
		if (NMIEN & 0x80) {
			GO(NMI_C);
			NMI();
		}
	}
#ifdef CURSES_BASIC
	if (--scanlines_to_curses_display == 0) {
		curses_display_line(IR & 0xf, memory + screenaddr);
		/* 4k wrap */
		if (((screenaddr ^ newscreenaddr) & 0x1000) != 0)
			screenaddr = newscreenaddr - 0x1000;
		else
			screenaddr = newscreenaddr;
	}
#endif
	xpos += bytes;
	/* steal cycles in font modes */
	switch (IR & 0xf) {
	case 2:
	case 3:
	case 4:
	case 5:
		xpos += font40_cycles[DMACTL & 3];
		break;
	case 6:
	case 7:
		xpos += font20_cycles[DMACTL & 3];
		break;
	default:
		break;
	}
}

#define BASIC_LINE GO(LINE_C); xpos -= LINE_C - DMAR; screenline_cpu_clock += LINE_C; ypos++

static void basic_frame(void)
{
	/* scanlines 0 - 7 */
	ypos = 0;
	do {
		POKEY_Scanline();		/* check and generate IRQ */
		BASIC_LINE;
	} while (ypos < 8);

	scanlines_to_dl = 1;
	/* scanlines 8 - 247 */
	do {
		POKEY_Scanline();		/* check and generate IRQ */
		basic_antic_scanline();
		BASIC_LINE;
	} while (ypos < 248);

	/* scanline 248 */
	POKEY_Scanline();			/* check and generate IRQ */
	GO(NMIST_C);
	NMIST = 0x5f;				/* Set VBLANK */
	if (NMIEN & 0x40) {
		GO(NMI_C);
		NMI();
	}
	BASIC_LINE;

	/* scanlines 249 - 261(311) */
	do {
		POKEY_Scanline();		/* check and generate IRQ */
		BASIC_LINE;
	} while (ypos < max_ypos);
}

#endif /* defined(BASIC) || defined(VERY_SLOW) || defined(CURSES_BASIC) */

void Atari800_Frame(void)
{
#ifndef BASIC
	static int refresh_counter = 0;
	switch (key_code) {
	case AKEY_COLDSTART:
		Coldstart();
		break;
	case AKEY_WARMSTART:
		Warmstart();
		break;
	case AKEY_EXIT:
		Atari800_Exit(FALSE);
		exit(0);
	case AKEY_UI:
#ifdef SOUND
		Sound_Pause();
#endif
		ui();
#ifdef SOUND
		Sound_Continue();
#endif
		frametime = deltatime;
		break;
#ifndef CURSES_BASIC
	case AKEY_SCREENSHOT:
		Screen_SaveNextScreenshot(FALSE);
		break;
	case AKEY_SCREENSHOT_INTERLACE:
		Screen_SaveNextScreenshot(TRUE);
		break;
#endif /* CURSES_BASIC */
	default:
		break;
	}
#endif /* BASIC */

	Device_Frame();
#ifndef BASIC
	INPUT_Frame();
#endif
	GTIA_Frame();
#ifdef SOUND
	Sound_Update();
#endif

#ifdef BASIC
	basic_frame();
#else /* BASIC */
	if (++refresh_counter >= refresh_rate) {
		refresh_counter = 0;
#ifdef USE_CURSES
		curses_clear_screen();
#endif
#ifdef CURSES_BASIC
		basic_frame();
#else
		ANTIC_Frame(TRUE);
		INPUT_DrawMousePointer();
		Screen_DrawAtariSpeed();
		Screen_DrawDiskLED();
#endif /* CURSES_BASIC */
#ifdef DONT_DISPLAY
		display_screen = FALSE;
#else
		display_screen = TRUE;
#endif /* DONT_DISPLAY */
	}
	else {
#if defined(VERY_SLOW) || defined(CURSES_BASIC)
		basic_frame();
#else
		ANTIC_Frame(sprite_collisions_in_skipped_frames);
#endif
		display_screen = FALSE;
	}
#endif /* BASIC */

	POKEY_Frame();
	nframes++;

#ifdef BENCHMARK
	if (nframes >= BENCHMARK) {
		double benchmark_time = Atari_time() - benchmark_start_time;
		Atari800_Exit(FALSE);
		printf("%d frames emulated in %.2f seconds\n", BENCHMARK, benchmark_time);
		exit(0);
	}
#else

#ifndef DONT_SYNC_WITH_HOST
	atari_sync();
#endif

#endif /* BENCHMARK */
}

/* Opens a new temporary file and fills in filename with its name.
   filename must point to FILENAME_MAX characters buffer, but doesn't need
   to be initialized. */
FILE *Atari_tmpfile(char *filename, const char *mode)
{
	/* We cannot simply call tmpfile(), because we don't want the file
	   to be deleted when we close it, and we need the filename. */

#if defined(HAVE_MKSTEMP) && defined(HAVE_FDOPEN)
	/* this is the only implementation without a race condition */
	strcpy(filename, "a8XXXXXX");
	/* mkstemp() modifies the 'X'es and returns an open descriptor */
	return fdopen(mkstemp(filename), mode);
#elif defined(HAVE_TMPNAM)
	/* tmpnam() is better than mktemp(), because it creates filenames
	   in system's temporary directory. It is also more portable. */
	return fopen(tmpnam(filename), mode);
#elif defined(HAVE_MKTEMP)
	strcpy(filename, "a8XXXXXX");
	/* mktemp() modifies the 'X'es and returns filename */
	return fopen(mktemp(filename), mode);
#else
	/* Roll-your-own */
	int no;
	for (no = 0; no < 1000000; no++) {
		FILE *fp;
		sprintf(filename, "a8%06d", no);
		fp = fopen(filename, "rb");
		if (fp == NULL)
			return fopen(filename, mode);
		fclose(fp);
	}
	return NULL;
#endif
}

#ifndef BASIC

void MainStateSave(void)
{
	UBYTE temp;
	int default_tv_mode;
	int os = 0;
	int default_system = 3;
	int pil_on = FALSE;

	if (tv_mode == TV_PAL) {
		temp = 0;
		default_tv_mode = 1;
	}
	else {
		temp = 1;
		default_tv_mode = 2;
	}
	SaveUBYTE(&temp, 1);

	switch (machine_type) {
	case MACHINE_OSA:
		temp = ram_size == 16 ? 5 : 0;
		os = 1;
		default_system = 1;
		break;
	case MACHINE_OSB:
		temp = ram_size == 16 ? 5 : 0;
		os = 2;
		default_system = 2;
		break;
	case MACHINE_XLXE:
		switch (ram_size) {
		case 16:
			temp = 6;
			default_system = 3;
			break;
		case 64:
			temp = 1;
			default_system = 3;
			break;
		case 128:
			temp = 2;
			default_system = 4;
			break;
		case 192:
			temp = 9;
			default_system = 8;
			break;
		case RAM_320_RAMBO:
		case RAM_320_COMPY_SHOP:
			temp = 3;
			default_system = 5;
			break;
		case 576:
			temp = 7;
			default_system = 6;
			break;
		case 1088:
			temp = 8;
			default_system = 7;
			break;
		}
		break;
	case MACHINE_5200:
		temp = 4;
		default_system = 6;
		break;
	}
	SaveUBYTE(&temp, 1);

	SaveINT(&os, 1);
	SaveINT(&pil_on, 1);
	SaveINT(&default_tv_mode, 1);
	SaveINT(&default_system, 1);
}

void MainStateRead(void)
{
	/* these are all for compatibility with previous versions */
	UBYTE temp;
	int default_tv_mode;
	int os;
	int default_system;
	int pil_on;

	ReadUBYTE(&temp, 1);
	tv_mode = (temp == 0) ? TV_PAL : TV_NTSC;

	ReadUBYTE(&temp, 1);
	ReadINT(&os, 1);
	switch (temp) {
	case 0:
		machine_type = os == 1 ? MACHINE_OSA : MACHINE_OSB;
		ram_size = 48;
		break;
	case 1:
		machine_type = MACHINE_XLXE;
		ram_size = 64;
		break;
	case 2:
		machine_type = MACHINE_XLXE;
		ram_size = 128;
		break;
	case 3:
		machine_type = MACHINE_XLXE;
		ram_size = RAM_320_COMPY_SHOP;
		break;
	case 4:
		machine_type = MACHINE_5200;
		ram_size = 16;
		break;
	case 5:
		machine_type = os == 1 ? MACHINE_OSA : MACHINE_OSB;
		ram_size = 16;
		break;
	case 6:
		machine_type = MACHINE_XLXE;
		ram_size = 16;
		break;
	case 7:
		machine_type = MACHINE_XLXE;
		ram_size = 576;
		break;
	case 8:
		machine_type = MACHINE_XLXE;
		ram_size = 1088;
		break;
	case 9:
		machine_type = MACHINE_XLXE;
		ram_size = 192;
		break;
	default:
		machine_type = MACHINE_XLXE;
		ram_size = 64;
		Aprint("Warning: Bad machine type read in from state save, defaulting to 800 XL");
		break;
	}

	ReadINT(&pil_on, 1);
	ReadINT(&default_tv_mode, 1);
	ReadINT(&default_system, 1);
	load_roms();
	/* XXX: what about patches? */
}

#endif
