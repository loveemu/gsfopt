
#ifndef GSFOPT_H
#define GSFOPT_H

#include <string>
#include <map>

#include "vbam/gba/GBA.h"

class GsfOpt
{
public:
	GsfOpt();
	virtual ~GsfOpt();

	bool LoadROM(const void *rom, u32 size, bool multiboot);
	bool LoadROMFile(const std::string& filename);
	void PatchROM(u32 offset, const void * data, u32 size);
	void ResetGame(void);

	void ResetOptimizer(void);
	void Optimize(void);

	bool GetROM(void * rom, u32 size, bool wipe_unused_data) const;
	bool SaveROM(const std::string& filename, bool wipe_unused_data) const;
	bool SaveGSF(const std::string& filename, bool wipe_unused_data) const;
	bool SaveGSF(const std::string& filename, bool wipe_unused_data, std::map<std::string, std::string>& tags) const;

	inline u32 GetROMSize(void) const
	{
		return rom_size;
	}

	inline double GetTimeout(void) const
	{
		return optimize_timeout;
	}

	inline void SetTimeout(double timeout)
	{
		optimize_timeout = timeout;
	}

	inline u32 GetParanoidSize(void) const
	{
		return paranoid_bytes;
	}

	inline void SetParanoidSize(u32 size)
	{
		paranoid_bytes = size;
	}

	inline const std::string& message(void) const
	{
		return m_message;
	}

	static std::string ToTimeString(double t);
	static double ToTimeValue(const std::string& str);

protected:
	GBASystem * m_system;
	u32 rom_size;

	struct gsf_sound_out : public GBASoundOut
	{
		u32 sample_rate;
		u32 samples_received;
		u32 silent_samples_received;
		u16 silence_threshold;
		u32 silence_start;

		gsf_sound_out() :
			sample_rate(44100),
			silence_threshold(8)
		{
			reset_timer();
		}

		virtual ~gsf_sound_out()
		{
		}

		// Receives signed 16-bit stereo audio and a byte count
		virtual void write(const void * samples, unsigned long bytes)
		{
			samples_received += (bytes / 2);

			for (unsigned int i = 0; i < (bytes / 2); i++)
			{
				s16 samp = ((s16 *)samples)[i];
				if ((samp + silence_threshold) >= 0 && (samp + silence_threshold) < (silence_threshold * 2))
				{
					if (silent_samples_received == 0)
					{
						silence_start = samples_received;
					}
					silent_samples_received++;
				}
				else
				{
					silence_start = samples_received;
					silent_samples_received = 0;
				}
			}
		}

		void reset_timer(void)
		{
			samples_received = 0;
			silence_start = 0;
			silent_samples_received = 0;
		}

		double get_timer(void) const
		{
			return (double) samples_received / 2 / sample_rate;
		}

		double get_silence_start(void) const
		{
			return (double) silence_start / 2 / sample_rate;
		}

		double get_silence_length(void) const
		{
			return (double) silent_samples_received / 2 / sample_rate;
		}
	};
	gsf_sound_out m_output;

	u8 * rom_refs;
	u32 bytes_used;
	double optimize_timeout;
	double optimize_endpoint;
	double optimize_progress_frequency;

	double time_last_new_data;
	double loop_point[256];
	u8 loop_count;

	u32 paranoid_bytes;

	bool ReadGSFFile(const std::string& filename, unsigned int nesting_level, u8 * rom_buf, u32 * ptr_entrypoint, u32 * ptr_rom_size);

	static u32 MergeRefs(u8 * dst_refs, const u8 * src_refs, u32 size);

	virtual void DetectLoop(void);
	virtual void AdjustOptimizationEndPoint(void);
	virtual void ResetOptimizerVariables(void);
	virtual void ShowOptimizeProgress(void) const;
	virtual void ShowOptimizeResult(void) const;

	std::string m_message;

private:
	std::string rom_path;
	u32 bytes_used_old;
};

#endif
