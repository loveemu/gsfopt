/////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "VBA/psftag.h"

#define APP_NAME    "gsfopt"
#define APP_VER     "[2014-01-19]"
#define APP_URL     "http://github.com/loveemu/gsfopt"

#define FALSE 0
#define TRUE 1

int emulating = FALSE;
int file_length; // file length, in bytes
unsigned long GSF_entrypoint = 0;
unsigned long GSF_offset = 0;
unsigned long GSF_rom_size = 0;
double optimizetime=300000; // 5 Minutes
int paranoidbytes=0;
int fixedtime=0;

double decode_pos_ms=0;
int Optt=0;
int OptT=0; //IF the T option is used, override the default always.

unsigned char optData[0x2000000];
unsigned long optCount;
unsigned long prevCount=0;

int number_loops=2;
int verify_loops=20;
int OptTAG=0;
double BPplaytime=0;
double BPoneshot=0;
int LoopFade=10000;
int OneShotFade=1000;
int OneShotSong=0;
int silencedetected=0;
int silencelength=15;


extern unsigned long GSF_rom_size;

char strdata[256];
char *filename;

HANDLE input_file=INVALID_HANDLE_VALUE; // input file handle

DWORD WINAPI __stdcall DecodeThread(void *b); // the decode thread procedure

extern int GSFRun(char *filename);
extern void GSFClose(void);

void SetGSFTag(char *filename, char *tag, char *value);
void GetGSFTag(char *filename, char *tag, char *value, int valuesize);

void OptimizeGSF(unsigned long offset, int j, int l);
extern void SaveOptimizedGSF(char *fn, int saveasrom);

unsigned char bSequentialOpt = FALSE;
//unsigned char bFileOpt;

unsigned char hexdigit(unsigned char digit)
{
	if(digit>='0'&&digit<='9')
		return digit-'0';
	if(digit>='A'&&digit<='F')
		return digit-'A'+10;
    if(digit>='a'&&digit<='f')
		return digit-'a'+10;
	return 0;
}

unsigned long ahextoi(const char *buffer)
{
	char *buf = (char*) buffer;
	int i=0;
	unsigned long integer=0;
	while(*buf)
	{
		integer<<=4;
		integer+=hexdigit(*buf);
		buf++;
		
	}

	return integer;
}

unsigned char IsGSFLIB;
int LengthFromString(const char * timestring);

int usage(const char * progname, int extended)
{
	printf("\n");
	printf("%s %s\n", APP_NAME, APP_VER);
	printf("<%s>\n", APP_URL);
	printf("\n");
	printf("usage: %s [options] [-s or -l or -f or -t] [gsf files]\n",progname);
	if(!extended)
		printf("\nfor detailed usage info, type %s -?\n",progname);
	printf("\n");
	if(extended)
	{
		printf("[options]\n\n");
		printf("-T [time]  - Runs the emulation till no new data has been found\n");
		printf("             for [time] specified.\n");
		printf("             Time is specified in mm:ss.nnn format\n");
		printf("             mm = minutes, ss = seoconds, nnn = milliseconds\n\n");

		printf("-P [bytes] - I am paranoid, and wish to assume that any data \n");
		printf("             within [bytes] bytes of a used byte, is also used\n");
		printf("\n\n");
		printf("[file processing modes (-s) (-l) (-f) (-t)\n\n");
		printf("-f [gsf files] - Optimize single files, and in the process, convert\n");
		printf("                 minigsfs/gsflibs to single gsf files\n\n");
		printf("-l [gsf files] - Optimize the gsflib using passed gsf files.\n\n");
		printf("-r [gsf files] - Convert to Rom files, no optimization\n");
		printf("-s [gsflib] [Hex offset] [Count] - Optimize gsflib using a\n");
		printf("                                   known offset/count\n\n");
		printf("-t [options] [gsf files]\n");
		printf("                 Times the GSF files. (for auto tagging, use the -T option)\n");
		printf("                 Unlike psf playback, silence detection is MANDATORY\n");
		printf("                 Do NOT try to evade this with an excessively long silence detect\n");
		printf("                 time.  (The max time is less than 2*Verify loops for silence detection)\n");
		printf("[options] for -t\n");
		printf("-V [count]       Number of verify loops at end point. (Default 20)\n");
		printf("-L [count]       Number of loops to time for. (Default 2, max 255)\n");
		printf("-T               Tag the songs with found time.\n");
		printf("                 A Fade is also added if the song is not detected\n");
		printf("                 to be one shot.\n");
		printf("-F [time]        Length of looping song fade. (default 10.000)\n");
		printf("-f [time]        Length of one shot song fade. (default 1.000)\n");
		printf("-s [time]        Time in seconds for silence detection (default 15 seconds)\n");
		printf("                 Max (2*Verify loop count) seconds.\n");

		printf("\n\n");
	}
	return 1;

}

int main(int argc, char *argv[]) {
	int i, j;

	int filestart, fileend;

	unsigned long offset;
	unsigned short count;

	int OptF=0, OptR=0, OptS=0, OptL=0;

	if(argc<3)
	{
		if(argc<2)
		{
			usage(argv[0],0);
		}
		else if ((argv[1][0]=='-')&&(argv[1][1]=='?'))
			usage(argv[0],1);
		else
			usage(argv[0],0);
		return -1;
	}

/*	printf("%d parameters\n",argc);
	for(i=0;i<argc;i++)
		printf("parameter %d = %s\n",i,argv[i]);*/


	memset(optData, 0, sizeof(optData));

	for(i=1;i<argc;i++)
	{
		switch (argv[i][1])
		{
		case 'f':  //regular .gsf optimization. this option doubles as minigsf->gsf converter
				OptF=1;
				filestart = i + 1;
				if(argc<=(i+1))
					return usage(argv[0],0);
				break;
		case 'r':
			OptR=1;
			filestart = i + 1;
			if(argc<=(i+1))
				return usage(argv[0],0);
			break;
		case 's':  //Song value gsflib optimization.
			OptS=1;
			filestart = i + 1;
			if(argc<=(i+3))
					return usage(argv[0],0);
			break;
		case 'l':  //gsflib optimization.
			OptL=1;
			filestart = i + 1;
			if(argc<=(i+1))
					return usage(argv[0],0);
			break;
		case 't':
			Optt=1;
			//filestart = i + 1;
			if(argc<=(i+1))
				return usage(argv[0],0);
			break;
		case 'T': // Optimize while no new data found for.
			OptT = 1;
			if(argc<=(i+1))
				return usage(argv[0],0);
			optimizetime=LengthFromString(argv[i+1]);
			i++;
			break;
		case 'P': // I am paranoid. assume x bytes within a used byte is also used.
			if(argc<=(i+1))
				return usage(argv[0],0);
			paranoidbytes=atoi(argv[i+1]);
			i++;
			break;
		default:
			break;
		}
		if (OptR||OptS||OptF||OptL)
		{
			break;
		}
		if(Optt)
		{
			for(i++;i<argc;i++)
			{
				if(argv[i][0]!='-')
					break;
				switch(argv[i][1])
				{
				case 'V':
					if(argc<=(i+1))
						return usage(argv[0],0);
					verify_loops = atoi(argv[i+1]);
					i++;
					break;
				case 'L':
					if(argc<=(i+1))
						return usage(argv[0],0);
					number_loops = atoi(argv[i+1]);
					if(number_loops>255)
					{
						printf("Max Loop count is 255\n");
						return usage(argv[0],0);
					}
					i++;
					break;
				case 'T':
					OptTAG=1;
					break;
				case 'F':
					if(argc<=(i+1))
						return usage(argv[0],0);
					LoopFade = LengthFromString(argv[i+1]);
					i++;
					break;
				case 'f':
					if(argc<=(i+1))
						return usage(argv[0],0);
					OneShotFade = LengthFromString(argv[i+1]);
					i++;
					break;
				case 's':
					if(argc<=(i+1))
						return usage(argv[0],0);
					silencelength = atoi(argv[i+1]);
					i++;
					break;
				default:
					break;
				}
			}
			if(silencelength > (verify_loops * 2))
			{
				silencelength = verify_loops * 2;
				printf("WARNING: Max silence length is %d\n",silencelength);
			}
			filestart = i;
			break;
		}
	}
	if((OptR+OptS+OptF+OptL+Optt)==0)
	{
		printf("You need to specify a processing mode, -f, -s, -l");
		return 1;
	}


	if (OptS)		//if the user requests song value gsflib optimization
	{
		bSequentialOpt = TRUE;
		IsGSFLIB = TRUE;
		count = atoi(argv[filestart+2]);
		offset = ahextoi(argv[filestart+1]);

		if (!GSFRun(argv[filestart])) { printf("\nExiting gsfopt."); return; }
		for (j=0; j<count; j++) {
			printf("\nOptimizing %s  Song value %X", argv[filestart], j);
			filename = argv[filestart];
			OptimizeGSF(offset,j,count > 255 ? 2 : 1);
		}
		SaveOptimizedGSF(argv[filestart],0);
		GSFClose();							//shuts down emulation
	}
	else if (OptL)	//if the user requests gsflib optimization
	{
		IsGSFLIB = TRUE;
		for (i = filestart; i < argc; i++)			//for all the files passed
		{
			if (!GSFRun(argv[i])) {	printf("\nExiting gsfopt."); return; } //opens the file and loads it into ram
			printf("\nOptimizing %s", argv[i]);
			filename = argv[i];
			OptimizeGSF(0,0,0);				//0,0 cause offset and count values unused					
		}
		SaveOptimizedGSF(argv[filestart],0);
		GSFClose();							//shuts down emulation
	}
	else if (OptF)	//if the first argument is neither flag, then we assume regular .gsf optimization. this option doubles as minigsf->gsf converter
	{
		IsGSFLIB = FALSE;
		for (i = filestart; i < argc; i++)			//for all the files passed
		{
			memset(optData, 0, sizeof(optData));
			if (!GSFRun(argv[i])) {	printf("\nSkipping"); continue; } //opens the file and loads it into ram
			printf("\nOptimizing %s", argv[i]);
			filename = argv[i];
			OptimizeGSF(0,0,0);				//0,0 cause offset and count values unused
			SaveOptimizedGSF(argv[i],0);
		}
		GSFClose();					//shuts down emulation
	}
	else if (OptR)
	{
		IsGSFLIB = FALSE;
		for (i = filestart; i < argc; i++)
		{
			
			if (!GSFRun(argv[i])) {	printf("\nSkipping"); continue; } //opens the file and loads it into ram
			memset(optData, 1, sizeof(optData));
			SaveOptimizedGSF(argv[i],1);
		}
		GSFClose();

	}
	else if (Optt) //Get Timer estimates for GSFs. (Accurate 99% of the time.)
	{
		if(!OptT) optimizetime=(300000*number_loops);
		for (i = filestart; i < argc; i++)
		{
			int minute;
			int second;
			int msecond;
			if(!GSFRun(argv[i])) { printf(" Skipping"); continue; }
			//printf("%s ",argv[i]);
			filename = argv[i];
			OptimizeGSF(0,0,0);
			decode_pos_ms -= optimizetime;
			minute = (int)(decode_pos_ms/1000/60);
			second = (int)(decode_pos_ms/1000);
			msecond = (int)(decode_pos_ms);
			second%=60;
			msecond%=1000;
			
			if(OptTAG)
			{
				if(!OneShotSong)
					sprintf(strdata,"%d.%.3d",(LoopFade/1000),(LoopFade%1000));
				else
					sprintf(strdata,"%d.%.3d",(OneShotFade/1000),(OneShotFade%1000));
				SetGSFTag(filename,"fade",strdata);
				sprintf(strdata,"%d:%.2d.%.3d",minute,second,msecond);
				SetGSFTag(filename,"length",strdata);
			}
			printf("Time = %d:%.2d.%.3d ",minute,second,msecond);
			if(!OneShotSong)
				printf("\n");
			else
				printf("- One SHOT\n");
		}
		GSFClose();
	}
	return 0;
}

extern BOOL EmulationLoop();
extern BOOL EmulationReset();
extern void WriteRom(unsigned long offset, unsigned short count, unsigned char size);

/*=========== LOOP Detection Code ============*/
unsigned long CountBytes(int loop, int recount)
{
	int i;
	static int j[256];

	if(recount)
	{
		for(i=0;i<256;i++)
			j[i]=0;
		for(i=0;i<GSF_rom_size;i++)
			j[optData[i]]++;
	}
	return j[loop];
	
}
/*=========== End of LOOP Detection Code ============*/

void OptimizeGSF(unsigned long offset, int j, int l)
{
	

	int i, k=0;

	//Automated Two loop counter variables.
	int m,n=0;
	int loopcount=0;
	int bytecountequal=0;
	int loop_count[512];
	
	int minute;
	int second;
	int msecond;
	int keystate=0;
	int prevkeystate=0;
	

	double playtime;

	BPplaytime=0;
	BPoneshot=0;
	silencedetected=0;

	decode_pos_ms=0;
	optCount=0;
    prevCount=0;

	for(m=0;m<512;m++)
		loop_count[m]=0;

	EmulationReset();
	
	if(bSequentialOpt)
		WriteRom(offset,j,l);

	playtime = decode_pos_ms + optimizetime;
	
	if(!Optt)
		printf("\n");
	//for (i=0; i<10000; i++)
	while(decode_pos_ms < playtime)
	{
		k++;
		if (k%100 == 0)
		{
			double playtime2 = playtime - optimizetime;
			if(Optt)
				playtime2 = BPplaytime;
			minute=(int)(playtime2/1000/60);
			second=(int)((playtime2/1000));
			msecond=(int)(playtime2);
			second%=60;
			msecond%=1000;
			if(!Optt)
				printf("Playtime = %.2d:%.2d.%.3d, ", minute,second,msecond);
			else
			{
				for(m=0;m<35;m++)
					if(filename[m])
						printf("%c",filename[m]);
					else
						break;
				printf(": Time = %.2d:%.2d.%.3d, ", minute,second,msecond);
			}
				
			playtime2 = playtime - decode_pos_ms;
			minute=(int)(playtime2/1000/60);
			second=(int)((playtime2/1000));
			msecond=(int)(playtime2);
			second%=60;
			msecond%=1000;
			
			if(!Optt)
			{
				printf("Time Remaining = %.2d:%.2d.%.3d, ", minute,second,msecond);
				printf("Optimize bytes = %d", optCount);
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			}
			else
			{
				/*=========== LOOP Detection Code ============*/
				n=10;
				if(((n%10)==0)&&(bytecountequal<verify_loops))
				{
						CountBytes(0,1);
				}
				for(m=0,loopcount=0;m<(number_loops+1);m++)
				{
					loop_count[m*2]=CountBytes(m,0);
					if((n%10)==0)
					{
						if(loop_count[(m*2)+1]==loop_count[m*2])
							loopcount++;
						loop_count[(m*2)+1]=loop_count[m*2];
					}	
				}
				if(loopcount==(number_loops+1))
				{
					//if(bytecountequal==0)
					//	BPplaytime = decode_pos_ms;
					bytecountequal++;
				}
				else if (loopcount < (number_loops+1))
				{
					bytecountequal=0;
					BPplaytime = decode_pos_ms;
				}
				if((bytecountequal==verify_loops)&&(loopcount==(number_loops+1)))
				{
					//printf("Loop tripped\n");
					playtime = BPplaytime + optimizetime;
					decode_pos_ms = playtime;
					
					for(m=0;m<82;m++)
						printf("\b");
					//bytecountequal=1;
					OneShotSong = 0;
					break;
				}
				if(silencedetected>0&&((decode_pos_ms-BPoneshot)>(silencelength*1000)))
				{
					playtime = BPoneshot + optimizetime;
					decode_pos_ms = playtime;
					
					for(m=0;m<82;m++)
						printf("\b");

					OneShotSong = 1;
					break;
				}
				/*=========== End of LOOP Detection Code ============*/
				sprintf(strdata,"%.2d",verify_loops-bytecountequal);
				printf("%s",strdata);
				for(m=0;m<82;m++)
					printf("\b");
			}
		//	fflush(stdout);
		}
	/*		if(k%2000 == 0)
				printf("|");
			else
				printf(".");*/
		EmulationLoop();
		if(prevCount!=optCount)
		{
			i=0;
			prevCount = optCount;
			playtime = decode_pos_ms + optimizetime;
		}
		//if(i%100==99)
		//	printf("\nEmulation loop = %d, optCount = %d",i,optCount);
	}
	if(!Optt)
	{
		playtime -= optimizetime;
		minute=(int)(playtime/1000/60);
		second=(int)((playtime/1000));
		msecond=(int)(playtime);
		second%=60;
		msecond%=1000;
		printf("Playtime = %.2d:%.2d.%.3d, ", minute,second,msecond);
		printf("Optimize bytes = %d", optCount);
		printf("                              ");
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	}
	else
	{
		for(m=0;m<79;m++)
			printf(" ");
		for(m=0;m<82;m++)
			printf("\b");
		printf("%s: ",filename);
	}

	return;
}

void GetGSFTag(char *filename, char *tag, char *value, int valuesize)
{
	char GSFTAG[50000];
	unsigned long filesize;
    unsigned long header;
    unsigned long reserved;
    unsigned long program;
    unsigned long ccrc;
    unsigned long decompsize=12;
	FILE *f;
	
	f=fopen(filename,"rb");

	if(f==NULL)
		return;

	fseek(f,0,SEEK_END); filesize=ftell(f); fseek(f,0,SEEK_SET);
	
	  if((filesize<0x10)||(filesize>0x4000000))
	  {
		  fclose(f);
		  return;
	  }
	  fread(&header,1,4,f);
	  
	  if(header!=0x22465350)
	  {
		  fclose(f);
		  return;
	  }
	  fread(&reserved,1,4,f);
	  fread(&program,1,4,f);
	  fread(&ccrc,1,4,f);
	  
	  if((reserved+program+16)>filesize)
	  {
		  fclose(f);
		  return;
	  }

	  fseek(f,(reserved+program+16),SEEK_SET);

	  fread(GSFTAG,1,5,f);
	  if(!stricmp(GSFTAG,"[TAG]"))
	  {
	    fread(GSFTAG,1,50000,f);
	  }

	  psftag_raw_getvar(GSFTAG,tag,value,valuesize);


	  fclose(f);
}

void SetGSFTag(char *filename, char *tag, char *value)
{
	char GSFTAG[50000];
	int tagsize=0;
	unsigned long filesize;
    unsigned long header;
    unsigned long reserved;
    unsigned long program;
    unsigned long ccrc;
    unsigned long decompsize=12;
	FILE *f;
	
	f=fopen(filename,"rb+");

	if(f==NULL)
		return;

	fseek(f,0,SEEK_END); filesize=ftell(f); fseek(f,0,SEEK_SET);
	
	  if((filesize<0x10)||(filesize>0x4000000))
	  {
		  fclose(f);
		  return;
	  }
	  fread(&header,1,4,f);
	  
	  if(header!=0x22465350)
	  {
		  fclose(f);
		  return;
	  }
	  fread(&reserved,1,4,f);
	  fread(&program,1,4,f);
	  fread(&ccrc,1,4,f);
	  
	  if((reserved+program+16)>filesize)
	  {
		  fclose(f);
		  return;
	  }

	  fseek(f,(reserved+program+16),SEEK_SET);

	  fread(GSFTAG,1,5,f);
	  GSFTAG[5]=0;
	  if(!stricmp(GSFTAG,"[TAG]"))
	  {
	    tagsize=fread(GSFTAG,1,50000,f);
	  }
	  else
	  {
		  fseek(f,(reserved+program+16),SEEK_SET);
		  fwrite("[TAG]",1,5,f);
		  GSFTAG[0] = 0;
	  }
	  //while((GSFTAG[tagsize]!=0)&&(tagsize<50000))
	  //	  tagsize++;
	  GSFTAG[tagsize]=0;

	  psftag_raw_setvar(GSFTAG,50000,tag,value);

	  tagsize=0;
	  while((GSFTAG[tagsize]!=0)&&(tagsize<50000))
		  tagsize++;

	  fseek(f,(reserved+program+16)+5,SEEK_SET);

	  fwrite(GSFTAG,1,tagsize,f);


	  fclose(f);
}
