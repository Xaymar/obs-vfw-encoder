#include "enc-vfw.h"
#include "libobs/obs-encoder.h"

#include <chrono>
#include <list>
#include <tuple>
#include <vector>
#include <map>
#include <sstream>
#include <emmintrin.h>
#include <sstream>

std::map<std::string, VFW::Info*> _IdToInfo;

#define snprintf sprintf_s
static const size_t preprocessthreads = 4;

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

std::map<uint64_t, std::tuple<uint8_t, uint8_t, uint8_t>> mpeg2hertz;
const uint64_t mpeg2hertz_mult = 0xFFFFFFFF;

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
	// Initialize MPEG-2 Rewriting Map
	std::pair<uint8_t, double_t> native_hertz[] = {
		std::make_pair(8, 60.0),
		std::make_pair(7, (60000.0 / 1001.0)),
		std::make_pair(6, 50.0),
		std::make_pair(5, 30.0),
		std::make_pair(4, (30000.0 / 1001.0)),
		std::make_pair(3, 25.0),
		std::make_pair(2, 24.0),
		std::make_pair(1, (24000.0 / 1001.0)),
	};
	for (auto kv : native_hertz) {
		PLOG_DEBUG("(MPEG-2 Rewrite) Native Framerate: %f", kv.second);
		mpeg2hertz.insert(std::make_pair(uint64_t(kv.second * mpeg2hertz_mult),
			std::make_tuple(kv.first, 0, 0)));

	}
	for (auto kv : native_hertz) {
		std::stringstream buf;
		for (uint8_t num = 0; num < (1 << 2); num++) {
			for (uint8_t den = 0; den < (1 << 5); den++) {
				if (num == den)
					continue; // Don't need the 1:1 ones >_>

				double_t fps = kv.second * (double_t(num + 1) / double_t(den + 1));
				uint64_t key = uint64_t(fps * mpeg2hertz_mult);
				if (mpeg2hertz.count(key)) {
					continue; // Duplicate.
				}

				mpeg2hertz.insert(std::make_pair(key, std::make_tuple(kv.first, num, den)));
				buf << fps << " (" << kv.second << " * " << num + 1 << " / " << den + 1 << "), ";
			}
		}
		PLOG_DEBUG("(MPEG-2 Rewrite) Extended Framerates for native %f: %s", kv.second, buf.str().c_str());
	}

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
				info->obsInfo.get_extra_data = VFW::Encoder::get_extra_data;
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
	obs_data_set_default_int(settings, PROP_INTERVAL_TYPE, 0);
	obs_data_set_default_double(settings, PROP_KEYFRAME_INTERVAL, 1.0);
	obs_data_set_default_int(settings, PROP_KEYFRAME_INTERVAL2, 30);
	obs_data_set_default_bool(settings, PROP_FORCE_KEYFRAMES, true);
	obs_data_set_default_string(settings, PROP_MODE, PROP_MODE_SEQUENTIAL);
	obs_data_set_default_string(settings, PROP_ICMODE, PROP_ICMODE_FASTCOMPRESS);
	obs_data_set_default_int(settings, PROP_LATENCY, 3);
}

obs_properties_t* VFW::Encoder::get_properties(void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(data);

	obs_properties_t* pr = obs_properties_create();
	obs_properties_set_param(pr, data, nullptr);
	obs_property_t* p;

	p = obs_properties_add_button(pr, PROP_CONFIGURE, "Configure", cb_configure);
	obs_property_set_visible(p, info->hasConfigure);

	p = obs_properties_add_int_slider(pr, PROP_BITRATE, "Bitrate", 0, 1000000, 1);
	obs_property_set_visible(p, ((info->icInfo2.dwFlags & VIDCF_CRUNCH) != 0));

	p = obs_properties_add_float_slider(pr, PROP_QUALITY, "Quality", 1, 100, 0.01);
	obs_property_set_visible(p, ((info->icInfo2.dwFlags & VIDCF_QUALITY) != 0));

	p = obs_properties_add_list(pr, PROP_INTERVAL_TYPE, "Interval Type", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, "Seconds", 0);
	obs_property_list_add_int(p, "Frames", 1);
	obs_property_set_modified_callback(p, cb_modified);

	p = obs_properties_add_float(pr, PROP_KEYFRAME_INTERVAL, "Keyframe Interval", 0.00, 30.00, 0.01);
	p = obs_properties_add_int(pr, PROP_KEYFRAME_INTERVAL2, "Keyframe Interval", 0, 300, 1);
	p = obs_properties_add_bool(pr, PROP_FORCE_KEYFRAMES, "Force Keyframes");

	p = obs_properties_add_list(pr, PROP_MODE, "Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Normal", PROP_MODE_NORMAL);
	if ((info->icInfo2.dwFlags & VIDCF_TEMPORAL) != 0)
		obs_property_list_add_string(p, "Temporal", PROP_MODE_TEMPORAL);
	obs_property_list_add_string(p, "Sequential", PROP_MODE_SEQUENTIAL);

	p = obs_properties_add_list(pr, PROP_ICMODE, "Compress Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Normal", PROP_ICMODE_COMPRESS);
	obs_property_list_add_string(p, "Fast", PROP_ICMODE_FASTCOMPRESS);

	p = obs_properties_add_int_slider(pr, PROP_LATENCY, "Frame Latency", 0, 10, 1);

	p = obs_properties_add_button(pr, PROP_ABOUT, "About", cb_about);
	obs_property_set_visible(p, info->hasAbout);

	return pr;
}

bool VFW::Encoder::cb_configure(obs_properties_t *pr, obs_property_t *p, void *data) {
	UNREFERENCED_PARAMETER(pr);
	UNREFERENCED_PARAMETER(p);
	UNREFERENCED_PARAMETER(data);

	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));

	HIC hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_COMPRESS);
	if (hIC == 0)
		hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	if (hIC == 0)
		return false;

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
	HIC hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_COMPRESS);
	if (hIC == 0)
		hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	if (hIC == 0)
		return false;
	ICAbout(hIC, GetDesktopWindow());
	ICClose(hIC);

	return false;
}

bool VFW::Encoder::cb_modified(obs_properties_t *pr, obs_property_t *, obs_data_t *data) {
	int64_t v = obs_data_get_int(data, PROP_INTERVAL_TYPE);
	obs_property_set_visible(obs_properties_get(pr, PROP_KEYFRAME_INTERVAL), v == 0);
	obs_property_set_visible(obs_properties_get(pr, PROP_KEYFRAME_INTERVAL2), v == 1);
	return true;
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
	switch (obs_data_get_int(settings, PROP_INTERVAL_TYPE)) {
		case 0:
			m_keyframeInterval = max(uint32_t(
				factor * obs_data_get_double(settings, PROP_KEYFRAME_INTERVAL)
			), 0);
			break;
		case 1:
			m_keyframeInterval =
				(uint32_t)obs_data_get_int(settings, PROP_KEYFRAME_INTERVAL2);
			break;
	}
	m_forceKeyframes = obs_data_get_bool(settings, PROP_FORCE_KEYFRAMES);
	m_bitrate = uint32_t(obs_data_get_int(settings, PROP_BITRATE));
	m_quality = uint32_t(obs_data_get_double(settings, PROP_QUALITY) * 100);
	m_latency = uint32_t(obs_data_get_int(settings, PROP_LATENCY));
	m_maxQueueSize = (m_latency + 1) * 2;

	PLOG_INFO("<%s> Initializing... ("
		"Resolution: %" PRIu32 "x%" PRIu32 ", "
		"Frame Rate: %" PRIu32 "/%" PRIu32 " = %0.1f FPS, "
		"Bitrate: %" PRIu32 ", "
		"Quality: %0.2f%%, "
		"Keyframe Interval: %" PRIu32 " (%s), "
		"Mode: %s, "
		"Compress Mode: %s"
		")",
		myInfo->Name.c_str(),
		m_width, m_height,
		m_fpsNum, m_fpsDen, (double_t)m_fpsNum / (double_t)m_fpsDen,
		m_bitrate, m_quality,
		m_keyframeInterval, m_forceKeyframes ? "Enforced" : "Standard",
		obs_data_get_string(settings, PROP_MODE),
		obs_data_get_string(settings, PROP_ICMODE));

	UINT mainIC = ICMODE_FASTCOMPRESS;
	const char* mainICs = "Fast";
	UINT backupIC = ICMODE_COMPRESS;
	const char* backupICs = "Normal";

	if (strcmp(obs_data_get_string(settings, PROP_ICMODE), PROP_ICMODE_COMPRESS) == 0) {
		std::swap(mainIC, backupIC);
		std::swap(mainICs, backupICs);
	}

	hIC = ICOpen(myInfo->icInfo.fccType, myInfo->icInfo.fccHandler, mainIC);
	if (hIC == 0) {
		PLOG_WARNING(
			"<%s> Failed to initialize with %s compression mode, "
			"falling back to %s compression mode...",
			myInfo->Name.c_str(), mainICs, backupICs);
		hIC = ICOpen(myInfo->icInfo.fccType, myInfo->icInfo.fccHandler, backupIC);
		if (hIC == 0) {
			PLOG_ERROR("<%s> Failed to initialize.",
				myInfo->Name.c_str());
			throw std::exception();
		}
	} else {
		PLOG_DEBUG("<%s> Initialized with %s compression mode, setting up...",
			myInfo->Name.c_str(), mainICs);
	}

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
		err = ICSetState(hIC, myInfo->stateInfo.data(), myInfo->stateInfo.size());
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
	size_t alignedWidth = (m_width / 16 + 1) * 16;
	const size_t bufferSize = alignedWidth * m_height * 4;
	m_bufferInput.resize(bufferSize);
	m_bufferPrevInput.resize(bufferSize);

	// Begin Compression
	if (m_useNormalCompress) {
		err = ICCompressBegin(hIC, m_inputBitmapInfo, m_outputBitmapInfo);
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

	// Thread stuff. These can't fail in most situations.
	m_threadShutdown = false;
	m_preProcessData.worker = std::thread(threadMain, this, 0);
	m_encodeData.worker = std::thread(threadMain, this, 1);
	m_postProcessData.worker = std::thread(threadMain, this, 2);

	PLOG_INFO("<%s> Started.",
		myInfo->Name.c_str());
}

void VFW::Encoder::destroy(void* data) {
	delete static_cast<VFW::Encoder*>(data);
}

VFW::Encoder::~Encoder() {
	m_threadShutdown = true;
	m_preProcessData.cv.notify_all();
	m_preProcessData.worker.join();
	m_encodeData.cv.notify_all();
	m_encodeData.worker.join();
	m_postProcessData.cv.notify_all();
	m_postProcessData.worker.join();

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
	auto tbegin = std::chrono::high_resolution_clock::now();

	namespace sc = std::chrono;
	using schrc = std::chrono::high_resolution_clock;

	bool submittedFrame = false;
	long long maxTime = size_t((double_t(m_fpsNum) / double_t(m_fpsDen)) * 1000000000);
	while (((*received_packet == false) || (submittedFrame == false))
		&& (sc::nanoseconds((schrc::now() - tbegin)).count() < maxTime)) {
		// Submit frame to PreProcessor
		if (!submittedFrame) {
			std::unique_lock<std::mutex> ulock(m_preProcessData.lock);
			std::unique_lock<std::mutex> elock(m_encodeData.lock);
			std::unique_lock<std::mutex> plock(m_postProcessData.lock);
			if ((m_preProcessData.data.size() < m_maxQueueSize)
				&& (m_encodeData.data.size() < m_maxQueueSize)
				&& (m_postProcessData.data.size() < m_maxQueueSize)) {
				m_preProcessData.data.push(std::make_tuple(
					std::make_shared<std::vector<char>>(frame->data[0], frame->data[0] + (frame->linesize[0] * this->m_height)),
					frame->pts,
					false));
				submittedFrame = true;
				m_preProcessData.cv.notify_all();
			}
		}

		if (!*received_packet) {
			std::unique_lock<std::mutex> ulock(m_finalPacketsLock);
			if (m_finalPackets.size() > m_latency) {
				auto front = m_finalPackets.front();
				m_donotuse_datastor = std::get<0>(front);
				packet->type = OBS_ENCODER_VIDEO;
				packet->data = reinterpret_cast<uint8_t*>(m_donotuse_datastor->data());
				packet->size = m_donotuse_datastor->size();
				packet->pts = packet->dts = std::get<1>(front);
				packet->keyframe = std::get<2>(front);
				*received_packet = true;
				m_finalPackets.pop();
			#ifdef _DEBUG
				PLOG_DEBUG("<%s> PTS: %" PRIu32 ", DTS: %" PRIu32 ", Keyframe: %s, Size: %" PRIu32,
					myInfo->Name.c_str(), packet->pts, packet->dts, packet->keyframe ? "Yes" : "No", packet->size);
			#endif
			}
		}

		std::this_thread::sleep_for(sc::milliseconds(1));
	}

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
	extra_data = nullptr;
	size = 0;
	return true;
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
	info->format = VIDEO_FORMAT_BGRA;
	info->range = VIDEO_RANGE_FULL;
	info->colorspace = VIDEO_CS_709;
}

void VFW::Encoder::threadMain(void *data, int32_t flag) {
	reinterpret_cast<Encoder*>(data)->threadLocal(flag);
}

void VFW::Encoder::threadLocal(int32_t flag) {
	thread_data* td = &m_preProcessData;
	if (flag == 0) {
		td = &m_preProcessData;
	} else if (flag == 1) {
		td = &m_encodeData;
	} else if (flag == 2) {
		td = &m_postProcessData;
	}

	std::unique_lock<std::mutex> ulock(td->lock);
	while (!m_threadShutdown) {
		td->cv.wait(ulock, [this, td] {
			return m_threadShutdown || td->data.size() > 0;
		});
		if (m_threadShutdown)
			break;

		if (flag == 0) {
			preProcessLocal(ulock);
		} else if (flag == 1) {
			encodeLocal(ulock);
		} else if (flag == 2) {
			postProcessLocal(ulock);
		}
	}
}

void VFW::Encoder::preProcessLocal(std::unique_lock<std::mutex>& ul) {
#ifdef _DEBUG
	auto total_start = std::chrono::high_resolution_clock::now();
#endif

	auto kv = m_preProcessData.data.front();
	ul.unlock();

#ifdef _DEBUG
	auto invert_start = std::chrono::high_resolution_clock::now();
#endif
	std::shared_ptr<std::vector<char>> inbuf = std::get<0>(kv);
	std::shared_ptr<std::vector<char>> outbuf = inbuf;// std::make_shared<std::vector<char>>(inbuf->size());

	size_t halfHeight = m_height / 2;
	size_t lineSize = inbuf->size() / m_height;
	std::vector<char> tempBuf(lineSize);
	for (size_t line = 0; line < halfHeight; line++) {
		size_t front = line * lineSize;
		size_t back = (m_height - line - 1) * lineSize;

		std::memcpy(tempBuf.data(), inbuf->data() + front, lineSize);
		std::memcpy(outbuf->data() + front, inbuf->data() + back, lineSize);
		std::memcpy(outbuf->data() + back, tempBuf.data(), lineSize);
	}
#ifdef _DEBUG
	auto invert_end = std::chrono::high_resolution_clock::now();
#endif

//#ifdef _DEBUG
//	auto wait_start = std::chrono::high_resolution_clock::now();
//#endif
//	// Do not fill queue if it is > latency.
//	size_t queueSize = m_maxQueueSize;
//	while (queueSize >= m_maxQueueSize) {
//		{
//			std::unique_lock<std::mutex> elock(m_encodeData.lock);
//			queueSize = m_encodeData.data.size();
//		}
//
//		if (queueSize >= m_maxQueueSize)
//			std::this_thread::sleep_for(std::chrono::milliseconds(1));
//	}
//#ifdef _DEBUG
//	auto wait_end = std::chrono::high_resolution_clock::now();
//#endif

#ifdef _DEBUG
	auto queue_start = std::chrono::high_resolution_clock::now();
#endif
	{
		std::unique_lock<std::mutex> plock(m_preProcessData.lock);
		std::unique_lock<std::mutex> elock(m_encodeData.lock);
		m_encodeData.data.push(std::make_tuple(outbuf, std::get<1>(kv), std::get<2>(kv)));
		m_encodeData.cv.notify_all();
		m_preProcessData.data.pop();
	}
#ifdef _DEBUG
	auto queue_end = std::chrono::high_resolution_clock::now();
#endif

	ul.lock();
#ifdef _DEBUG
	auto total_end = std::chrono::high_resolution_clock::now();
#endif

#ifdef _DEBUG
	auto time_total = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start);
	auto time_invert = std::chrono::duration_cast<std::chrono::nanoseconds>(invert_end - invert_start);
	auto time_wait = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start);
	auto time_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(queue_end - queue_start);
#endif
#ifdef _DEBUG
	PLOG_INFO("[Thread PrePro] Frame %" PRId64 ": "
		"Total: %" PRId64 "ns, "
		"Invert: %" PRId64 "ns, "
		"Wait: %" PRId64 "ns, "
		"Queue: %" PRId64 "ns",
		std::get<1>(kv),
		time_total.count(),
		time_invert.count(),
		time_wait.count(),
		time_queue.count());
#endif
}

void VFW::Encoder::encodeLocal(std::unique_lock<std::mutex>& ul) {
#ifdef _DEBUG
	auto total_start = std::chrono::high_resolution_clock::now();
#endif

	auto kv = m_encodeData.data.front();
	ul.unlock();

#ifdef _DEBUG
	auto encode_start = std::chrono::high_resolution_clock::now();
#endif
	bool isKeyframe = false;
	bool makeKeyframe = (m_keyframeInterval > 0) && ((std::get<1>(kv) % m_keyframeInterval) == 0);
	std::shared_ptr<std::vector<char>> inbuf = std::get<0>(kv);
	std::shared_ptr<std::vector<char>> outbuf = std::make_shared<std::vector<char>>(m_bufferOutput.size());
	if (m_useNormalCompress) {
		DWORD dwFlags = 0, cwCompFlags = 0;
	#ifdef _DEBUG
		PLOG_DEBUG("<%s:Normal> PTS: %" PRIu32 ", Keyframe: %s", myInfo->Name.c_str(), std::get<1>(kv), makeKeyframe ? "Yes" : "No");
	#endif
		LRESULT err = ICCompress(hIC,
			makeKeyframe ? ICCOMPRESS_KEYFRAME : 0,
			&(m_outputBitmapInfo->bmiHeader), outbuf->data(),
			&(m_inputBitmapInfo->bmiHeader), inbuf->data(),
			&dwFlags, &cwCompFlags,
			(LONG)std::get<1>(kv),
			m_useBitrateFlag ? m_bitrate : 0,
			m_useQualityFlag ? m_quality : 0,
			!makeKeyframe && m_useTemporalFlag ? &(m_prevInputBitmapInfo->bmiHeader) : NULL,
			!makeKeyframe && m_useTemporalFlag ? m_bufferPrevInput.data() : NULL);
		if (err == ICERR_OK) {
			outbuf->resize(m_outputBitmapInfo->bmiHeader.biSizeImage);
			//std::memcpy(outbuf->data(), m_bufferOutput.data(), outbuf->size());

			isKeyframe = (cwCompFlags & AVIIF_KEYFRAME) != 0;

			// Swap Buffers
			m_bufferPrevInput.swap(m_bufferInput);

		#ifdef _DEBUG
			PLOG_DEBUG("<%s:Normal> PTS: %" PRIu32 ", Keyframe: %s, Size: %" PRIu32,
				myInfo->Name.c_str(), std::get<1>(kv), isKeyframe ? "Yes" : "No", outbuf->size());
		#endif
		} else {
			PLOG_ERROR("Unable to encode: %s.", FormattedICCError(err).c_str());
		}
	} else {
		BOOL keyframe; LONG plSize = (LONG)inbuf->size();
	#ifdef _DEBUG
		PLOG_DEBUG("<%s:Sequential> PTS: %" PRIu32 ", Keyframe: %s",
			myInfo->Name.c_str(), std::get<1>(kv), makeKeyframe ? "Yes" : "No");
	#endif
		LPVOID fptr = ICSeqCompressFrame(
			&cv,
			makeKeyframe ? 1 : 0,
			reinterpret_cast<LPVOID>(inbuf->data()),
			&keyframe,
			&plSize);
		if (fptr == NULL) {
			PLOG_ERROR("Unable to encode.");
		} else {
			outbuf->resize(plSize);
			std::memcpy(outbuf->data(), fptr, outbuf->size());
			isKeyframe = keyframe != 0;

		#ifdef _DEBUG
			PLOG_DEBUG("<%s:Sequential> PTS: %" PRIu32 ", Keyframe: %s, Size: %" PRIu32,
				myInfo->Name.c_str(), std::get<1>(kv), isKeyframe ? "Yes" : "No", outbuf->size());
		#endif
		}
	}

	isKeyframe = m_forceKeyframes ? makeKeyframe || isKeyframe : isKeyframe;
#ifdef _DEBUG
	auto encode_end = std::chrono::high_resolution_clock::now();
#endif

//#ifdef _DEBUG
//	auto wait_start = std::chrono::high_resolution_clock::now();
//#endif
//	// Do not fill queue if it is > latency.
//	size_t queueSize = m_maxQueueSize;
//	while (queueSize >= m_maxQueueSize) {
//		{
//			std::unique_lock<std::mutex> elock(m_postProcessData.lock);
//			queueSize = m_postProcessData.data.size();
//		}
//		std::this_thread::sleep_for(std::chrono::milliseconds(1));
//	}
//#ifdef _DEBUG
//	auto wait_end = std::chrono::high_resolution_clock::now();
//#endif

#ifdef _DEBUG
	auto queue_start = std::chrono::high_resolution_clock::now();
#endif
	{
		std::unique_lock<std::mutex> elock(m_encodeData.lock);
		std::unique_lock<std::mutex> plock(m_postProcessData.lock);
		m_postProcessData.data.push(std::make_tuple(outbuf, std::get<1>(kv), isKeyframe));
		m_postProcessData.cv.notify_all();
		m_encodeData.data.pop();
	}
#ifdef _DEBUG
	auto queue_end = std::chrono::high_resolution_clock::now();
#endif

	ul.lock();
#ifdef _DEBUG
	auto total_end = std::chrono::high_resolution_clock::now();
#endif

#ifdef _DEBUG
	auto time_total = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start);
	auto time_encode = std::chrono::duration_cast<std::chrono::nanoseconds>(encode_end - encode_start);
	auto time_wait = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start);
	auto time_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(queue_end - queue_start);
	PLOG_DEBUG("[Thread Encode] Frame %" PRId64 ": "
		"Total: %" PRId64 "ns, "
		"Encode: %" PRId64 "ns, "
		"Wait: %" PRId64 "ns, "
		"Queue: %" PRId64 "ns",
		std::get<1>(kv),
		time_total.count(),
		time_encode.count(),
		time_wait.count(),
		time_queue.count());
#endif
}

void MatroxM2VBitstreamFixer(std::shared_ptr<std::vector<char>>& ptr, std::pair<uint32_t, uint32_t> framerate) {
	// Matrox developers are idiots. Their MPEG-2 codec flags the content
	// as interlaced top-field top-displayed, but in reality there is a
	// progressive frame there. But that isn't the only issue.
	// They also have structures in the stream that are larger than the
	// standard allows for, or even invalid user data (all 0s). It's just
	// a big bunch of "How did this ever work?" ...

	// Find best match FPS
	double_t sourceHertz = double_t(framerate.first) / double_t(framerate.second);
	uint64_t sourceKey = uint64_t(sourceHertz * mpeg2hertz_mult);
	std::pair<uint64_t, std::tuple<uint8_t, uint8_t, uint8_t>> bestMatch;
	uint64_t bestMatchDiff = UINT64_MAX;
	for (auto kv : mpeg2hertz) {
		uint64_t diff = uint64_t(abs(int64_t(sourceKey) - int64_t(kv.first)));
		if (diff < bestMatchDiff) {
			bestMatch = kv;
			bestMatchDiff = diff;
		}
	}
#ifdef _DEBUG
	PLOG_DEBUG("(MPEG-2 Rewrite) Best Match for Content: %f (Diff: %llu idx: %i, extn: %i, extd: %i)",
		double_t(bestMatch.first / mpeg2hertz_mult),
		bestMatchDiff,
		std::get<0>(bestMatch.second),
		std::get<1>(bestMatch.second),
		std::get<2>(bestMatch.second));
#endif

	std::vector<char>* buffer = ptr.get();

	// Rewrite stream
	size_t streamPosition = 0, streamSize = buffer->size();
	while ((streamPosition < streamSize) && ((streamSize - streamPosition) >= 4)) {
		uint8_t blockId = (uint8_t)(*buffer)[streamPosition + 3];
		streamPosition += 4;

		switch (blockId) {
			case 0xB3: // Sequence Header
			{
			#ifdef _DEBUG
				PLOG_DEBUG("(MPEG-2 Rewrite) Sequence Header at %" PRIu64, streamPosition);
			#endif
				// Rewrite Framerate
				char b = (*buffer)[streamPosition + 3];
				char fpsflag = std::get<0>(bestMatch.second);
				(*buffer)[streamPosition + 3] = (b & 0xF0) + (fpsflag & 0x0F);
				streamPosition += 8;
				break;
			}
			case 0xB5:
			{
				//					streamPosition += 1;
				char type = ((*buffer)[streamPosition] & 0xF0) >> 4;
				switch (type) {
					case 0b0001:
					{ // Sequence Extension (Progressive, FPS, ChromaFormat possible)
					#ifdef _DEBUG
						PLOG_DEBUG("(MPEG-2 Rewrite) Sequence Extension at %" PRIu64, streamPosition);
					#endif
						(*buffer)[streamPosition + 1] |= 1 << 3; // Flag Progressive
						(*buffer)[streamPosition + 5] = // Rewrite FPS Ext
							((*buffer)[streamPosition + 5] & 0x80)
							| ((std::get<1>(bestMatch.second) & 0x3) << 5)
							| ((std::get<2>(bestMatch.second) & 0x1F));
						streamPosition += 6;
						break;
					}
					case 0b0010:
					{
					#ifdef _DEBUG
						PLOG_DEBUG("(MPEG-2 Rewrite) Sequence Display Extension at %" PRIu64, streamPosition);
					#endif
						if ((*buffer)[streamPosition] & 0b1) {
							streamPosition += 8;
						} else {
							streamPosition += 5;
						}
					}
					break;
					case 0b1000:
					{ // Chroma Stuff?
					#ifdef _DEBUG
						PLOG_DEBUG("(MPEG-2 Rewrite) Picture Coding Extension at %" PRIu64, streamPosition);
					#endif
						// Picture Structure
						(*buffer)[streamPosition + 2] |= 0x3; // Full Frame
						(*buffer)[streamPosition + 3] &= ~(1 << 7); // top field first
						(*buffer)[streamPosition + 3] &= ~(1 << 1); // repeat first field
						(*buffer)[streamPosition + 4] |= 1 << 7; // progressive
						if ((*buffer)[streamPosition + 4] & 0b1000000) {
							streamPosition += 7;
						} else {
							streamPosition += 5;
						}
						break;
					}
				#ifdef _DEBUG
					default:
						PLOG_DEBUG("(MPEG-2 Rewrite) Unknown Extension %" PRIx8 " at %" PRIu64, type, streamPosition);
						break;
					#endif
				}

				break;
			}
		}

		// Seek to a valid position.
		while ((streamPosition < buffer->size()) && ((buffer->size() - streamPosition) >= 4)
			&& (
			((*buffer)[streamPosition] != 0)
				|| ((*buffer)[streamPosition + 1] != 0)
				|| ((*buffer)[streamPosition + 2] != 1)
				)) {
			++streamPosition;
		}
	}
}

void VFW::Encoder::postProcessLocal(std::unique_lock<std::mutex>& ul) {
#ifdef _DEBUG
	auto total_start = std::chrono::high_resolution_clock::now();
#endif

	auto kv = m_postProcessData.data.front();
	ul.unlock();

#ifdef _DEBUG
	auto bitstream_start = std::chrono::high_resolution_clock::now();
#endif
	if ((myInfo->Id == "mvcVfwMpeg2-mmes")
		|| (myInfo->Id == "mvcVfwMpeg2Alpha-m704")
		|| (myInfo->Id == "mvcVfwMpeg2HD-m701")
		|| (myInfo->Id == "mvcVfwMpeg2Alpha-m705")) {
		MatroxM2VBitstreamFixer(std::get<0>(kv), std::make_pair(m_fpsNum, m_fpsDen));
	}
#ifdef _DEBUG
	auto bitstream_end = std::chrono::high_resolution_clock::now();
#endif

//#ifdef _DEBUG
//	auto wait_start = std::chrono::high_resolution_clock::now();
//#endif
//	// Do not fill queue if it is > latency.
//	size_t queueSize = m_maxQueueSize;
//	while (queueSize >= m_maxQueueSize) {
//		{
//			std::unique_lock<std::mutex> flock(m_finalPacketsLock);
//			queueSize = m_finalPackets.size();
//		}
//		std::this_thread::sleep_for(std::chrono::milliseconds(1));
//	}
//#ifdef _DEBUG
//	auto wait_end = std::chrono::high_resolution_clock::now();
//#endif

#ifdef _DEBUG
	auto queue_start = std::chrono::high_resolution_clock::now();
#endif
	{
		std::unique_lock<std::mutex> plock(m_postProcessData.lock);
		std::unique_lock<std::mutex> flock(m_finalPacketsLock);
		m_finalPackets.push(kv);
		m_postProcessData.data.pop();
	}
#ifdef _DEBUG
	auto queue_end = std::chrono::high_resolution_clock::now();
#endif

	ul.lock();
#ifdef _DEBUG
	auto total_end = std::chrono::high_resolution_clock::now();
#endif

#ifdef _DEBUG
	auto time_total = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start);
	auto time_bitstream = std::chrono::duration_cast<std::chrono::nanoseconds>(bitstream_end - bitstream_start);
	auto time_wait = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start);
	auto time_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(queue_end - queue_start);
	PLOG_INFO("[Thread PostPr] Frame %" PRId64 ": "
		"Total: %" PRId64 "ns, "
		"Bitstream: %" PRId64 "ns, "
		"Wait: %" PRId64 "ns, "
		"Queue: %" PRId64 "ns",
		std::get<1>(kv),
		time_total.count(),
		time_bitstream.count(),
		time_wait.count(),
		time_queue.count());
#endif
}
