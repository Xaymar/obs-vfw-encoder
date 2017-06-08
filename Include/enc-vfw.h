#pragma once
#include "plugin.h"
#include "libobs/obs-encoder.h"

#include <string>
#include <vector>
#include <fstream>

// VFW
#define COMPMAN
#define VIDEO
#define MMREG
#include <windows.h>
extern "C" {
	#include <Vfw.h>
	#include <vfwext.h>
	#include <vfwmsgs.h>
};

namespace VFW {
	struct Info {
		std::string Id;
		std::string Name;
		std::string Path;
		ICINFO icInfo;
		ICINFO icInfo2;
		size_t index;
		obs_encoder_info obsInfo;

		std::string FourCC, FourCC2;
		std::vector<uint8_t> stateInfo;

		int32_t defaultQuality;
		int32_t defaultKeyframeRate;
		bool hasConfigure, hasAbout;
	};
	bool Initialize();
	bool Finalize();

	class Encoder {
		public:

		static const char* get_name(void* type_data);
		static void get_defaults(obs_data_t *settings);
		static obs_properties_t* get_properties(void *data);
		static bool cb_configure(obs_properties_t *pr, obs_property_t *p, void *data);
		static bool cb_about(obs_properties_t *pr, obs_property_t *p, void *data);

		static void* create(obs_data_t *settings, obs_encoder_t *encoder);
		Encoder(obs_data_t *settings, obs_encoder_t *encoder);

		static void destroy(void* data);
		~Encoder();

		static bool encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet);
		bool encode(struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet);

		static bool update(void *data, obs_data_t *settings);
		bool update(obs_data_t* settings);

		static bool get_extra_data(void *data, uint8_t **extra_data, size_t *size);
		bool get_extra_data(uint8_t** extra_data, size_t* size);

		static bool get_sei_data(void *data, uint8_t **sei_data, size_t *size);
		bool get_sei_data(uint8_t** sei_data, size_t* size);

		static void get_video_info(void *data, struct video_scale_info *info);
		void get_video_info(struct video_scale_info *info);
		
		private:
		VFW::Info* myInfo;
		HIC hIC;
		COMPVARS cv;
		std::vector<char>
			m_bufferInput,
			m_bufferInputBitmapInfo,
			m_bufferPrevInput,
			m_bufferPrevInputBitmapInfo,
			m_bufferOutput,
			m_bufferOutputBitmapInfo;
		BITMAPINFO 
			*m_inputBitmapInfo,
			*m_prevInputBitmapInfo,
			*m_outputBitmapInfo;

		uint32_t 
			m_width, m_height,
			m_fpsNum, m_fpsDen,
			m_keyframeInterval,
			m_bitrate, m_quality;
		bool 
			m_useNormalCompress, 
			m_useTemporalFlag,
			m_useBitrateFlag,
			m_useQualityFlag;
	};
};
