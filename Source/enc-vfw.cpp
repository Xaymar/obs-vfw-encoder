#include "enc-vfw.h"
#include "libobs/obs-encoder.h"

#include <list>
#include <vector>
#include <map>
#include <emmintrin.h>

std::map<std::string, VFW::Info*> _IdToInfo;

#define snprintf sprintf_s

std::vector<std::pair<const char*, const char*>> codecCorrections = {
	// Cinepak Codec
	{ "cvid", "cinepak" }, //AV_CODEC_ID_CINEPAK

	// Intel IYUV Encoder
	{ "iyuv", "h263i" }, // Potentially Intel h263p or h263i?
	{ "i420", "h263p" },

	// Microsoft
	{ "mrle", "msrle" }, //AV_CODEC_ID_MSRLE
	{ "msvc", "msvideo1" }, //AV_CODEC_ID_MSVIDEO1

	// x264vfw
	{ "x264", "h264" },

	// Matrox
	{ "mjpg", "mjpeg" }, // Matrox M-JPEG (playback only)
	//{ "M101", "" }, // Matrox Uncompressed SD
	//{ "M102", "" }, // Matrox Uncompressed HD
	{ "m103", "ayuv" }, // Matrox Uncompressed SD + Alpha
	{ "m104", "ayuv" }, // Matrox Uncompressed HD + Alpha
	{ "m702", "mpeg2video" }, // Matrox Offline HD
	{ "m703", "mpeg2video" }, // Matrox HDV (playback only)
	{ "mmes", "mpeg2video" }, // Matrox MPEG-2 I-frame
	{ "m704", "mpeg2video" }, // Matrox MPEG-2 I-frame + Alpha
	{ "m701", "mpeg2video" }, // Matrox MPEG-2 I-frame HD
	{ "m705", "mpeg2video" }, // Matrox MPEG-2 I-frame HD + Alpha 
	{ "dvh1", "dvvideo" }, // Matrox DVCPRO HD
	{ "dvsd", "dvvideo" }, // Matrox DV/DVCAM
	{ "dv25", "dvvideo" }, // Matrox DVCPRO
	{ "dv50", "dvvideo" }, // Matrox DVCPRO50
};

std::string FourCCFromInt32(DWORD& fccHandler) {
	return std::string(reinterpret_cast<char*>(&fccHandler), 4);
}

std::string FormattedICCError(LRESULT error) {
	switch (error) {
		case ICERR_OK:
			return "Ok";
		case ICERR_UNSUPPORTED:
			return "Unsupported";
		case ICERR_BADFORMAT:
			return "Bad Format";
		case ICERR_MEMORY:
			return "Memory";
		case ICERR_INTERNAL:
			return "Internal";
		case ICERR_BADFLAGS:
			return "Bad Flags";
		case ICERR_BADPARAM:
			return "Bad Parameter";
		case ICERR_BADSIZE:
			return "Bad Size";
		case ICERR_BADHANDLE:
			return "Bad Handle";
		case ICERR_CANTUPDATE:
			return "Can't Update";
		case ICERR_ABORT:
			return "Abort";
		case ICERR_ERROR:
			return "Generic Error";
		case ICERR_BADBITDEPTH:
			return "Bad Bit Depth";
		case ICERR_BADIMAGESIZE:
			return "Bad Image Size";
		case ICERR_CUSTOM:
		default:
			return "Custom Error";
	}
}

bool VFW::Initialize() {
	// Initialize all VFW Encoders (we can only use one anyway)
	ICINFO icinfo;
	std::memset(&icinfo, 0, sizeof(ICINFO));
	icinfo.dwSize = sizeof(icinfo);

	DWORD fccType = 0;
	for (size_t i = 0; ICInfo(fccType, (DWORD)i, &icinfo); i++) {
		HIC hIC = ICOpen(icinfo.fccType, icinfo.fccHandler, ICMODE_QUERY);
		if (hIC) {
			ICINFO icinfo2;
			if (ICGetInfo(hIC, &icinfo2, sizeof(icinfo2))) {
				// Track
				VFW::Info* info = new VFW::Info();
				std::memcpy(&info->icInfo, &icinfo, sizeof(ICINFO));
				std::memcpy(&info->icInfo2, &icinfo2, sizeof(ICINFO));
				info->FourCC = FourCCFromInt32(info->icInfo.fccHandler);
				info->FourCC2 = FourCCFromInt32(info->icInfo2.fccHandler);
				info->index = i;

				std::vector<char> idBuf(64);
				snprintf(idBuf.data(), idBuf.size(), "%.16ls", icinfo2.szName);
				info->Id = std::string(idBuf.data()) + "-" + info->FourCC;

				std::vector<char> nameBuf(1024);
				snprintf(nameBuf.data(), nameBuf.size(), "%.128ls [%s] (" PLUGIN_NAME ")", icinfo2.szDescription, info->FourCC.c_str());
				info->Name = std::string(nameBuf.data());

				std::vector<char> pathBuf(512);
				snprintf(pathBuf.data(), pathBuf.size(), "%.128ls", icinfo2.szDriver);
				info->Path = std::string(pathBuf.data());

				// Register
				std::memset(&info->obsInfo, 0, sizeof(obs_encoder_info));
				info->obsInfo.id = info->Id.data();
				info->obsInfo.type = OBS_ENCODER_VIDEO;
				info->obsInfo.codec = info->FourCC.c_str();
				for (auto& kv : codecCorrections) {
					if (kv.first == info->FourCC) {
						info->obsInfo.codec = kv.second;
					}
				}
				info->obsInfo.type_data = info; // circular reference but whatever, it's not reference counted
				info->obsInfo.get_name = VFW::Encoder::get_name;
				info->obsInfo.create = VFW::Encoder::create;
				info->obsInfo.destroy = VFW::Encoder::destroy;
				info->obsInfo.encode = VFW::Encoder::encode;
				info->obsInfo.get_defaults = VFW::Encoder::get_defaults;
				info->obsInfo.get_properties = VFW::Encoder::get_properties;
				info->obsInfo.update = VFW::Encoder::update;
				//info->obsInfo.get_extra_data = VFW::Encoder::get_extra_data;
				//info->obsInfo.get_sei_data = VFW::Encoder::get_sei_data;
				info->obsInfo.get_video_info = VFW::Encoder::get_video_info;

				info->defaultQuality = ICGetDefaultQuality(hIC);
				info->defaultKeyframeRate = ICGetDefaultKeyFrameRate(hIC);
				info->hasConfigure = ICQueryConfigure(hIC);
				info->hasAbout = ICQueryAbout(hIC);

				PLOG_INFO("Registering '%s' (Id: %s, FourCC1: %s, FourCC2: %s, Codec: %s, Driver: '%s', DefQual: %ld, DefKfR: %ld)",
					info->Name.c_str(),
					info->Id.c_str(),
					info->FourCC.c_str(),
					info->FourCC2.c_str(),
					info->obsInfo.codec,
					info->Path.c_str(),
					info->defaultQuality,
					info->defaultKeyframeRate);

				obs_register_encoder(&info->obsInfo);
				_IdToInfo.insert(std::make_pair(info->Id, info));
			}
			ICClose(hIC);
		}
	}
	return true;
}

bool VFW::Finalize() {
	return true;
}

const char* VFW::Encoder::get_name(void* type_data) {
	VFW::Info* info = static_cast<VFW::Info*>(type_data);
	return info->Name.data();
}

void VFW::Encoder::get_defaults(obs_data_t *settings) {
	obs_data_set_default_int(settings, PROP_BITRATE, 0);
	obs_data_set_default_double(settings, PROP_QUALITY, 100.0);
	obs_data_set_default_double(settings, PROP_KEYFRAME_INTERVAL, 0.0);
}

obs_properties_t* VFW::Encoder::get_properties(void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(data);

	obs_properties_t* pr = obs_properties_create();
	obs_properties_set_param(pr, data, nullptr);
	obs_property_t* p;

	p = obs_properties_add_int_slider(pr, PROP_BITRATE, "Bitrate", 0, 300000, 1);
	obs_property_set_visible(p, ((info->icInfo2.dwFlags & VIDCF_CRUNCH) != 0));
	p = obs_properties_add_float_slider(pr, PROP_QUALITY, "Quality", 1, 100, 0.01);
	obs_property_set_visible(p, ((info->icInfo2.dwFlags & VIDCF_QUALITY) != 0));
	p = obs_properties_add_float(pr, PROP_KEYFRAME_INTERVAL, "Keyframe Interval", 0.00, 30.00, 0.01);

	p = obs_properties_add_list(pr, PROP_MODE, "Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Normal", PROP_MODE_NORMAL);
	if ((info->icInfo2.dwFlags & VIDCF_TEMPORAL) != 0)
		obs_property_list_add_string(p, "Temporal", PROP_MODE_TEMPORAL);
	obs_property_list_add_string(p, "Sequential", PROP_MODE_SEQUENTIAL);

	p = obs_properties_add_button(pr, PROP_CONFIGURE, "Configure", cb_configure);
	obs_property_set_visible(p, info->hasConfigure);
	p = obs_properties_add_button(pr, PROP_ABOUT, "About", cb_about);
	obs_property_set_visible(p, info->hasAbout);

	return pr;
}

bool VFW::Encoder::cb_configure(obs_properties_t *pr, obs_property_t *p, void *data) {
	UNREFERENCED_PARAMETER(pr);
	UNREFERENCED_PARAMETER(p);
	UNREFERENCED_PARAMETER(data);

	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));

	HIC hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	if (info->stateInfo.size() > 0) {
		LRESULT err = ICSetState(hIC, info->stateInfo.data(), info->stateInfo.size());
		if (err != ICERR_OK) {
			PLOG_ERROR("Failed to set state before Configure: %s.",
				FormattedICCError(err).c_str());
		}
	} else {
		ICSetState(hIC, NULL, 0);
	}
	ICConfigure(hIC, GetDesktopWindow());
	DWORD size = ICGetStateSize(hIC);
	if (size > 0) {
		info->stateInfo.resize(size);
		LRESULT err = ICGetState(hIC, info->stateInfo.data(), size);
		if (err != ICERR_OK) {
			PLOG_ERROR("Failed to retrieve state after Configure: %s.",
				FormattedICCError(err).c_str());
		}
	}
	ICClose(hIC);

	return false;
}

bool VFW::Encoder::cb_about(obs_properties_t *pr, obs_property_t *p, void *data) {
	UNREFERENCED_PARAMETER(pr);
	UNREFERENCED_PARAMETER(p);
	UNREFERENCED_PARAMETER(data);

	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));
	HIC hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	ICAbout(hIC, GetDesktopWindow());
	ICClose(hIC);

	return false;
}

void* VFW::Encoder::create(obs_data_t *settings, obs_encoder_t *encoder) {
	try {
		return new VFW::Encoder(settings, encoder);
	} catch (...) {
		return nullptr;
	}
}

VFW::Encoder::Encoder(obs_data_t *settings, obs_encoder_t *encoder) {
	PLOG_DEBUG(__FUNCTION_NAME__);

	myInfo = static_cast<VFW::Info*>(obs_encoder_get_type_data(encoder));

	LRESULT err = ICERR_OK;

	// Generic information.
	video_t* obsVideo = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(obsVideo);
	m_width = obs_encoder_get_width(encoder);	m_height = obs_encoder_get_height(encoder);
	m_fpsNum = voi->fps_num;	m_fpsDen = voi->fps_den;
	double_t factor = double_t(m_fpsNum) / double_t(m_fpsDen);
	m_keyframeInterval = max(uint32_t(factor * obs_data_get_double(settings, PROP_KEYFRAME_INTERVAL)), 0);
	m_bitrate = uint32_t(obs_data_get_int(settings, PROP_BITRATE));
	m_quality = uint32_t(obs_data_get_double(settings, PROP_QUALITY) * 100);

	hIC = ICOpen(myInfo->icInfo.fccType, myInfo->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	if (!hIC) {
		PLOG_ERROR("Failed to create '%s' VFW encoder.",
			myInfo->Name.c_str());
		throw std::exception();
	} else {
		PLOG_INFO("Created '%s' VFW encoder.",
			myInfo->Name.c_str());
	}

	PLOG_DEBUG("Initializing at %" PRIu32 "x%" PRIu32 " with %" PRIu32 "/%" PRIu32 " FPS",
		m_width, m_height, m_fpsNum, m_fpsDen);
	PLOG_DEBUG("Using Bitrate %" PRIu32 " kbit, Quality %" PRIu32 ".%02" PRIu32 "%%, Keyframe Interval %" PRIu32,
		m_bitrate,
		m_quality / 100, m_quality % 100,
		m_keyframeInterval);

	// Store temporary flags
	m_useBitrateFlag = (myInfo->icInfo2.dwFlags & VIDCF_CRUNCH) != 0;
	m_useQualityFlag = (myInfo->icInfo2.dwFlags & VIDCF_QUALITY) != 0;

	const char* emode = obs_data_get_string(settings, PROP_MODE);
	if (strcmp(emode, PROP_MODE_NORMAL)) {
		m_useTemporalFlag = false;
		m_useNormalCompress = true;
	} else if (strcmp(emode, PROP_MODE_TEMPORAL)) {
		m_useTemporalFlag = true;
		m_useNormalCompress = true;
	} else {
		m_useTemporalFlag = false;
		m_useNormalCompress = false;
	}

	// Load State from memory.
	if (myInfo->stateInfo.size() > 0) {
		LRESULT err = ICSetState(hIC, myInfo->stateInfo.data(), myInfo->stateInfo.size());
		if (err != ICERR_OK) {
			PLOG_ERROR("Failed to set state before encoding: %s.",
				FormattedICCError(err).c_str());
		}
	} else {
		ICSetState(hIC, NULL, 0);
	}

#pragma region Get Bitmap Information
	m_bufferInputBitmapInfo.resize(sizeof(BITMAPINFOHEADER));
	std::memset(m_bufferInputBitmapInfo.data(), 0, m_bufferInputBitmapInfo.size());
	m_inputBitmapInfo = reinterpret_cast<BITMAPINFO*>(m_bufferInputBitmapInfo.data());
	m_inputBitmapInfo->bmiHeader.biSize = (DWORD)m_bufferInputBitmapInfo.size();
	m_inputBitmapInfo->bmiHeader.biWidth = m_width;
	m_inputBitmapInfo->bmiHeader.biHeight = m_height;
	m_inputBitmapInfo->bmiHeader.biPlanes = 1;
	m_inputBitmapInfo->bmiHeader.biBitCount = 32;
	m_inputBitmapInfo->bmiHeader.biCompression = BI_RGB;
	m_inputBitmapInfo->bmiHeader.biSizeImage = m_width * m_height * (m_inputBitmapInfo->bmiHeader.biBitCount / 8) * m_inputBitmapInfo->bmiHeader.biPlanes;

	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, (DWORD_PTR)m_inputBitmapInfo, NULL);
	if (err <= 0) {
		PLOG_ERROR("Unable to retrieve format information size: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}

	if (m_useTemporalFlag) {
		m_bufferPrevInputBitmapInfo.resize(m_bufferInputBitmapInfo.size());
		std::memcpy(m_bufferPrevInputBitmapInfo.data(), m_bufferInputBitmapInfo.data(), m_bufferInputBitmapInfo.size());
	}

	m_bufferOutputBitmapInfo.resize(err);
	std::memset(m_bufferOutputBitmapInfo.data(), 0, m_bufferOutputBitmapInfo.size());
	m_outputBitmapInfo = (BITMAPINFO*)m_bufferOutputBitmapInfo.data();
	m_outputBitmapInfo->bmiHeader.biSize = (DWORD)m_bufferOutputBitmapInfo.size();
	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, (DWORD_PTR)m_inputBitmapInfo, (DWORD_PTR)m_outputBitmapInfo);
	if (err != ICERR_OK) {
		PLOG_ERROR("Unable to retrieve format information: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}
#pragma endregion Get Bitmap Information

	// Prepare Input Buffers
	size_t alignedWidth = (m_width / 16) * 16;
	const size_t bufferSize = alignedWidth * m_height * 4;
	m_bufferInput.resize(bufferSize);
	m_bufferPrevInput.resize(bufferSize);

	// Begin Compression
	if (m_useNormalCompress) {
		LRESULT err = ICCompressBegin(hIC, m_inputBitmapInfo, m_outputBitmapInfo);
		if (err != ICERR_OK) {
			PLOG_ERROR("Unable to begin encoding: %s.", FormattedICCError(err).c_str());
			throw std::runtime_error(FormattedICCError(err));
		}

		DWORD size = ICCompressGetSize(hIC, m_inputBitmapInfo, m_outputBitmapInfo);
		m_bufferOutput.resize(size);
	} else {
		std::memset(&cv, 0, sizeof(COMPVARS));
		cv.cbSize = sizeof(COMPVARS);
		cv.dwFlags = ICMF_COMPVARS_VALID;
		cv.hic = hIC;
		cv.fccType = myInfo->icInfo2.fccType;
		cv.fccHandler = myInfo->icInfo2.fccHandler;
		cv.lpbiOut = m_outputBitmapInfo;
		cv.lKey = m_keyframeInterval;
		cv.lDataRate = m_bitrate;
		cv.lQ = m_quality;

		if (!ICSeqCompressFrameStart(&cv, m_inputBitmapInfo)) {
			PLOG_ERROR("Unable to begin encoding.");
			throw std::exception();
		}
	}

}

void VFW::Encoder::destroy(void* data) {
	delete static_cast<VFW::Encoder*>(data);
}

VFW::Encoder::~Encoder() {
	if (m_useNormalCompress) {
		ICCompressEnd(hIC);
	} else {
		ICSeqCompressFrameEnd(&cv);
		//ICCompressorFree(&cv);		
	}

	ICClose(hIC);
}

bool VFW::Encoder::encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	return static_cast<VFW::Encoder*>(data)->encode(frame, packet, received_packet);
}

bool VFW::Encoder::encode(struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	// Vertically invert Image for some reason.

	size_t maxX = (m_width * 4) / 16;
	if (maxX <= 0)
		maxX = 1;

	// xF xE xD xC xB xA x9 x8 x7 x6 x5 x4 x3 x2 x1 x0
	// A  B  G  R  A  B  G  R  A  B  G  R  A  B  G  R

	// Swizzle Mask (128-Bit swapping, could do higher but AMD decided to say fuck it)
	/*
		0xF  0xE  0xD  0xC
		0xB  0xA  0x9  0x8
		0x7  0x6  0x5  0x4
		0x3  0x2  0x1  0x0
	*/
	__m128i swizzle = _mm_set_epi8(
		0xF, 0xC, 0xD, 0xE, 
		0xB, 0x8, 0x9, 0xA, 
		0x7, 0x4, 0x5, 0x6, 
		0x3, 0x0, 0x1, 0x2);

	const size_t lineSize = m_width * 4;
	for (size_t y = 0; y < m_height; ++y) {
		__m128i* pIn = reinterpret_cast<__m128i*>(frame->data[0] + (y * lineSize));
		__m128i* pOut = reinterpret_cast<__m128i*>(m_bufferInput.data() + ((m_height - 1 - y) * lineSize));

		for (size_t x = 0; x < maxX; ++x) {
			__m128i in = _mm_loadu_si128(pIn + x);
			__m128i out = _mm_shuffle_epi8(in, swizzle);
			_mm_storeu_si128(pOut + x, out);
		}
	}

	//for (size_t y = 0; y < m_height; ++y) {

	//	uint8_t* lineOut = reinterpret_cast<uint8_t*>(m_bufferInput.data()) + (lineOutSize * (m_height - y - 1));

	//	for (size_t x = 0; x < m_width; ++x) {
	//		lineOut[x * 4] = lineIn[x * 4 + 2];
	//		lineOut[x * 4 + 1] = lineIn[x * 4 + 1];
	//		lineOut[x * 4 + 2] = lineIn[x * 4];
	//		lineOut[x * 4 + 3] = lineIn[x * 4 + 3];
	//	}

	//}

	bool isKeyframe = false;

	*received_packet = false;
	if (m_useNormalCompress) {
		DWORD dwFlags, cwCompFlags;
		bool makeKeyframe = (m_keyframeInterval > 0) && ((frame->pts % m_keyframeInterval) == 0);

		LRESULT err = ICCompress(hIC,
			makeKeyframe ? ICCOMPRESS_KEYFRAME : 0,
			&(m_outputBitmapInfo->bmiHeader), m_bufferOutput.data(),
			&(m_inputBitmapInfo->bmiHeader), m_bufferInput.data(),
			&dwFlags, &cwCompFlags,
			(LONG)frame->pts,
			m_useBitrateFlag ? m_bitrate : 0,
			m_useQualityFlag ? m_quality : 0,
			!makeKeyframe && m_useTemporalFlag ? &(m_prevInputBitmapInfo->bmiHeader) : NULL,
			!makeKeyframe && m_useTemporalFlag ? m_bufferPrevInput.data() : NULL);
		if (err != ICERR_OK) {
			PLOG_ERROR("Unable to encode: %s.", FormattedICCError(err).c_str());
			return false;
		}

		// Swap Buffers
		m_bufferPrevInput.swap(m_bufferInput);

		// Store some information we need right now.
		packet->size = m_outputBitmapInfo->bmiHeader.biSizeImage;
		isKeyframe = (cwCompFlags & AVIIF_KEYFRAME) != 0;
	} else {
		BOOL keyframe; LONG plSize = (LONG)m_bufferInput.size();
		LPVOID fptr = ICSeqCompressFrame(
			&cv,
			0,
			reinterpret_cast<LPVOID>(m_bufferInput.data()),
			&keyframe,
			&plSize);
		if (fptr == NULL) {
			PLOG_ERROR("Unable to encode.");
			return false;
		}

		if (plSize > m_bufferOutput.size())
			m_bufferOutput.resize(plSize);
		std::memcpy(m_bufferOutput.data(), fptr, plSize);
		packet->size = plSize;
		isKeyframe = keyframe != 0;
		return true;
	}

	*received_packet = true;
	packet->type = OBS_ENCODER_VIDEO;
	packet->data = reinterpret_cast<uint8_t*>(m_bufferOutput.data());
	packet->keyframe = isKeyframe;
	packet->pts = frame->pts;
	packet->dts = frame->pts - 2;

	return true;
}

bool VFW::Encoder::update(void *data, obs_data_t *settings) {
	return static_cast<VFW::Encoder*>(data)->update(settings);
}

bool VFW::Encoder::update(obs_data_t* settings) {
	UNREFERENCED_PARAMETER(settings);
	return false;
}

bool VFW::Encoder::get_extra_data(void *data, uint8_t **extra_data, size_t *size) {
	return static_cast<VFW::Encoder*>(data)->get_extra_data(extra_data, size);
}

bool VFW::Encoder::get_extra_data(uint8_t** extra_data, size_t* size) {
	UNREFERENCED_PARAMETER(extra_data);
	UNREFERENCED_PARAMETER(size);
	return false;
}

bool VFW::Encoder::get_sei_data(void *data, uint8_t **sei_data, size_t *size) {
	return static_cast<VFW::Encoder*>(data)->get_sei_data(sei_data, size);
}

bool VFW::Encoder::get_sei_data(uint8_t** sei_data, size_t* size) {
	UNREFERENCED_PARAMETER(sei_data);
	UNREFERENCED_PARAMETER(size);
	return false;
}

void VFW::Encoder::get_video_info(void *data, struct video_scale_info *info) {
	return static_cast<VFW::Encoder*>(data)->get_video_info(info);
}

void VFW::Encoder::get_video_info(struct video_scale_info *info) {
	info->format = VIDEO_FORMAT_RGBA;
	info->range = VIDEO_RANGE_FULL;
	info->colorspace = VIDEO_CS_DEFAULT;
}
