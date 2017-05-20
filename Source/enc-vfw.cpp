#include "enc-vfw.h"
#include "libobs/obs-encoder.h"

#include <list>
#include <vector>
#include <map>

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

std::string FormattedICCError(DWORD error) {
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
	for (size_t i = 0; ICInfo(fccType, i, &icinfo); i++) {
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

				PLOG_INFO("Registering '%s' (Id: %s, FourCC1: %s, FourCC2: %s, Codec: %s, Driver: '%s')",
					info->Name.c_str(),
					info->Id.c_str(),
					info->FourCC.c_str(),
					info->FourCC2.c_str(),
					info->obsInfo.codec,
					info->Path.c_str());

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
	obs_data_set_default_int(settings, PROP_BITRATE, 2000);
	obs_data_set_default_double(settings, PROP_QUALITY, 90.0);
	obs_data_set_default_double(settings, PROP_KEYFRAME_INTERVAL, 2.0);
}

obs_properties_t* VFW::Encoder::get_properties(void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(data);
	
	obs_properties_t* pr = obs_properties_create();
	obs_properties_set_param(pr, data, nullptr);
	obs_property_t* p;

	p = obs_properties_add_int(pr, PROP_BITRATE, "Bitrate", 1, 1000000, 1);
	p = obs_properties_add_float_slider(pr, PROP_QUALITY, "Quality", 1, 100, 0.01);
	p = obs_properties_add_float(pr, PROP_KEYFRAME_INTERVAL, "Keyframe Interval", 0.1, 30.00, 0.1);
	p = obs_properties_add_button(pr, PROP_CONFIGURE, "Configure", cb_configure);
	p = obs_properties_add_button(pr, PROP_ABOUT, "About", cb_about);

	return pr;
}

bool VFW::Encoder::cb_configure(obs_properties_t *pr, obs_property_t *p, void *data) {
	UNREFERENCED_PARAMETER(pr);
	UNREFERENCED_PARAMETER(p);
	UNREFERENCED_PARAMETER(data);

	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));

	HIC hIC = ICOpen(info->icInfo.fccType, info->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	ICConfigure(hIC, GetDesktopWindow());
	ICClose(hIC);

	return false;
}

bool VFW::Encoder::cb_about(obs_properties_t *pr, obs_property_t *p, void *data) {
	UNREFERENCED_PARAMETER(pr);
	UNREFERENCED_PARAMETER(p);
	UNREFERENCED_PARAMETER(data);

	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));

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
	width = obs_encoder_get_width(encoder);	height = obs_encoder_get_height(encoder);
	fpsNum = voi->fps_num;	fpsDen = voi->fps_den;
	double_t factor = double_t(fpsNum) / double_t(fpsDen);
	userKeyframeInterval = max(uint32_t(factor * obs_data_get_double(settings, PROP_KEYFRAME_INTERVAL)), 1);
	userBitrate = obs_data_get_int(settings, PROP_BITRATE);
	userQuality = uint32_t(obs_data_get_double(settings, PROP_QUALITY) * 100);

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
		width, height, fpsNum, fpsDen);
	PLOG_DEBUG("Using Bitrate %" PRIu32 " kbit, Quality %" PRIu32 ".%02" PRIu32 "%%, Keyframe Interval %" PRIu32,
		userBitrate,
		userQuality / 100, userQuality % 100,
		userKeyframeInterval);

#pragma region Get Bitmap Information
	vbiInput.resize(sizeof(BITMAPINFOHEADER));
	std::memset(vbiInput.data(), 0, vbiInput.size());
	biInput = reinterpret_cast<BITMAPINFO*>(vbiInput.data());
	biInput->bmiHeader.biSize = vbiInput.size();
	biInput->bmiHeader.biWidth = width;
	biInput->bmiHeader.biHeight = height;
	biInput->bmiHeader.biPlanes = 1;
	biInput->bmiHeader.biBitCount = 32;
	biInput->bmiHeader.biCompression = BI_RGB;
	biInput->bmiHeader.biSizeImage = width * height * (biInput->bmiHeader.biBitCount / 8) * biInput->bmiHeader.biPlanes;

	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, (DWORD_PTR)biInput, NULL);
	if (err <= 0) {
		PLOG_ERROR("Unable to retrieve format information size: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}

	vbiOutput.resize(err);
	std::memset(vbiOutput.data(), 0, vbiOutput.size());
	biOutput = (BITMAPINFO*)vbiOutput.data();
	biOutput->bmiHeader.biSize = vbiOutput.size();
	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, (DWORD_PTR)biInput, (DWORD_PTR)biOutput);
	if (err != ICERR_OK) {
		PLOG_ERROR("Unable to retrieve format information: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}
#pragma endregion Get Bitmap Information

	std::memset(&cv, 0, sizeof(COMPVARS));
	cv.cbSize = sizeof(COMPVARS);
	cv.dwFlags = ICMF_COMPVARS_VALID;
	cv.hic = hIC;
	cv.fccType = myInfo->icInfo2.fccType;
	cv.fccHandler = myInfo->icInfo2.fccHandler;
	cv.lpbiOut = biOutput;
	cv.lKey = userKeyframeInterval;
	cv.lDataRate = userBitrate;
	cv.lQ = userQuality;

	if (!ICSeqCompressFrameStart(&cv, biInput)) {
		PLOG_ERROR("Unable to begin encoding.");
		throw std::exception();
	}

	inBuffer.resize(width * height * 4);
	outBuffer.resize(width * height * 4);
}

void VFW::Encoder::destroy(void* data) {
	delete static_cast<VFW::Encoder*>(data);
}

VFW::Encoder::~Encoder() {
	ICSeqCompressFrameEnd(&cv);
	ICClose(hIC);
}

bool VFW::Encoder::encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	return static_cast<VFW::Encoder*>(data)->encode(frame, packet, received_packet);
}

bool VFW::Encoder::encode(struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	DWORD dwCkID, cwCompFlags;

	// Vertically invert Image for some reason.
	const size_t ysize = height;
	for (size_t y = 0; y < height; y++) {
		uint8_t offset = frame->linesize[0] * y;
		uint8_t target = width * 4 * (ysize - y - 1);

		uint8_t* in = reinterpret_cast<uint8_t*>(frame->data[0]) + (frame->linesize[0] * y);
		uint8_t* out = reinterpret_cast<uint8_t*>(inBuffer.data()) + (width * 4 * (ysize - y - 1));
		std::memcpy(out, in, width * 4);
	}

	BOOL keyframe; LONG plSize = outBuffer.size();
	LPVOID fptr = ICSeqCompressFrame(
		&cv,
		0,
		reinterpret_cast<LPVOID>(inBuffer.data()),
		&keyframe,
		&plSize);
	if (fptr != NULL) {
		*received_packet = true;
		packet->keyframe = keyframe;
		packet->dts = packet->pts = frame->pts;
		packet->size = plSize;
		packet->data = reinterpret_cast<uint8_t*>(fptr);
		packet->type = OBS_ENCODER_VIDEO;
		return true;
	} else {
		PLOG_ERROR("Unable to encode.");
		return false;
	}
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
	info->format = VIDEO_FORMAT_BGRX;
}
