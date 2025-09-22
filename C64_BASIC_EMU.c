// A VIC-64 BASIC emulator. With custom stuff to be usable without interrupt and CIA chips and a CPU power saver (the 6502 is only running if it isn't waiting for a keypress in the BASIC).
// Has support for the C64 memory maps for the CPU, provided by the PLA in the C64.
// Goal: Make a C64 BASIC emulator that can run on a MCU, that has C64 cartridge support for some fun hacking.
// The reason for making this emulator to work on Linux is only to iterate the development and debugging faster at this point in time. 
// Current state: Can be tested by writing BASIC programs in the C64 prompt. Prints text in the linux command prompt for easier development of further features.

// Ways to go...
// * Connect a real C64 keyboard using a raspberry pico 2.
// * Patch the kernal to generic stuff that can be done on a MCU.
// * Emulate the CIA chips.
// * Modify the KERNAL to be usable by raspberry pico 2.

// Generic C stuff...
#include <stdint.h>
#include <stdio.h>

// ikiGUI settings...
#define IKIGUI_STANDALONE
#define WIN_WIDTH  320	// 8*40
#define WIN_HEIGHT 200	// 8*25
#include "ikigui.h"	// To open a window to get something to draw to.
ikigui_window mywin;	// A stuct for the window and the used lib.
ikigui_image font;	// Global graphics for monospace text characters.
ikigui_map font_map;	// for textbased statusbar for debugging.
ikigui_image bg;	// Global graphics for background art.

// Emulator stuff...
#include "basic.h"	// BASIC     ROM
#include "kernal.h"	// Kernal    ROM
#include "characters.h" // Character ROM 
//#include "diagC64.h"	// Cart      ROM

#define VIDEOADDR 0x400			// Start of video buffer in the address space.
uint8_t sysram[0x10000];		// 64kb RAM
uint8_t color_ram[1024];		// VIC-II extrenal RAM
uint8_t cia_1_port_a_data ;		// Port A
uint8_t cia_1_port_b_data ;		// Port B
uint8_t cia_1_port_a_direction ;	// Port A
uint8_t cia_1_port_b_direction ;	// Port B
uint8_t shaddow_io[0x1000] ;		// Writes to Hardware saved like in a hacking cartridge.

// Define external C64 palette - Possible future, make the colors address maped into the C64 memory space, a great easy upgrade of the C64.
const unsigned int c64_palette[16] = {
    0xFF000000, // 0 black
    0xFFFFFFFF, // 1 white
    0xFF68372B, // 2 red
    0xFF70A4B2, // 3 cyan
    0xFF6F3D86, // 4 purple
    0xFF588D43, // 5 green
    0xFF352879, // 6 blue
    0xFFB8C76F, // 7 yellow
    0xFF6F4F25, // 8 orange
    0xFF433900, // 9 brown
    0xFF9A6759, // 10 light red
    0xFF444444, // 11 dark gray
    0xFF6C6C6C, // 12 medium gray
    0xFF9AD284, // 13 light green
    0xFF6C5EB5, // 14 light blue
    0xFF959595  // 15 light gray
};

// **********************************************
// For the 6502 emulation ...
void    write6502(uint16_t address, uint8_t value);
uint8_t read6502(uint16_t address);
#include "cpu_c.c"

uint8_t read6502(uint16_t address){
	// Reference for making a full cart support...
	// -------------------------------------------
	// Cart mode is selected by 2 pins att the port /GAME(pin8) & /EXROM(pin9), that defaults to 11 (using pull up resistors) if no cart is present,
		// 01 -  8K Cart normal mode.	0x8000 - 0x9FFF <- a 8KB ROM in cart.
		// 10 - Ultimax cart mode
		// 00 - 16K Cart xy mode. Additional 0xA000 - 0xBFFF <- 8KB ROM
		// 11 - No Cart mode

	// UltiMAX map, looks to work... -- Fix the correct mirroring
	//	if (address >= 0xE000 && address <= 0xFFFF) return deadTest[address - 0xE000];
	//	if (address >= 0xD000 && address <= 0xDFFF) goto HARDWARE; 			// I/O Hardware registers.
	//	return sysram[address];								// RAM

	// For a PLA Logic equivalent memory map... Has been tested with 8K cartridges...
	switch(sysram[1]&0x7){ // PLA logic... Tere is no break in this switch. So it will fall trough at the if cases if is's not true.
		case 7: 
			//if (address >= 0x8000 && address <= 0x9FFF) return diagC64[address - 0x8000]; 	// 8K Cart ROM
			if (address >= 0xA000 && address <= 0xBFFF) return basic[ address - 0xA000]; 		// BASIC ROM
		case 6: if (address >= 0xE000 && address <= 0xFFFF) return kernal[address - 0xE000]; 		// KERNAL ROM - This is replaced in Ultimax mode with a cart ROM at the same address.
		case 5: if (address >= 0xD000 && address <= 0xDFFF) goto HARDWARE; 				// I/O Hardware registers.
		case 4: return sysram[address];									// RAM
		case 3: 
			//if (address >= 0x8000 && address <= 0x9FFF) return diagC64[address - 0x8000]; 	// 8K Cart ROM
			if (address >= 0xA000 && address <= 0xBFFF) return basic[ address - 0xA000]; 		// BASIC ROM
		case 2: if (address >= 0xE000 && address <= 0xFFFF) return kernal[address - 0xE000]; 		// KERNAL ROM
		case 1: if (address >= 0xD000 && address <= 0xDFFF) return characters[address - 0xD000]; 	// CHARACTER ROM
		case 0: return sysram[address];									// RAM
	}
	
	// *********************************************************************************************************************
	// I/O Registers... Can only come here by falling through to and/or run case 5.
	HARDWARE: // 

		// Color RAM... 
		if (address >= 0xD800 && address <= 0xDBFF){ // not mirrored!
			// printf("Read Color RAM, ");
			return color_ram[address - 0xD800] ; // Read Color RAM (4 bit RAM for each char).
		}

		// SID registers is officially mapped to $D400–$D41C. Mirrored Range: This means the 29 registers repeat every 32 bytes ($20 in hex) within this range.
		if(address >= 0xD400 && address <= 0xD7FF){ // The complete range for the SID-chip. 1 KB. 
			printf("SID Read - from 0x%4X\n", address); return 0;
			switch(address & 0xF71F){ // if (address >= 0xD400 && address <= 0xD41C){
				case 0xD400: printf("Voice 1 Frequency Low\n");		return 0;
				case 0xD401: printf("Voice 1 Frequency High\n");	return 0;
				case 0xD402: printf("Voice 1 Pulse Width Low\n");	return 0;
				case 0xD403: printf("Voice 1 Pulse Width High\n");	return 0;
				case 0xD404: printf("Voice 1 Control Register\n");	return 0;
				case 0xD405: printf("Voice 1 Attack/Decay\n");		return 0;
				case 0xD406: printf("Voice 1 Sustain/Release\n");	return 0;

				case 0xD407: printf("Voice 2 Frequency Low\n");		return 0;
				case 0xD408: printf("Voice 2 Frequency High\n");	return 0;
				case 0xD409: printf("Voice 2 Pulse Width Low\n");	return 0;
				case 0xD40A: printf("Voice 2 Pulse Width High\n");	return 0;
				case 0xD40B: printf("Voice 2 Control Register\n");	return 0;
				case 0xD40C: printf("Voice 2 Attack/Decay\n");		return 0;
				case 0xD40D: printf("Voice 2 Sustain/Release\n");	return 0;

				case 0xD40E: printf("Voice 3 Frequency Low\n");		return 0;
				case 0xD40F: printf("Voice 3 Frequency High\n");	return 0;
				case 0xD410: printf("Voice 3 Pulse Width Low\n");	return 0;
				case 0xD411: printf("Voice 3 Pulse Width High\n");	return 0;
				case 0xD412: printf("Voice 3 Control Register\n");	return 0;
				case 0xD413: printf("Voice 3 Attack/Decay\n");		return 0;
				case 0xD414: printf("Voice 3 Sustain/Release\n");	return 0;

				case 0xD415: printf("Filter Cutoff Low\n");		return 0;
				case 0xD416: printf("Filter Cutoff High\n");		return 0;
				case 0xD417: printf("Filter Resonance/Routing\n");	return 0;
				case 0xD418: printf("Volume and Filter Mode. Filter Control / Voice 3 Ring Mod / Sync / Test\n"); return 0;

				case 0xD419: printf("Paddle X\n");			return 0;
				case 0xD41A: printf("Paddle Y\n");			return 0;
				case 0xD41B: printf("Voice 3 Oscillator Output\n");	return 0;	// only Readable C64 registers in SID is...according to chatGPT the only Readable register 1/4... $D41B (POTX) – Analog input from control port (paddle/mouse).		Probably false address!
				case 0xD41C: printf("Voice 3 Envelope Output\n");	return 0;	// only Readable C64 registers in SID is...according to chatGPT the only Readable register 2/4... $D41C (POTY) – Analog input from control port.			Probably false address!
				case 0xD41D: printf("No Reg\n");			return 0;	// only Readable C64 registers in SID is...according to chatGPT the only Readable register 3/4... $D41D (OSC3) – Waveform output from oscillator 3 (8-bit value).	Probably false address!
				case 0xD41E: printf("No Reg\n");			return 0;	// only Readable C64 registers in SID is...according to chatGPT the only Readable register 4/4... $D41E (ENV3) – Envelope generator output from voice 3 (8-bit value).	Probably false address!
				case 0xD41F: printf("No Reg\n");			return 0;
			}
		}

		// VIC-II registers
		if (address >= 0xD000 && address <= 0xD3FF){ 
			printf("VIC-II Read  - from 0x%X ",address); fflush(stdout);  // Forces the buffer to flush immediately
			switch (address & 0xC03F){
				case 0xD000: printf("X-coord Sprite 0\n");	return 0; //
				case 0xD001: printf("Y-coord Sprite 0\n");	return 0; //
				case 0xD002: printf("X-Coord Sprite 1\n");	return 0; //
				case 0xD003: printf("Y-Coord Sprite 1\n");	return 0; //
				case 0xD004: printf("X-Coord Sprite 2\n");	return 0; //
				case 0xD005: printf("Y-Coord Sprite 2\n");	return 0; //
				case 0xD006: printf("X-Coord Sprite 3\n");	return 0; //
				case 0xD007: printf("Y-Coord Sprite 3\n");	return 0; //
				case 0xD008: printf("X-Coord Sprite 4\n");	return 0; //
				case 0xD009: printf("Y-Coord Sprite 4\n");	return 0; //
				case 0xD00A: printf("X-Coord Sprite 5\n");	return 0; //
				case 0xD00B: printf("Y-Coord Sprite 5\n");	return 0; //
				case 0xD00C: printf("X-Coord Sprite 6\n");	return 0; //
				case 0xD00D: printf("Y-Coord Sprite 6\n");	return 0; //
				case 0xD00E: printf("X-Coord Sprite 7\n");	return 0; //
				case 0xD00F: printf("Y-Coord Sprite 7\n");	return 0; //
				case 0xD010: printf("MSB:s of X-coords\n");	return 0; //
				case 0xD011: printf("Control register 1 \n");	return 0; //
				case 0xD012: printf("Raster row counter\n");	return 0; //
				case 0xD013: printf("Light pen X\n");		return 0; //
				case 0xD014: printf("Light pen Y\n");   	return 0; //
				case 0xD015: printf("Sprite enabled\n");	return 0; //
				case 0xD016: printf("Control register 2\n");	return 0; //
				case 0xD017: printf("Sprite Y expansion\n");	return 0; //
				case 0xD018: printf("Memory pointers\n");	return 0; //
				case 0xD019: printf("Interrupt register\n");	return 0; //
				case 0xD01A: printf("Interrupt enabled\n");	return 0; //
				case 0xD01B: printf("Sprite data priority\n");	return 0; //
				case 0xD01C: printf("Sprite multicolour\n");	return 0; //
				case 0xD01D: printf("Sprite X expansion\n");	return 0; //
				case 0xD01E: printf("Sprite-sprite collision\n");return 0;//
				case 0xD01F: printf("Sprite-data collision\n");	return 0; //
				case 0xD020: printf("Border color\n");		return 0; //
				case 0xD021: printf("Background color 0\n");	return 0; //
				case 0xD022: printf("Background color 1\n");	return 0; //
				case 0xD023: printf("Background color 2\n");	return 0; //
				case 0xD024: printf("Background color 3\n");	return 0; //
				case 0xD025: printf("Sprite multicolor 0\n");	return 0; //
				case 0xD026: printf("Sprite multicolor 1\n");	return 0; //
				case 0xD027: printf("Sprite 0 color\n");	return 0; //
				case 0xD028: printf("Sprite 1 color\n");	return 0; //
				case 0xD029: printf("Sprite 2 color\n");	return 0; //
				case 0xD02A: printf("Sprite 3 color\n");	return 0; //
				case 0xD02B: printf("Sprite 4 color\n");	return 0; //
				case 0xD02C: printf("Sprite 5 color\n");	return 0; //
				case 0xD02D: printf("Sprite 6 color\n");	return 0; //
				case 0xD02E: printf("Sprite 7 color\n");	return 0; //
				case 0xD02F: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD030: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD031: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD032: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD033: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD034: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD035: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD036: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD037: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD038: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD039: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD03A: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD03B: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD03C: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD03D: printf("Unused, $FF on reading\n"); return 0xFF; //
				case 0xD03E: printf("Unused, $FF on reading\n"); return 0xFF; // 
				case 0xD03F: printf("Unused, $FF on reading\n"); return 0xFF; //
				default: printf("\n"); return 0; //
			}
			return 0;
		}

		// CIA #1 Registers...
		if (address >= 0xDC00 && address <= 0xDCFF){ // mirrored every 16 bytes within its 256-byte block.
			printf("CIA #1 Read  - from 0x%X ",address); fflush(stdout);  // Forces the buffer to flush immediately
			switch (address & 0xFF0F){ // implementation of registers... with mirroring
				case 0xDC00:	printf("Port A data   0x%02X            \n",cia_1_port_a_data); return cia_1_port_a_data;  // Column outputs 
				case 0xDC01:	printf("Port B data   0x%02X            \n",cia_1_port_b_data); return cia_1_port_b_data; // 151 ; // Row inputs
				case 0xDC02:	printf("Port A Direction             \n"); return cia_1_port_a_direction;
				case 0xDC03:	printf("Port B Direction             \n"); return cia_1_port_b_direction;
				case 0xDC04:	printf("TIMER A LOW                  \n"); return 0;
				case 0xDC05:	printf("TIMER A HIGH                 \n"); return 0;
				case 0xDC06:	printf("TIMER B LOW                  \n"); return 0;
				case 0xDC07:	printf("TIMER B HIGH                 \n"); return 0;
				case 0xDC08:	printf("Real Time Clock 1/10s        \n"); return 0; // Real Time Clock 1/10s
				case 0xDC09:	printf("Real Time Clock Seconds      \n"); return 0; // Real Time Clock Seconds
				case 0xDC0A:	printf("Real Time Clock Minutes      \n"); return 0; // Real Time Clock Minutes
				case 0xDC0B:	printf("Real Time Clock Hours        \n"); return 0; // Real Time Clock Hours
				case 0xDC0C:	printf("Serial shift register        \n"); return 0; // Serial shift register - The byte within this register will be shifted bitwise to or from the SP-pin with every positive slope at the CNT-pin. 
				case 0xDC0D:	printf("Interrupt Control and status \n"); return 1; // Interrupt Control and status - CIA1 is connected to the IRQ-Line.
				case 0xDC0E:	printf("Control Timer A (CRA)        \n"); return 0; // Control Timer A Control-Register-A
				case 0xDC0F:	printf("Control Timer B (CRB)        \n"); return 0; // Control Timer B Control-Register-B
				default: printf("no default address");
			}
			return 0;
		}

		// CIA #2 Registers...
		if (address >= 0xDD00 && address <= 0xDDFF){ // mirrored every 16 bytes within its 256-byte block.
			printf("CIA #2 Read  - From 0x%X ",address); fflush(stdout);  // Forces the buffer to flush immediately
			switch (address & 0xFF0F){ // implementation of registers... with mirroring
				case 0xDD00:	printf("Port A data                  \n"); return 0; // Bit 1-0 Selects position of VIC II memory. Register controlles othe stuff also. 
				case 0xDD01:	printf("Port B data                  \n"); return 0;
				case 0xDD02:	printf("Port A Direction             \n"); return 0;
				case 0xDD03:	printf("Port B Direction             \n"); return 0;
				case 0xDD04:	printf("TIMER A LOW                  \n"); return 0;
				case 0xDD05:	printf("TIMER A HIGH                 \n"); return 0;
				case 0xDD06:	printf("TIMER B LOW                  \n"); return 0;
				case 0xDD07:	printf("TIMER B HIGH                 \n"); return 0;
				case 0xDD08:	printf("Real Time Clock 1/10s	  \n"); return 0; // Real Time Clock 1/10s
				case 0xDD09:	printf("Real Time Clock Seconds	  \n"); return 0; // Real Time Clock Seconds
				case 0xDD0A:	printf("Real Time Clock Minutes	  \n"); return 0; // Real Time Clock Minutes
				case 0xDD0B:	printf("Real Time Clock Hours        \n"); return 0; // Real Time Clock Hours
				case 0xDD0C:	printf("Serial shift register        \n"); return 0; // Serial shift register - The byte within this register will be shifted bitwise to or from the SP-pin with every positive slope at the CNT-pin. 
				case 0xDD0D:	printf("Interrupt Control and status \n"); return 1; // Interrupt Control and status - CIA2 is connected to the NMI-Line.
				case 0xDD0E:	printf("Control Timer A (CRA)        \n"); return 0; // Control Timer A Control-Register-A
				case 0xDD0F:	printf("Control Timer B (CRB)        \n"); return 0; // Control Timer B Control-Register-B
				default: printf("no default address");
			}
			return 0;
		}

		// A large catch all for hardware registers!!!! If I have not everything down in the program
		printf("Read from 0x%04X (range 0xD000 - 0xDFFF) ",address); fflush(stdout);  // Forces the buffer to flush immediately
		printf("Unknown known hardware. Fix emulation!!!\n");
		return sysram[address];	// RAM - Some type of failsafe, this row will never run.
}

void write6502(uint16_t address, uint8_t value){

	if ((sysram[1] & 0x03) == 0 || (address & 0xF000) != 0xD000) { // PLA Logic
		sysram[address] = value; // RAM - Catches all RAM writes, not more, not less!
	}else{ // A I/O Write - Put all I/O writes here...
	
		shaddow_io[address - 0xD000] = value ; // Saving writes to I/O in RAM, just like a hacking cartridge do.

		// Write Color RAM -- do not touch sysram
		if (address >= 0xD800 && address <= 0xDBFF) {		// Fix mirroring !!!
		    color_ram[address - 0xD800] = (value & 0x0F);	// store 4-bit color
		    return;  // 
		}

		// SID registers
		else if (address >= 0xD400 && address <= 0xD41C){
			printf("SID Write    - 0x%02X to 0x%04X\n",value,address); return ;
			switch(address){
				// Put SID registers here
			}
		}

		else if (address >= 0xD000 && address <= 0xDFFF){ // Registers

			// VIC-II registers
			if (address >= 0xD000 && address <= 0xD3FF){
				printf("VIC-II Write - 0x%02X to 0x%04X ",value,address); fflush(stdout);  // Forces the buffer to flush immediately
				switch(address){
					case 0xD000: printf("X-coord Sprite 0\n"); return; //
					case 0xD001: printf("Y-coord Sprite 0\n"); return; //
					case 0xD002: printf("X-Coord Sprite 1\n"); return; //
					case 0xD003: printf("Y-Coord Sprite 1\n"); return; //
					case 0xD004: printf("X-Coord Sprite 2\n"); return; //
					case 0xD005: printf("Y-Coord Sprite 2\n"); return; //
					case 0xD006: printf("X-Coord Sprite 3\n"); return; //
					case 0xD007: printf("Y-Coord Sprite 3\n"); return; //
					case 0xD008: printf("X-Coord Sprite 4\n"); return; //
					case 0xD009: printf("Y-Coord Sprite 4\n"); return; //
					case 0xD00A: printf("X-Coord Sprite 5\n"); return; //
					case 0xD00B: printf("Y-Coord Sprite 5\n"); return; //
					case 0xD00C: printf("X-Coord Sprite 6\n"); return; //
					case 0xD00D: printf("Y-Coord Sprite 6\n"); return; //
					case 0xD00E: printf("X-Coord Sprite 7\n"); return; //
					case 0xD00F: printf("Y-Coord Sprite 7\n"); return; //
					case 0xD010: printf("MSB:s of X-coords\n");  return; //
					case 0xD011: printf("Control register 1\n"); return; //
					case 0xD012: printf("Raster row counter\n"); return; //
					case 0xD013: printf("Light pen X\n"); return;  //
					case 0xD014: printf("Light pen Y\n"); return;  //
					case 0xD015: printf("Sprite enabled\n"); return; //
					case 0xD016: printf("Control register 2\n"); return; //
					case 0xD017: printf("Sprite Y expansion\n"); return; //
					case 0xD018: printf("Memory pointers\n"); return; //
					case 0xD019: printf("Interrupt register\n"); return; //
					case 0xD01A: printf("Interrupt enabled\n"); return;  //
					case 0xD01B: printf("Sprite data priority\n"); return; //
					case 0xD01C: printf("Sprite multicolour\n"); return; //
					case 0xD01D: printf("Sprite X expansion\n"); return; //
					case 0xD01E: printf("Sprite-sprite collision\n"); return; //
					case 0xD01F: printf("Sprite-data collision\n"); return; //
					case 0xD020: printf("Border color\n"); return; //
					case 0xD021: printf("Background color 0\n"); 
						ikigui_image_solid(&bg, c64_palette[value & 0xF]); printf("Color change\n");
					return; //
					case 0xD022: printf("Background color 1\n"); return; //
					case 0xD023: printf("Background color 2\n"); return; //
					case 0xD024: printf("Background color 3\n"); return; //
					case 0xD025: printf("Sprite multicolor 0\n"); return; //
					case 0xD026: printf("Sprite multicolor 1\n"); return; //
					case 0xD027: printf("Sprite 0 color\n"); return; //
					case 0xD028: printf("Sprite 1 color\n"); return; //
					case 0xD029: printf("Sprite 2 color\n"); return; //
					case 0xD02A: printf("Sprite 3 color\n"); return; //
					case 0xD02B: printf("Sprite 4 color\n"); return; //
					case 0xD02C: printf("Sprite 5 color\n"); return; //
					case 0xD02D: printf("Sprite 6 color\n"); return; //
					case 0xD02E: printf("Sprite 7 color\n"); return; //
					case 0xD02F: printf("Unused, write access ignored\n"); return; //
					case 0xD030: printf("Unused, write access ignored\n"); return; //
					case 0xD031: printf("Unused, write access ignored\n"); return; //
					case 0xD032: printf("Unused, write access ignored\n"); return; //
					case 0xD033: printf("Unused, write access ignored\n"); return; //
					case 0xD034: printf("Unused, write access ignored\n"); return; //
					case 0xD035: printf("Unused, write access ignored\n"); return; //
					case 0xD036: printf("Unused, write access ignored\n"); return; //
					case 0xD037: printf("Unused, write access ignored\n"); return; //
					case 0xD038: printf("Unused, write access ignored\n"); return; //
					case 0xD039: printf("Unused, write access ignored\n"); return; //
					case 0xD03A: printf("Unused, write access ignored\n"); return; //
					case 0xD03B: printf("Unused, write access ignored\n"); return; //
					case 0xD03C: printf("Unused, write access ignored\n"); return; // 
					case 0xD03D: printf("Unused, write access ignored\n"); return; //
					case 0xD03E: printf("Unused, write access ignored\n"); return; // 
					case 0xD03F: printf("Unused, write access ignored\n"); return; //
					default: printf("Missing case\n"); return ; //
				}
			}

			// CIA Registers...
			if (address >= 0xDC00 && address <= 0xDCFF){ 
				printf("CIA #1 Write - 0x%02X to 0x%04X ",value,address); fflush(stdout);  // Forces the buffer to flush immediately
				switch (address){ // implementation of registers...
					case 0xDC00:	printf("Port A data @ PC = 0x%X\n",getpc());	cia_1_port_a_data = value ; return; // Keyboard columns // return 151; // hämtat från en C64 vad default värdet är från tangentbordet.
					case 0xDC01:	printf("Port B data\n"); 			cia_1_port_b_data = value ; return; // Keyboard rows
					case 0xDC02:	printf("Port A Direction - 1=output, 0=input\n"); cia_1_port_a_direction = value ; return; 
					case 0xDC03:	printf("Port B Direction - 1=output, 0=input\n"); cia_1_port_b_direction = value ; return; 
					case 0xDC04:	printf("Timer A Low             \n"); return; 
					case 0xDC05:	printf("Timer A High            \n"); return; 
					case 0xDC06:	printf("Timer B Low             \n"); return; 
					case 0xDC07:	printf("Timer B High            \n"); return; 
					case 0xDC08:	printf("Real Time Clock 1/10s   \n"); return; // Real Time Clock 1/10s
					case 0xDC09:	printf("Real Time Clock Seconds \n"); return; // Real Time Clock Seconds
					case 0xDC0A:	printf("Real Time Clock Minutes \n"); return; // Real Time Clock Minutes
					case 0xDC0B:	printf("Real Time Clock Hours   \n"); return; // Real Time Clock Hours
					case 0xDC0C:	printf("Serial shift register   \n"); return; // Serial shift register - The byte within this register will be shifted bitwise to or from the SP-pin with every positive slope at the CNT-pin. 
					case 0xDC0D:	printf("Interrupt Control and status \n"); return; // Interrupt Control and status - CIA1 is connected to the IRQ-Line.
					case 0xDC0E:	printf("Control Timer A (CRA)   \n"); return; // Control Timer A Control-Register-A
					case 0xDC0F:	printf("Control Timer B (CRB)   \n"); return; // Control Timer B Control-Register-B
				}
			}

			if (address >= 0xDD00 && address <= 0xDDFF){ 
				printf("CIA #2 Write - 0x%02X to 0x%04X ",value,address); fflush(stdout);  // Forces the buffer to flush immediately
				switch (address){ // implementation of registers...
					case 0xDD00:	printf("Port A data      \n"); return; // Bit 1-0 Selects position of VIC II memory. Register controlles othe stuff also. 
					case 0xDD01:	printf("Port B data      \n"); return;
					case 0xDD02:	printf("Port A Direction - 1=output, 0=input\n"); return;
					case 0xDD03:	printf("Port B Direction - 1=output, 0=input\n"); return;
					case 0xDD04:	printf("TIMER A LOW      \n"); return;
					case 0xDD05:	printf("TIMER A HIGH     \n"); return;
					case 0xDD06:	printf("TIMER B LOW      \n"); return;
					case 0xDD07:	printf("TIMER B HIGH     \n"); return;
					case 0xDD08:	printf("Real Time Clock 1/10s	\n"); return; // Real Time Clock 1/10s
					case 0xDD09:	printf("Real Time Clock Seconds	\n"); return; // Real Time Clock Seconds
					case 0xDD0A:	printf("Real Time Clock Minutes	\n"); return; // Real Time Clock Minutes
					case 0xDD0B:	printf("Real Time Clock Hours	\n"); return; // Real Time Clock Hours
					case 0xDD0C:	printf("Serial shift register	\n"); return; // Serial shift register - The byte within this register will be shifted bitwise to or from the SP-pin with every positive slope at the CNT-pin. 
					case 0xDD0D:	printf("Interrupt Control and status\n"); return; // Interrupt Control and status - CIA2 is connected to the NMI-Line.
					case 0xDD0E:	printf("Control Timer A (CRA) \n"); return;   // Control Timer A Control-Register-A
					case 0xDD0F:	printf("Control Timer B (CRB) \n"); return;   // Control Timer B Control-Register-B
				}
			}

		}
		// A large catch all !!!! That is not needed as it's never triggered
		if (address >= 0xD000 && address <= 0xDFFF){ // Hardware Registers
			printf("0x%X Fix emulator!!! Uknown Hardware Write (range 0xD000 - 0xDFFF) with value 0x%x \n", address, value); return;
		}
		printf("Wow!!! strange!\n"); fflush(stdout); exit(0);
	}
}


/// Draw characters from a C64-style character ROM using color RAM + external palette.
/// - display->map holds the character codes (indexes into the ROM).
/// - color_ram_fg holds raw C64 foreground colors (0–15), one per tile.
/// - color_ram_bg holds raw C64 background colors (0–15), one per tile, or NULL.
/// - palette holds ARGB colors for the 16 C64 color codes.
/// - If color_ram_fg == NULL, falls back to display->source->color.
/// - If color_ram_bg == NULL, background is left transparent.

// this can be valuable for emulating hardware used bu a MVU. As I want to btidge the gap between MCU to CPU
void ikigui_map_draw_charrom( // Some extention that is used over the regular ikiGUI map, but does not use a lot of it, so much redundant code.
    struct ikigui_map *display,
    const uint8_t *char_rom,           ///< C64 character ROM (8 bytes per char)
    const uint8_t *color_ram_fg,       ///< Foreground color RAM (0–15)
    const uint8_t *color_ram_bg,       ///< Background color RAM (0–15), or NULL
    const unsigned int *palette,       ///< External ARGB palette[16]
    int x, int y                       ///< Pixel offset
) {
	// Safety check: make sure display, map, and char ROM exist
	if (!display || !display->map || !char_rom) return;

	int tile_w = display->tile_width;    // Tile width in pixels (usually 8)
	int tile_h = display->tile_height;   // Tile height in pixels (usually 8)
	int total_cells = display->rows * display->columns; // Total cells in the display

	// Iterate over each row of the character map
	for (int row = 0; row < display->rows; row++) {
		// Iterate over each column of the character map
		for (int col = 0; col < display->columns; col++) {
			int idx = row * display->columns + col;  // Linear index in map array
			if (idx >= total_cells) continue;        // Safety: skip if out of bounds

			// Get the character code from the map, apply offset (useful for ASCII)
			int char_code = (unsigned char)display->map[idx] + display->offset;

			// Determine foreground color for this character
			// If color RAM and palette are provided, use the color from color RAM
			unsigned int fg = 0;
			if (color_ram_fg && palette) fg = palette[color_ram_fg[idx] & 0x0F]; // Lookup in palette (0–15)
			else fg = display->source->color;           // Otherwise use the default tile color

			// Determine background color (optional)
			unsigned int bg = 0;
			int use_bg = (color_ram_bg && palette); // Only use background if provided
			if (use_bg) bg = palette[color_ram_bg[idx] & 0x0F]; // Lookup background color

			// Compute pixel position of this cell in the destination image
			int dst_x = x + col * display->x_spacing;
			int dst_y = y + row * display->y_spacing;

			// Pointer to the character glyph in the ROM (tile_h bytes per character)
			const uint8_t *glyph = &char_rom[char_code * tile_h];

			// Iterate over each pixel row of the glyph
			for (int yy = 0; yy < tile_h; yy++) {
				uint8_t bits = glyph[yy];  // Bits of this glyph row
				// Iterate over each pixel column
				for (int xx = 0; xx < tile_w; xx++) {
					int px = dst_x + xx;   // Destination pixel X
					int py = dst_y + yy;   // Destination pixel Y
					unsigned int *dst_pixel = &display->dest->pixels[px + py * display->dest->w];

					if (bits & (1 << (7 - xx))) {
						// Bit is set → foreground pixel
						*dst_pixel = alpha_channel(*dst_pixel, fg);
					} else if (use_bg) {
						// Bit is clear → use background color if available
						*dst_pixel = alpha_channel(*dst_pixel, bg);
					}
					// Else: leave destination pixel unchanged (transparent)
				}
			}
		}
	}
}

int main() {
	ikigui_image_make(&bg, WIN_WIDTH,WIN_HEIGHT);				// Create a background image
	ikigui_image_gradient(&bg,0xffccdd22, 0xffc0d020);			// Fill background image with a gradient
	ikigui_window_open(&mywin, "C64 BASIC EMULATOR", WIN_WIDTH, WIN_HEIGHT);// Open a window for the emulators graphics frame buffer, and real time emulator status like a overlay over the graphics.
	ikigui_map_init(&font_map,&mywin.image,&font ,0,0,0,8,8,40,25);		// VIC-64 Character display, with 40 columns and 25 lines.
	font.color = 0x114433 ;
	font_map.map = &sysram[VIDEOADDR];	// We switch out tha allocated char buffer given by my lib.
	sysram[1] = 7; 				// PLA start setting. The reset vector is in KERNAL ROM so it has to be availible on reset. Made by resistors in the c64? before setting the 6510 GPIO port pins to outputs for the PLA.
	reset6502();				// Reset the CPU
	
	while(1){
		char blink, visible, idle; // Custom stuff for the fake cursor that is needed as we do not emulate any CIA chips.

		if(!idle){ // Do nothing if it's waiting for a character input.
			for(int i = 0 ; i < (1024*6) ; i++){ // instructions per frame, aproximatly the same speed in BASIC as a real C64
				exec6502(); 
				if(getpc() == 0xE5CD){ idle = 1; printf("Pause\n");break; }   // VIC-64 - Start of the main blocking loop in C64 looking for a key press.
			}
		}
			
		if (mywin.key > 0){ // Wait for keybord input
			visible = ~0 ; // Reset blink cycle at a keypress...
			blink = 0;     // ...so it's visible if we are writing fast on the keyboard.

			unsigned char tecken = mywin.text[0] ;
			if(tecken == 8)  tecken =  20 ; // Check if backspace. If so adapt it to PETSCII 
			if(tecken == 24) tecken = 157 ; // Left
			if(tecken == 25) tecken = 29  ; // Right
			if(tecken == 26) tecken = 145 ; // Up
			if(tecken == 27) tecken = 17  ; // Down
			if(tecken == 9){ 		// Tab, simulates the STOP key, to break out of the running BASIC program.
				push16(pc);	// Simulate a exception.
				setcarry();	// Break
				setzero();	// Simulate CTRL-C
				pc = 0xA832;	// Jump to BASIC Stop routine
			}
			mywin.key = 0; // Unstick keypress.
			
			sysram[0x0277 + sysram[0xF7]] = tecken ;	// Put key in buffer
			sysram[0xF8] = sysram[0xF8] + 1;		// Increment buffer pointer
			sysram[0xC6] = 1;				// Set flag indicating key was pressed (similar to C64's $C6)

			idle = 0; // BASIC was waiting for a keypress. Run the 6502 emulation again.
			printf("continue\n");
		}
		
		// For the fake BASIC cursor... We simulate the cursor as we have no interrupt to blink it.
		ikigui_rect rect ;
		rect.x = sysram[0xD3] * 8 ; // cursor at column?
		rect.y = sysram[0xD6] * 8 ; // cursor at row ?
		rect.w = 8 ; // cursor width
		rect.h = 8 ; // cursor hight
		if(idle){    // Blink cursor if BASIC isn't calculating
			blink++ ;
			if(blink == 7){ visible = ~visible ; blink = 0;} 
			if(visible)ikigui_draw_box_simple(&mywin.image, c64_palette[sysram[0x0286]],  &rect ); // Draw a cursor	with the current BASIC text color (found in address 0x286).	
		} 
		
		ikigui_window_till(&mywin,33); // Update screen and wait 33ms (aproximatley 30 frames per second).
		ikigui_draw_image(&mywin.image,&bg, 0, 0); // Draw background. Was originally like a backlit LCD, but now I have a char-color map as on the C64 when used in BASIC.
		ikigui_map_draw_charrom(&font_map, characters, (uint8_t *)&color_ram,NULL, c64_palette, 0, 0);
		// ikigui_map_draw_charrom(&font_map, c64_swedish2, (uint8_t *)&color_ram,NULL, c64_palette, 0, 0);
		// Draw sprites here - Do we need them? 
	}
}
