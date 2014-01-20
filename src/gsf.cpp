#include "./VBA/System.h"
#include "./VBA/Sound.h"
#include "./VBA/Util.h"
#include "./VBA/GBA.h"


#include "./VBA/unzip.h"

#include <string.h>

#include <windows.h>
#include <stdio.h>
#include "zlib/zlib.h"

int emulating = 0;



struct EmulatedSystem emulator;

bool debugger = false;
bool systemSoundOn = false;

#ifdef MMX
extern "C" bool cpu_mmx;
#endif

int	soundInitialized = false;

extern "C" unsigned char optData[0x2000000] = { 0 };		//stores data for which bytes are used.  each byte is a true/false value for used/unused
extern "C" unsigned char IsGSFLIB;
extern "C" int paranoidbytes;
extern "C" int number_loops;

unsigned char LogoData[156] =
{
    0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD, 
    0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21, 0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20, 
    0x10, 0x46, 0x4A, 0x4A, 0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF, 
    0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0, 0x13, 0x72, 0xA7, 0xFC, 
    0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61, 0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 
    0x23, 0x1D, 0xC7, 0x61, 0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD, 
    0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85, 0x60, 0xD6, 0x80, 0x25, 
    0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2, 0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 
    0x78, 0x00, 0x90, 0xCB, 0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF, 
    0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07, 
} ;

extern "C"
{

int GSFRun(char *fn)
{
  char tempName[2048];
  char filename[2048];

  if(rom != NULL) {
    CPUCleanUp();
    emulating = false;   
  }

  utilGetBaseName(fn, tempName);
  _fullpath(filename, tempName, 1024);
  
  IMAGE_TYPE type = utilFindType(filename);

  if(type == -2)
	  return false;
  else if (type == IMAGE_UNKNOWN) {
    printf("\nUnsupported file type: %s", filename);
    return false;
  }

 {
	int size = CPULoadRom(filename);

    if(!size)
	{
		printf("error loading rom");
        return false;
	}

    char *p = strrchr(tempName, '\\');
    if(p)
      *p = 0;
	
	{
	    char buffer[5];
	    strncpy(buffer, (const char *)&rom[0xac], 4);
	    buffer[4] = 0;

	    strcat(tempName, "\\vba-over.ini");
	}
  }
    
  if(soundInitialized) {
      soundReset();
  } else {
    if(!soundOffFlag)
      soundInit();
	soundInitialized = true;
  }

  if(type == IMAGE_GBA) {
    //skipBios = theApp.skipBiosFile ? true : false;
    //CPUInit((char *)(LPCTSTR)theApp.biosFileName, theApp.useBiosFile ? true : false);
	CPUInit((char *)NULL, false);				//don't use bios file
    CPUReset();
  }
 
  emulating = true;
  
  return true;
}


void GSFClose(void) 
{
  if(rom != NULL) {
    soundPause();
    CPUCleanUp();
  }
  emulating = 0;
}


#define EMU_COUNT 250000

void WriteRom(unsigned long offset, unsigned short count, unsigned char size)
{
	
	rom[offset&0x1FFFFFF] = count & 0xFF;
	if(size==2)
		rom[(offset&0x1FFFFFF)+1] = (count >> 8) & 0xFF;
}

BOOL EmulationLoop(void) 
{
  if(emulating) {
    for(int i = 0; i < 2; i++) {
		CPULoop(EMU_COUNT);
 
	    return TRUE;
	}
  }
  return FALSE;

}

BOOL EmulationReset(void)
{
	if(emulating)
	{
		emulating = false;
		CPUReset();
		emulating = true;

		return TRUE;
	}
	return FALSE;
}


BOOL IsValidGSF ( BYTE Test[4] ) {
	if ( *((DWORD *)&Test[0]) == 0x22465350 ) { return TRUE; }
	return FALSE;
}

BOOL IsTagPresent ( BYTE Test[5] ) {
	if ( *((DWORD *)&Test[0]) == 0x4741545b && Test[4]==0x5D) {return TRUE;}
	return FALSE;
}

extern unsigned char optData[0x2000000];
extern unsigned long optCount;
extern int number_loops;
extern unsigned long GSF_entrypoint;
extern unsigned long GSF_offset;
extern unsigned long GSF_rom_size;

void SaveOptimizedGSF(char *fn, int saveasrom=0)
{
	int i, j;
	FILE *f;
    uLong cl;
	unsigned long ccrc;
	int r;

	unsigned char *uncompbuf;
	unsigned char *compbuf;
	char out_fn[MAX_PATH];

	uncompbuf = (unsigned char *)malloc(GSF_rom_size+12);
	compbuf = (unsigned char *)malloc(GSF_rom_size+12);		//the compressed buffer won't be any greater than this size (in all practical circumstances)

	memset(uncompbuf, 0, sizeof(uncompbuf));

	for(i=GSF_rom_size-1; i>=0; i--)
	{
		if(optData[i] != 0)
		{
			for(j=i+1;(j<(int)GSF_rom_size)&&((j-i)<paranoidbytes)&&(optData[j]==0);j++)
				optData[j] = 1;
		}
	}
	for(i=0;i<0xC0;i++)
	{
		optData[i] = 1;	//Preserve the Nintendo Header Area.
	}
    
	for (i=0; i<(int)GSF_rom_size; i++)
	{
		if ((optData[i] != 0)&&!cpuIsMultiBoot)		//then the byte was used
			*(uncompbuf+i+12) = rom[i];		//+12 because of first three info words
		else if ((optData[i] != 0)&&cpuIsMultiBoot)
			*(uncompbuf+i+12) = workRAM[i];
	}

	memcpy(uncompbuf, &GSF_entrypoint, sizeof(GSF_entrypoint));
    memcpy(uncompbuf+4, &GSF_offset, sizeof(GSF_offset));
    memcpy(uncompbuf+8, &GSF_rom_size, sizeof(GSF_rom_size));

	//now the uncompressed buffer stores the uncompressed optimized rom image

	cl = GSF_rom_size+12;
	r=compress2(compbuf,&cl,uncompbuf,GSF_rom_size+12,9);
	if(r!=Z_OK){fprintf(stderr,"zlib compress2() failed (%d)\n", r);return;}

	i = (int)(strchr(fn, '.') - fn); 
	strncpy(out_fn, fn, i);
	if(IsGSFLIB)
		strcpy(out_fn+i, ".gsflib");
	else if(saveasrom)
		strcpy(out_fn+i, ".gba");
	else
		strcpy(out_fn+i, ".gsf");

	f=fopen(out_fn,"wb");if(!f){perror(out_fn);return;}
	if(!saveasrom)
	{
		fputc('P',f);fputc('S',f);fputc('F',f);fputc(0x22,f);
		fputc(0,f);fputc(0,f);fputc(0,f);fputc(0,f);
		fputc(cl  >> 0,f);
		fputc(cl  >> 8,f);
		fputc(cl  >>16,f);
		fputc(cl  >>24,f);
		ccrc=crc32(crc32(0L, Z_NULL, 0), compbuf, cl);
		fputc(ccrc>> 0,f);
		fputc(ccrc>> 8,f);
		fputc(ccrc>>16,f);
		fputc(ccrc>>24,f);
		fwrite(compbuf,1,cl,f);
	}
	else
	{
		memcpy(uncompbuf+12+4,LogoData,sizeof(LogoData)); // Set the Nintendo Logo Data
		unsigned char checksum=0x19;
		for(i=0xA0;i<=0xBD;i++)
		{
			checksum += *(uncompbuf+12+i);
		}
		if((checksum!=0)||(*(uncompbuf+12+0xB2)!=0x96))
		{
			memset(uncompbuf+12+0xA0,0x20,12);  //Name of rom is unknown
			memset(uncompbuf+12+0xAC,0x41,4);	//Rom Code is unknown
			memset(uncompbuf+12+0xB0,0x30,1);
			memset(uncompbuf+12+0xB1,0x31,1);	//Set ID to Nintendo (Why the Hell Not?)
			memset(uncompbuf+12+0xB2,0x96,1);	//Fixed byte is 0x96
			memset(uncompbuf+12+0xB3,0x00,1);	//Main Unit code should be 0 for GBA
			memset(uncompbuf+12+0xB4,0x00,1);	//Should be 0, as no debugging DACS is present
			memset(uncompbuf+12+0xB5,0x00,7);	//Reserved Area, must be 0
			memset(uncompbuf+12+0xBC,0x00,1);	//Software revision number, unknown, assuming 1.0
			memset(uncompbuf+12+0xBE,0x00,2);	//Reserved, must be 0

			checksum=0x19;
			for(i=0xA0;i<=0xBC;i++)
			{
				checksum += *(uncompbuf+12+i);
			}
			checksum = 0x100 - checksum;

			memset(uncompbuf+12+0xBD,checksum,1);	//Calculate the correct checksum
		}
		
		i = GSF_rom_size - 1;

		while((*(uncompbuf+12+i)==0))
		{
			i--;
		}
		GSF_rom_size=i+1;

		fwrite(uncompbuf+12,1,GSF_rom_size,f);
	}
    fclose(f);
    
	free(uncompbuf);
	free(compbuf);
	printf("\nSaved %s\n", out_fn);
}




unsigned long endianflip(unsigned long value)
{
	return ((value & 0xFF) << 24) + ((value & 0xFF00) << 8) + ((value & 0xFF0000) >> 8) + ((value & 0xFF000000) >> 24);
}


}