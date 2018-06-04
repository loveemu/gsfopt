
#define NOMINMAX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <string>
#include <map>
#include <vector>
#include <iterator>
#include <limits>
#include <algorithm>

#include "gsfopt.h"
#include "cpath.h"
#include "ctimer.h"
#include "PSFFile.h"

#ifdef WIN32
#include <direct.h>
#include <float.h>
#define getcwd _getcwd
#define chdir _chdir
#define isnan _isnan
#define strcasecmp _stricmp
#else
#include <unistd.h>
#endif

#define APP_NAME    "gsfopt"
#define APP_VER     "[2018-06-04]"
#define APP_URL     "http://github.com/loveemu/gsfopt"

#define GSF_PSF_VERSION	0x22
#define GSF_EXE_HEADER_SIZE	12

#define MIN_GBA_ROM_SIZE	0xC0
#define MAX_GBA_ROM_SIZE	0x02000000
#define MAX_GSF_EXE_SIZE	(MAX_GBA_ROM_SIZE + GSF_EXE_HEADER_SIZE)

GsfOpt::GsfOpt() :
	bytes_used(0),
	optimize_timeout(300.0),
	optimize_progress_frequency(0.2),
	time_loop_based(false),
	target_loop_count(2),
	loop_verify_length(20.0),
	oneshot_verify_length(15),
	paranoid_closed_area_fill_size(3),
	paranoid_post_fill_size(0)
{
	m_system = new GBASystem;
	rom_refs = new u8[MAX_GBA_ROM_SIZE];

	ResetOptimizer();
}

GsfOpt::~GsfOpt()
{
	if (m_system != NULL)
	{
		CPUCleanUp(m_system);
		delete m_system;
	}
	if (rom_refs != NULL)
	{
		delete [] rom_refs;
	}
}

std::string GsfOpt::ToTimeString(double t, bool padding)
{
	if (isnan(t))
	{
		return "NaN";
	}

	double seconds = fmod(t, 60.0);
	unsigned int minutes = (unsigned int) (t - seconds) / 60;
	unsigned int hours = minutes / 60;
	minutes %= 60;
	hours %= 60;

	char str[64];
	if (hours != 0)
	{
		sprintf(str, "%u:%02u:%06.3f", hours, minutes, seconds);
	}
	else
	{
		if (padding || minutes != 0)
		{
			sprintf(str, "%u:%06.3f", minutes, seconds);
		}
		else
		{
			sprintf(str, "%.3f", seconds);
		}
	}

	if (!padding)
	{
		size_t len = strlen(str);
		for (off_t i = (off_t)(len - 1); i >= 0; i--)
		{
			if (str[i] == '0')
			{
				str[i] = '\0';
			}
			else
			{
				if (str[i] == '.')
				{
					str[i] = '\0';
				}
				break;
			}
		}
	}

	return str;
}

double GsfOpt::ToTimeValue(const std::string& str)
{
	if (str.empty())
	{
		return 0.0;
	}

	// split by colons
	std::vector<std::string> tokens;
	size_t current = 0;
	size_t found;
	while((found = str.find_first_of(':', current)) != std::string::npos)
	{
		tokens.push_back(std::string(str, current, found - current));
		current = found + 1;
	}
	tokens.push_back(std::string(str, current, str.size() - current));

	// too many colons?
	if (tokens.size() > 3)
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	// set token for each fields
	const char * s_hours = "0";
	const char * s_minutes = "0";
	const char * s_seconds = "0";
	switch(tokens.size())
	{
		case 1:
			s_seconds = tokens[0].c_str();
			break;

		case 2:
			s_minutes = tokens[0].c_str();
			s_seconds = tokens[1].c_str();
			break;

		case 3:
			s_hours = tokens[0].c_str();
			s_minutes = tokens[1].c_str();
			s_seconds = tokens[2].c_str();
			break;
	}

	if (*s_hours == '\0' || *s_minutes == '\0' || *s_seconds == '\0' ||
		*s_hours == '+' || *s_minutes == '+' || *s_seconds == '+')
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	char * endptr = NULL;
	double n_seconds = strtod(s_seconds, &endptr);
	if (*endptr != '\0' || errno == ERANGE || n_seconds < 0)
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	long n_minutes = strtol(s_minutes, &endptr, 10);
	if (*endptr != '\0' || errno == ERANGE || n_minutes < 0)
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	long n_hours = strtol(s_hours, &endptr, 10);
	if (*endptr != '\0' || errno == ERANGE || n_hours < 0)
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	double tv = n_hours * 3600 + n_minutes * 60 + n_seconds;
	if (tv < 0)
	{
		return std::numeric_limits<double>::quiet_NaN();
	}
	return tv;
}

bool GsfOpt::LoadROM(const void *rom, u32 size, bool multiboot)
{
	rom_path = "";
	rom_filename = "";
	if (m_system->rom != NULL)
	{
		MergeRefs(rom_refs, m_system->rom_refs, GetROMSize());
		CPUCleanUp(m_system);
	}

	m_system->cpuIsMultiBoot = multiboot;

	m_system->soundSampleRate = 44100;
	m_system->soundDeclicking = false;
	m_system->soundInterpolation = false;

	CPULoadRom(m_system, rom, size);
	if (m_system->cpuIsMultiBoot)
	{
		rom_size = size;
	}
	else
	{
		rom_size = m_system->romSize;
	}

	soundInit(m_system, &m_output);
	soundReset(m_system);
	m_output.reset_timer();

	CPUInit(m_system);
	CPUReset(m_system);

	ResetOptimizerVariables();
	return true;
}

bool GsfOpt::LoadROMFile(const std::string& filename)
{
	u8 * rom_buf = NULL;
	bool load_result = false;

	if (PSFFile::IsPSFFile(filename))
	{
		u8 * rom_buf = new u8[MAX_GBA_ROM_SIZE];
		u32 entrypoint;

		load_result = ReadGSFFile(filename, 0, rom_buf, &entrypoint, &rom_size);
		if (load_result)
		{
			bool multiboot = ((entrypoint >> 24) == 0x02);
			load_result = LoadROM(rom_buf, rom_size, multiboot);
			if (load_result)
			{
				char tmppath[PATH_MAX];

				path_getabspath(filename.c_str(), tmppath);
				rom_path = tmppath;

				path_basename(tmppath);
				rom_filename = tmppath;
			}
		}

		delete [] rom_buf;
	}
	else
	{
		// Plain GBA ROM

		FILE *fp = NULL;
		off_t filesize;

		filesize = path_getfilesize(filename.c_str());
		if (filesize <= 0 || filesize > MAX_GBA_ROM_SIZE)
		{
			m_message = filename + " - " + "File size error";
			return false;
		}

		const char * fileext = path_findext(filename.c_str());
		bool multiboot = (fileext != NULL && strcasecmp(fileext, ".wb") == 0);

		fp = fopen(filename.c_str(), "rb");
		if (fp == NULL)
		{
			m_message = filename + " - " + "File size error";
			return false;
		}

		rom_buf = new u8[filesize];
		if (fread(rom_buf, 1, filesize, fp) != filesize)
		{
			m_message = filename + " - " + "Unable to load ROM data";
			fclose(fp);
			return false;
		}

		load_result = LoadROM(rom_buf, filesize, false);
		if (load_result)
		{
			char tmppath[PATH_MAX];

			path_getabspath(filename.c_str(), tmppath);
			rom_path = tmppath;

			path_basename(tmppath);
			rom_filename = tmppath;
		}

		delete [] rom_buf;
		fclose(fp);
	}
	return load_result;
}

void GsfOpt::PatchROM(u32 offset, const void * data, u32 size)
{
	u32 max_rom_size = 0;
	u8 * gba_rom = NULL;

	if (m_system->rom == NULL)
	{
		return;
	}

	if (m_system->cpuIsMultiBoot)
	{
		max_rom_size = 0x40000;
		gba_rom = m_system->workRAM;
	}
	else
	{
		max_rom_size = 0x02000000;
		gba_rom = m_system->rom;
	}

	if (offset >= max_rom_size)
	{
		return;
	}
	if (offset + size > max_rom_size)
	{
		size = max_rom_size - offset;
	}

	memcpy(&gba_rom[offset], data, size);
}

void GsfOpt::ResetGame()
{
	if (m_system->rom == NULL)
	{
		return;
	}

	MergeRefs(rom_refs, m_system->rom_refs, GetROMSize());
	CPUReset(m_system);
	m_output.reset_timer();
}

bool GsfOpt::ReadGSFFile(const std::string& filename, unsigned int nesting_level, u8 * rom_buf, u32 * ptr_entrypoint, u32 * ptr_rom_size)
{
	bool result;
	char str[256];

	// end the nesting hell up
	if (nesting_level > 10)
	{
		m_message = filename + " - " + "Too many gsflibs";
		return false;
	}

	// change base directory
	char savedcwd[PATH_MAX];
	getcwd(savedcwd, PATH_MAX);
	char gsf_dir[PATH_MAX];
	strcpy(gsf_dir, filename.c_str());
	path_dirname(gsf_dir);
	if (gsf_dir[0] != '\0')
	{
		chdir(gsf_dir);
	}

	// open GSF file
	PSFFile * gsf = PSFFile::load(filename);
	if (gsf == NULL)
	{
		m_message = filename + " - " + "PSF load error";
		chdir(savedcwd);
		return false;
	}

	// check version code
	if (gsf->version != GSF_PSF_VERSION)
	{
		m_message = filename + " - " + "Mismatch PSF version";
		chdir(savedcwd);
		return false;
	}

	// top-level initialization
	if (nesting_level == 0)
	{
		if (ptr_rom_size != NULL)
		{
			*ptr_rom_size = 0;
		}
	}

	// handle _lib file
	std::map<std::string, std::string>::iterator it_lib = gsf->tags.lower_bound("_lib");
	bool has_lib = (it_lib != gsf->tags.end() && it_lib->first == "_lib");
	u32 lib_entrypoint;
	if (has_lib)
	{
		if (!ReadGSFFile(it_lib->second, nesting_level + 1, rom_buf, &lib_entrypoint, ptr_rom_size))
		{
			delete gsf;
			chdir(savedcwd);
			return false;
		}
	}

	// GSF EXE header
	u32 entrypoint;
	u32 rom_address;
	u32 rom_size;
	result = true;
	result &= gsf->compressed_exe.readInt(entrypoint);
	result &= gsf->compressed_exe.readInt(rom_address);
	result &= gsf->compressed_exe.readInt(rom_size);
	if (!result)
	{
		m_message = filename + " - " + "Read error at GSF EXE header";
		delete gsf;
		chdir(savedcwd);
		return false;
	}

	// valid entrypoint?
	if (entrypoint != 0x02000000 && entrypoint != 0x08000000)
	{
		sprintf(str, "Unexpected entrypoint 0x%08X", entrypoint);
		m_message = filename + " - " + str;

		// not supported
		delete gsf;
		chdir(savedcwd);
		return false;
	}
	bool multiboot = ((entrypoint >> 24) == 0x02);

	// determine entrypoint
	if (has_lib)
	{
		if (lib_entrypoint != entrypoint)
		{
			sprintf(str, "Inconsistent entrypoint between 0x%08X and lib:0x%08X", entrypoint, lib_entrypoint);
			m_message = filename + " - " + str;

			// inconsistent entrypoint
			delete gsf;
			chdir(savedcwd);
			return false;
		}
		entrypoint = lib_entrypoint;
	}
	*ptr_entrypoint = entrypoint;

	// valid load address?
	u32 rom_offset;
	if ((entrypoint >> 24) == 0x02 && (rom_address >> 24) == 0x02)
	{
		rom_offset = rom_address & 0x3FFFF;
	}
	else if ((entrypoint >> 24) == 0x08 && ((rom_address >> 24) >= 0x08 && (rom_address >> 24) <= 0x0D))
	{
		rom_offset = rom_address & 0x1FFFFFF;
	}
	else
	{
		sprintf(str, "Unsupported load address 0x%08X", rom_address);
		m_message = filename + " - " + str;

		// unsupported address
		delete gsf;
		chdir(savedcwd);
		return false;
	}

	// check offset and size
	if (rom_offset + rom_size > (size_t) (multiboot ? 0x40000 : MAX_GBA_ROM_SIZE))
	{
		m_message = filename + " - " + "ROM size error";

		delete gsf;
		chdir(savedcwd);
		return false;
	}

	// update ROM size
	if (ptr_rom_size != NULL)
	{
		if (*ptr_rom_size < rom_offset + rom_size)
		{
			*ptr_rom_size = rom_offset + rom_size;
		}
	}

	// load ROM data
	if (gsf->compressed_exe.read(&rom_buf[rom_offset], rom_size) != rom_size)
	{
		m_message = filename + " - " + "Unable to load ROM data";

		delete gsf;
		chdir(savedcwd);
		return false;
	}

	// handle _libN files
	int libN = 2;
	while (true)
	{
		char libNname[16];
		sprintf(libNname, "_lib%d", libN);

		std::map<std::string, std::string>::iterator it_libN = gsf->tags.lower_bound(libNname);
		if (it_libN == gsf->tags.end() || it_libN->first != libNname)
		{
			break;
		}

		u32 libN_entrypoint;
		if (!ReadGSFFile(it_libN->second, nesting_level + 1, rom_buf, &libN_entrypoint, ptr_rom_size))
		{
			delete gsf;
			chdir(savedcwd);
			return false;
		}

		if (libN_entrypoint != entrypoint)
		{
			sprintf(str, "Inconsistent entrypoint between 0x%08X and lib%d:0x%08X", entrypoint, libN, libN_entrypoint);
			m_message = filename + " - " + str;

			// inconsistent entrypoint
			delete gsf;
			chdir(savedcwd);
			return false;
		}

		libN++;
	}

	m_message = filename + " - " + "Loaded successfully";
	delete gsf;
	chdir(savedcwd);
	return true;
}

void GsfOpt::ResetOptimizer(void)
{
	memset(rom_refs, 0, MAX_GBA_ROM_SIZE);
	memset(rom_refs_histogram, 0, sizeof(rom_refs_histogram));
	bytes_used = 0;
}

void GsfOpt::Optimize(void)
{
	timer_init();

	bytes_used_old = m_system->bytes_used;
	time_last_new_data = m_output.get_timer();

	for (int i = 0; i < 256; i++)
	{
		loop_point[i] = 0.0;
		loop_point_updated[i] = false;
	}
	loop_count = 0;
	oneshot_endpoint = 0.0;
	oneshot = false;
	initial_silence_length = 0.0;

	double time_last_prog = 0.0;
	bool finished = false;

	do
	{
		bytes_used_old = m_system->bytes_used;
		CPULoop(m_system, 250000);

		initial_silence_length = m_output.get_initial_silence_length();

		// any updates?
		if (m_system->bytes_used != bytes_used_old)
		{
			time_last_new_data = m_output.get_timer();
		}

		// loop detection
		DetectLoop();

		// oneshot detection
		DetectOneShot();

		// adjust endpoint
		AdjustOptimizationEndPoint();

		// is optimization (or loop detection) finished?
		if (m_output.get_timer() >= optimize_endpoint)
		{
			finished = true;
		}

		// show progress
		double time_current = timer_get();
		if (time_current >= time_last_prog + optimize_progress_frequency)
		{
			ShowOptimizeProgress();
			time_last_prog = time_current;
		}
	} while(!finished);

	initial_silence_length = std::min(initial_silence_length, song_endpoint);

	timer_uninit();

	ShowOptimizeResult();
}

void GsfOpt::DetectLoop()
{
	// detect possible maximum value of loop count at the moment
	u8 loop_count_expected_upper = 255;
	for (int count = 1; count < 256; count++)
	{
		if (rom_refs_histogram[count] != m_system->rom_refs_histogram[count])
		{
			loop_count_expected_upper = count - 1;
			break;
		}
	}

	// update loop point of new loops
	for (int count = loop_count_expected_upper; count > 0; count--)
	{
		if (loop_point_updated[count])
		{
			loop_point[count] = m_output.get_timer();
			loop_point_updated[count] = false;
		}
	}

	// verify the loop
	for (int count = loop_count_expected_upper; count > 0; count--)
	{
		if (m_output.get_timer() - loop_point[count] >= loop_verify_length)
		{
			loop_count = count;
			break;
		}
	}

	// update invalid loop points
	for (int count = loop_count_expected_upper + 1; count < 256; count++)
	{
		loop_point[count] = m_output.get_timer();
		loop_point_updated[count] = true;
	}

	// update histogram
	memcpy(rom_refs_histogram, m_system->rom_refs_histogram, sizeof(rom_refs_histogram));
}

void GsfOpt::DetectOneShot()
{
	if (m_output.get_silence_length() >= oneshot_verify_length && loop_count != 0) {
		oneshot_endpoint = m_output.get_silence_start();
		oneshot = true;
	}
	else {
		oneshot = false;
	}
}

void GsfOpt::AdjustOptimizationEndPoint()
{
	if (time_loop_based)
	{
		if (oneshot)
		{
			song_endpoint = oneshot_endpoint;
			optimize_endpoint = m_output.get_timer();
		}
		else
		{
			song_endpoint = loop_point[target_loop_count];
			optimize_endpoint = loop_point[target_loop_count] + std::max<double>(loop_verify_length, oneshot_verify_length);
		}
	}
	else
	{
		song_endpoint = time_last_new_data;
		optimize_endpoint = time_last_new_data + optimize_timeout;
	}
}

void GsfOpt::ShowOptimizeProgress() const
{
	printf("%s: ", rom_filename.substr(0, 24).c_str());
	printf("Time = %s", ToTimeString(song_endpoint).c_str());
	printf(", Remaining = %s", ToTimeString(std::max(0.0, optimize_endpoint - m_output.get_timer())).c_str());
	if (!time_loop_based)
	{
		printf(", %d bytes", m_system->bytes_used);
	}
	else
	{
		printf(", Loop = %d", loop_count + 1);
	}
	fflush(stdout);

	//       1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
}

void GsfOpt::ShowOptimizeResult() const
{
	printf("%s: ", rom_filename.c_str());

	if (!time_loop_based)
	{
		printf("Time = %s", ToTimeString(song_endpoint).c_str());
		printf(", %d bytes", m_system->bytes_used);
	}
	else
	{
		printf("Time = %s, Silence = %s",
			ToTimeString(song_endpoint - initial_silence_length).c_str(),
			ToTimeString(initial_silence_length).c_str());

		if (oneshot)
		{
			printf(" (One Shot)");
		}
		else
		{
			printf(" (%d Loops)", target_loop_count);
		}
	}
	printf("                                            ");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\n");
	fflush(stdout);
}

void GsfOpt::ResetOptimizerVariables()
{
}

u32 GsfOpt::MergeRefs(u8 * dst_refs, const u8 * src_refs, u32 size)
{
	u32 bytes_used = 0;
	for (u32 i = 0; i < size; i++)
	{
		if ((unsigned int)dst_refs[i] + src_refs[i] <= 0xff)
		{
			dst_refs[i] += src_refs[i];
		}
		else
		{
			dst_refs[i] = 0xff;
		}

		if (dst_refs[i] != 0)
		{
			bytes_used++;
		}
	}
	return bytes_used;
}

bool GsfOpt::GetROM(void * rom, u32 size, bool wipe_unused_data) const
{
	u8 * gba_rom = NULL;
	u32 rom_size = GetROMSize();

	if (m_system->rom == NULL)
	{
		return false;
	}

	if (size > rom_size)
	{
		size = rom_size;
	}

	if (m_system->cpuIsMultiBoot)
	{
		gba_rom = m_system->workRAM;
	}
	else
	{
		gba_rom = m_system->rom;
	}

	if (wipe_unused_data)
	{
		u8 * rom_refs = new u8[size];
		memcpy(rom_refs, this->rom_refs, size);
		MergeRefs(rom_refs, m_system->rom_refs, size);

		u32 paranoid_unused_area_size = 0;
		u32 paranoid_post_fill_count = 0;

		for (u32 offset = 0; offset < size; offset++) {
			bool is_header = offset < 0xC0;
			bool is_offset_used = rom_refs[offset] != 0 || is_header;

			if (is_offset_used || paranoid_post_fill_count > 0) {
				((u8 *)rom)[offset] = gba_rom[offset];

				if (paranoid_post_fill_count > 0) {
					paranoid_post_fill_count--;
				}
			} else {
				((u8 *)rom)[offset] = 0;
			}

			if (is_offset_used) {
				paranoid_post_fill_count = paranoid_post_fill_size;

				if (paranoid_unused_area_size <= paranoid_closed_area_fill_size) {
					while (paranoid_unused_area_size > 0) {
						((u8 *)rom)[offset - paranoid_unused_area_size] = gba_rom[offset - paranoid_unused_area_size];
						paranoid_unused_area_size--;
					}
				}
				paranoid_unused_area_size = 0;
			} else {
				paranoid_unused_area_size++;
			}
		}

		delete [] rom_refs;
	}
	else
	{
		memcpy(rom, gba_rom, size);
	}

	return true;
}

bool GsfOpt::SaveROM(const std::string& filename, bool wipe_unused_data) const
{
	u32 size = GetROMSize();
	u8 * rom = new u8[size];
	FILE * fp = NULL;
	bool result = false;

	if (!GetROM(rom, size, wipe_unused_data))
	{
		delete [] rom;
		return false;
	}

	fp = fopen(filename.c_str(), "wb");
	if (fp == NULL)
	{
		delete [] rom;
		return false;
	}

	//while (size > 0 && rom[size - 1] == 0)
	//{
	//	size--;
	//}

	result = (fwrite(rom, 1, size, fp) == size);

	fclose(fp);
	delete [] rom;
	return result;
}

bool GsfOpt::SaveGSF(const std::string& filename, bool wipe_unused_data, std::map<std::string, std::string>& tags) const
{
	u32 size = GetROMSize();
	u8 * rom = new u8[size];
	bool result = false;

	if (!GetROM(rom, size, wipe_unused_data))
	{
		delete [] rom;
		return false;
	}

	ZlibWriter exe(Z_BEST_COMPRESSION);

	result = true;
	result &= exe.writeInt(m_system->cpuIsMultiBoot ? 0x02000000 : 0x08000000);
	result &= exe.writeInt(m_system->cpuIsMultiBoot ? 0x02000000 : 0x08000000);
	result &= exe.writeInt(size);
	if (!result)
	{
		delete [] rom;
		return false;
	}

	if (exe.write(rom, size) != size)
	{
		delete [] rom;
		return false;
	}

	result = PSFFile::save(filename, GSF_PSF_VERSION, NULL, 0, exe, tags);

	delete [] rom;
	return result;
}

enum GsfOptProcMode
{
	GSFOPT_PROC_NONE = 0,
	GSFOPT_PROC_F,
	GSFOPT_PROC_L,
	GSFOPT_PROC_R,
	GSFOPT_PROC_S,
	GSFOPT_PROC_T,
};

static void usage(const char * progname, bool extended)
{
	printf("%s %s\n", APP_NAME, APP_VER);
	printf("<%s>\n", APP_URL);
	printf("\n");
	printf("Usage\n");
	printf("-----\n");
	printf("\n");
	printf("Syntax: `%s [options] [-s or -l or -f or -t] [gsf files]`\n", progname);
	printf("\n");

	if (!extended)
	{
		printf("for detailed usage info, type %s -?\n", progname);
	}
	else
	{
		printf("### Options\n");
		printf("\n");
		printf("`-T [time]`\n");
		printf("  : Runs the emulation till no new data has been found for [time] specified.\n");
		printf("    Time is specified in mm:ss.nnn format   \n");
		printf("    mm = minutes, ss = seoconds, nnn = milliseconds\n");
		printf("\n");
		printf("`-p [bytes]` (default=3)\n");
		printf("  : I am paranoid, and wish to assume that any data \n");
		printf("    within [bytes] bytes between two used bytes, is also used\n");
		printf("\n");
		printf("`-P [bytes]` (default=0)\n");
		printf("  : I am paranoid, and wish to assume that any trailing data \n");
		printf("    within [bytes] bytes of a used byte, is also used\n");
		printf("\n");
		printf("#### File Processing Modes (-s) (-l) (-f) (-r) (-t)\n");
		printf("\n");
		printf("`-f [gsf files]`\n");
		printf("  : Optimize single files, and in the process, convert\n");
		printf("    minigsfs/gsflibs to single gsf files\n");
		printf("\n");
		printf("`-l [gsf files]`\n");
		printf("  : Optimize the gsflib using passed gsf files.\n");
		printf("\n");
		printf("`-r [gsf files]`\n");
		printf("  : Convert to Rom files, no optimization\n");
		printf("\n");
		printf("`-s [gsflib] [Hex offset] [Count]`\n");
		printf("  : Optimize gsflib using a known offset/count\n");
		printf("\n");
		printf("`-t [options] [gsf files]`\n");
		printf("  : Times the GSF files. (for auto tagging, use the `-T` option)\n");
		printf("    Unlike psf playback, silence detection is MANDATORY\n");
		printf("    Do NOT try to evade this with an excessively long silence detect time.\n");
		printf("    (The max time is less than 2*Verify loops for silence detection)\n");
		printf("\n");
		printf("#### Options for -t\n");
		printf("\n");
		printf("`-V [time]`\n");
		printf("  : Length of verify loops at end point. (Default 20 seconds)\n");
		printf("\n");
		printf("`-L [count]`\n");
		printf("  : Number of loops to time for. (Default 2, max 255)\n");
		printf("\n");
		printf("`-T`\n");
		printf("  : Tag the songs with found time.\n");
		printf("    A Fade is also added if the song is not detected to be one shot.\n");
		printf("\n");
		printf("`-F [time]`\n");
		printf("  : Length of looping song fade. (default 10.000)\n");
		printf("\n");
		printf("`-f [time]`\n");
		printf("  : Length of one shot song postgap. (default 1.000)\n");
		printf("\n");
		printf("`-s [time]`\n");
		printf("  : Time in seconds for silence detection (default 15 seconds)\n");
		printf("    Max (2*Verify loop count) seconds.\n");
		printf("\n");
	}
}

int main(int argc, char *argv[])
{
	GsfOpt opt;
	GsfOptProcMode mode = GSFOPT_PROC_NONE;

	std::string out_name;

	double loopFadeLength = 10.0;
	double oneshotPostgapLength = 1.0;
	bool addGSFTags = false;

	long l;
	unsigned long ul;
	char * endptr = NULL;

	char *psfby = NULL;

	if (argc >= 2 && (strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "--help") == 0))
	{
		usage(argv[0], true);
		return -1;
	}
	else if (argc == 1)
	{
		usage(argv[0], false);
		return -1;
	}

	int argi;
	for(argi = 1; argi < argc; argi++)
	{
		if (argv[argi][0] != '-')
		{
			break;
		}

		if (strcmp(argv[argi], "-f") == 0)  //regular .gsf optimization. this option doubles as minigsf->gsf converter
		{
			mode = GSFOPT_PROC_F;

			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}
			argi++;
		}
		else if (strcmp(argv[argi], "-r") == 0)
		{
			mode = GSFOPT_PROC_R;

			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}
			argi++;
		}
		else if (strcmp(argv[argi], "-s") == 0)  //Song value gsflib optimization.
		{
			mode = GSFOPT_PROC_S;

			if (argc <= (argi + 3))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}
			argi++;
		}
		else if (strcmp(argv[argi], "-l") == 0)  //gsflib optimization.
		{
			mode = GSFOPT_PROC_L;

			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}
			argi++;
		}
		else if (strcmp(argv[argi], "-t") == 0)
		{
			mode = GSFOPT_PROC_T;
			opt.SetTimeLoopBased(true);

			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}
			argi++;
		}
		else if (strcmp(argv[argi], "-T") == 0) // Optimize while no new data found for.
		{
			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}

			opt.SetTimeout(GsfOpt::ToTimeValue(argv[argi + 1]));
			argi++;
		}
		else if (strcmp(argv[argi], "-p") == 0) // I am paranoid. assume within x bytes between two used bytes is also used.
		{
			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}

			l = strtol(argv[argi + 1], &endptr, 0);
			if (*endptr != '\0' || errno == ERANGE || l < 0)
			{
				fprintf(stderr, "Error: Number format error \"%s\"\n", argv[argi + 1]);
				return 1;
			}
			opt.SetParanoidClosedAreaFillSize(l);
			argi++;
		}
		else if (strcmp(argv[argi], "-P") == 0) // I am paranoid. assume within x trailing bytes of a used byte is also used.
		{
			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}

			l = strtol(argv[argi + 1], &endptr, 0);
			if (*endptr != '\0' || errno == ERANGE || l < 0)
			{
				fprintf(stderr, "Error: Number format error \"%s\"\n", argv[argi + 1]);
				return 1;
			}
			opt.SetParanoidPostFillSize(l);
			argi++;
		}
		else if (strcmp(argv[argi], "-o") == 0) // output name
		{
			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}

			out_name = argv[argi + 1];
			argi++;
		}
		else if (strcmp(argv[argi], "--psfby") == 0 || strcmp(argv[argi], "--gsfby") == 0)
		{
			if (argc <= (argi + 1))
			{
				fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
				return 1;
			}

			psfby = argv[argi + 1];
			argi++;
		}
		else
		{
			fprintf(stderr, "Error: Unknown option \"%s\"\n", argv[argi]);
			return 1;
		}

		if (mode != GSFOPT_PROC_NONE)
		{
			if (mode == GSFOPT_PROC_T)
			{
				for (; argi < argc; argi++)
				{
					if (argv[argi][0] != '-')
					{
						break;
					}

					if (strcmp(argv[argi], "-V") == 0)
					{
						if (argc <= (argi + 1))
						{
							fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
							return 1;
						}
						opt.SetLoopVerifyLength(GsfOpt::ToTimeValue(argv[argi + 1]));
						argi++;
					}
					else if (strcmp(argv[argi], "-L") == 0)
					{
						if (argc <= (argi + 1))
						{
							fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
							return 1;
						}

						l = strtol(argv[argi + 1], &endptr, 0);
						if (*endptr != '\0' || errno == ERANGE || l < 0)
						{
							fprintf(stderr, "Error: Number format error \"%s\"\n", argv[argi + 1]);
							return 1;
						}
						if (l == 0 || l > 255)
						{
							fprintf(stderr, "Error: Loop count must be in range (1..255)\n");
							return 1;
						}
						opt.SetTargetLoopCount((u8)l);
						argi++;
					}
					else if (strcmp(argv[argi], "-T") == 0)
					{
						addGSFTags = true;
					}
					else if (strcmp(argv[argi], "-F") == 0)
					{
						if (argc <= (argi + 1))
						{
							fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
							return 1;
						}

						loopFadeLength = GsfOpt::ToTimeValue(argv[argi + 1]);
						argi++;
					}
					else if (strcmp(argv[argi], "-f") == 0)
					{
						if (argc <= (argi + 1))
						{
							fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
							return 1;
						}

						oneshotPostgapLength = GsfOpt::ToTimeValue(argv[argi + 1]);
						argi++;
					}
					else if (strcmp(argv[argi], "-s") == 0)
					{
						if (argc <= (argi + 1))
						{
							fprintf(stderr, "Error: Too few arguments for \"%s\"\n", argv[argi]);
							return 1;
						}

						opt.SetOneShotVerifyLength(GsfOpt::ToTimeValue(argv[argi + 1]));
						argi++;
					}
					else
					{
						fprintf(stderr, "Error: Unknown option \"%s\"\n", argv[argi]);
						return 1;
					}
				}

				if (opt.GetOneShotVerifyLength() > (opt.GetLoopVerifyLength() * 2))
				{
					double oneshot_verify_length = opt.GetLoopVerifyLength() * 2;
					opt.SetOneShotVerifyLength(oneshot_verify_length);
					fprintf(stderr, "Warning: Max silence length is %s\n", GsfOpt::ToTimeString(oneshot_verify_length).c_str());
				}
			}
			break;
		}
	}

	if (mode == GSFOPT_PROC_NONE)
	{
		fprintf(stderr, "Error: You need to specify a processing mode, -f, -s, -l, -r, -t\n");
		return 1;
	}

	switch (mode)
	{
		case GSFOPT_PROC_S:
		{
			ul = strtoul(argv[argi + 1], &endptr, 16);
			if (*endptr != '\0' || errno == ERANGE)
			{
				fprintf(stderr, "Error: Number format error \"%s\"\n", argv[argi + 1]);
				return 1;
			}
			u32 minigsf_offset = ul & 0x1FFFFFF;

			l = strtol(argv[argi + 2], &endptr, 0);
			if (*endptr != '\0' || errno == ERANGE || l < 0)
			{
				fprintf(stderr, "Error: Number format error \"%s\"\n", argv[argi + 1]);
				return 1;
			}
			u32 minigsf_count = (u32) l;

			u32 minigsf_size = 0;
			do
			{
				minigsf_size++;
			} while (minigsf_count >> (minigsf_size * 8));

			// determine output filename
			std::string out_path;
			if (out_name.empty())
			{
				const char *ext = path_findext(argv[argi]);
				if (*ext == '\0')
				{
					out_path = argv[argi];
					out_path += ".gsflib";
				}
				else
				{
					out_path = std::string(argv[argi], ext - argv[argi]);
					out_path += ".gsflib";
				}
			}
			else
			{
				out_path = out_name;

				const char *ext = path_findext(out_name.c_str());
				if (*ext == '\0')
				{
					out_path += ".gsflib";
				}
			}

			if (opt.GetParanoidPostFillSize() > 0 || opt.GetParanoidClosedAreaFillSize() > 0) {
				printf("I am paranoid. (closed area = %d bytes, post = %d bytes)\n",
					opt.GetParanoidClosedAreaFillSize(), opt.GetParanoidPostFillSize());
			}

			// optimize
			opt.ResetOptimizer();
			if (!opt.LoadROMFile(argv[argi]))
			{
				fprintf(stderr, "Error: %s\n", opt.message().c_str());
				return 1;
			}
			for (u32 song = 0; song < minigsf_count; song++)
			{
				printf("Optimizing %s  Song value %X\n", argv[argi], song);

				u8 patch[4] = {
					static_cast<uint8_t>(song & 0xff),
					static_cast<uint8_t>((song >> 8) & 0xff),
					static_cast<uint8_t>((song >> 16) & 0xff),
					static_cast<uint8_t>((song >> 24) & 0xff),
				};
				opt.PatchROM(minigsf_offset, patch, minigsf_size);
				opt.ResetGame();

				opt.Optimize();
			}

			std::map<std::string, std::string> tags;
			if (psfby != NULL && strcmp(psfby, "") != 0) {
				tags["gsfby"] = psfby;
			}

			opt.SaveGSF(out_path, true, tags);
			break;
		}

		case GSFOPT_PROC_L:
		{
			// determine output filename
			std::string out_path = out_name;
			if (out_name.empty())
			{
				const char *ext = path_findext(argv[argi]);
				if (*ext == '\0')
				{
					out_path = argv[argi];
					out_path += ".gsflib";
				}
				else
				{
					out_path = std::string(argv[argi], ext - argv[argi]);
					out_path += ".gsflib";
				}
			}
			else
			{
				out_path = out_name;

				const char *ext = path_findext(out_name.c_str());
				if (*ext == '\0')
				{
					out_path += ".gsflib";
				}
			}

			if (opt.GetParanoidPostFillSize() > 0 || opt.GetParanoidClosedAreaFillSize() > 0) {
				printf("I am paranoid. (closed area = %d bytes, post = %d bytes)\n",
					opt.GetParanoidClosedAreaFillSize(), opt.GetParanoidPostFillSize());
			}

			// optimize
			opt.ResetOptimizer();
			for (; argi < argc; argi++)
			{
				printf("Optimizing %s\n", argv[argi]);

				if (!opt.LoadROMFile(argv[argi]))
				{
					fprintf(stderr, "Error: %s\n", opt.message().c_str());
					return 1;
				}
				opt.Optimize();
			}

			std::map<std::string, std::string> tags;
			if (psfby != NULL && strcmp(psfby, "") != 0) {
				tags["gsfby"] = psfby;
			}

			opt.SaveGSF(out_path, true, tags);
			break;
		}

		case GSFOPT_PROC_F:
		{
			if (argi + 1 < argc && !out_name.empty())
			{
				fprintf(stderr, "Error: Output filename cannot be specified to multiple ROMs.\n");
				return 1;
			}

			if (opt.GetParanoidPostFillSize() > 0 || opt.GetParanoidClosedAreaFillSize() > 0) {
				printf("I am paranoid. (closed area = %d bytes, post = %d bytes)\n",
					opt.GetParanoidClosedAreaFillSize(), opt.GetParanoidPostFillSize());
			}

			// optimize
			for (; argi < argc; argi++)
			{
				// determine output filename
				std::string out_path = out_name;
				if (out_name.empty())
				{
					const char *ext = path_findext(argv[argi]);
					if (*ext == '\0')
					{
						out_path = argv[argi];
						out_path += ".gsf";
					}
					else
					{
						out_path = std::string(argv[argi], ext - argv[argi]);
						out_path += ".gsf";
					}
				}
				else
				{
					out_path = out_name;

					const char *ext = path_findext(out_name.c_str());
					if (*ext == '\0')
					{
						out_path += ".gsf";
					}
				}

				printf("Optimizing %s\n", argv[argi]);

				opt.ResetOptimizer();
				if (!opt.LoadROMFile(argv[argi]))
				{
					fprintf(stderr, "Error: %s\n", opt.message().c_str());
					return 1;
				}
				opt.Optimize();

				std::map<std::string, std::string> tags;
				if (psfby != NULL && strcmp(psfby, "") != 0) {
					tags["gsfby"] = psfby;
				}

				opt.SaveGSF(out_path, true, tags);
			}
			break;
		}

		case GSFOPT_PROC_R:
		{
			if (argi + 1 < argc && !out_name.empty())
			{
				fprintf(stderr, "Error: Output filename cannot be specified to multiple ROMs.\n");
				return 1;
			}

			for (int i = argi; i < argc; i++)
			{
				std::string out_path;
				if (out_name.empty())
				{
					const char *ext = path_findext(argv[i]);
					if (*ext == '\0')
					{
						out_path = argv[i];
						out_path += ".gba";
					}
					else
					{
						out_path = std::string(argv[i], ext - argv[i]);
						out_path += ".gba";
					}
				}
				else
				{
					out_path = out_name;

					const char *ext = path_findext(out_name.c_str());
					if (*ext == '\0')
					{
						out_path += ".gba";
					}
				}

				if (!opt.LoadROMFile(argv[i]))
				{
					fprintf(stderr, "Error: %s\n", opt.message().c_str());
					return 1;
				}
				opt.SaveROM(out_path, false);
			}
			break;
		}

		case GSFOPT_PROC_T:
		{
			if (!out_name.empty())
			{
				fprintf(stderr, "Error: Output filename cannot be specified for \"-t\".\n");
				return 1;
			}

			// optimize
			for (; argi < argc; argi++)
			{
				// determine output filename
				std::string out_path = argv[argi];

				opt.ResetOptimizer();
				if (!opt.LoadROMFile(argv[argi]))
				{
					fprintf(stderr, "Error: %s\n", opt.message().c_str());
					return 1;
				}
				opt.Optimize();

#ifdef _DEBUG
				for (int count = 1; count <= opt.GetTargetLoopCount(); count++)
				{
					printf("Loop Point %d = %s\n", count, opt.GetLoopPointString(count).c_str());
				}
#endif

				if (addGSFTags)
				{
					PSFFile * gsf = PSFFile::load(argv[argi]);
					if (gsf == NULL)
					{
						fprintf(stderr, "Error: Invalid PSF file %s (file operation error)\n", argv[argi]);
						return 1;
					}

					if (opt.IsOneShot())
					{
						if (opt.GetOneShotEndPoint() == opt.GetInitialSilenceLength())
						{
							gsf->tags["length"] = "0";
						}
						else
						{
							gsf->tags["length"] = GsfOpt::ToTimeString(opt.GetOneShotEndPoint() + oneshotPostgapLength - opt.GetInitialSilenceLength(), false);
						}
						gsf->tags["fade"] = "0";
					}
					else
					{
						gsf->tags["length"] = GsfOpt::ToTimeString(opt.GetLoopPoint() - opt.GetInitialSilenceLength(), false);

						if (loopFadeLength >= 0.001)
						{
							gsf->tags["fade"] = GsfOpt::ToTimeString(loopFadeLength, false);
						}
						else
						{
							gsf->tags["fade"] = "0";
						}
					}

					gsf->save(out_path);
					delete gsf;
				}
			}
			break;
		}

		default:
			fprintf(stderr, "Sorry, specified processing mode is not supported yet.\n");
			return 1;
	}

	return 0;
}
