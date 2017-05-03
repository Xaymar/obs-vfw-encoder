#include "enc-vfw.h"
#include "libobs/obs-encoder.h"

#include <list>
#include <vector>
#include <map>

std::map<std::string, VFW::Info*> _IdToInfo;

#define snprintf sprintf_s

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
		HIC hIC = ICOpen(icinfo.fccType, icinfo.fccHandler, ICMODE_FASTCOMPRESS);
		if (hIC) {
			ICINFO icinfo2;
			if (ICGetInfo(hIC, &icinfo2, sizeof(icinfo2))) {
				std::vector<char> idBuf(64);
				snprintf(idBuf.data(), idBuf.size(), "%ls", icinfo2.szName);
				std::vector<char> nameBuf(1024);
				snprintf(nameBuf.data(), nameBuf.size(), "%ls (" PLUGIN_NAME ")", icinfo2.szDescription);
				std::vector<char> pathBuf(1024);
				snprintf(pathBuf.data(), pathBuf.size(), "%ls (" PLUGIN_NAME ")", icinfo2.szDriver);

				// Track
				VFW::Info* info = new VFW::Info();
				info->Id = std::string(idBuf.data());
				info->Name = std::string(nameBuf.data());
				info->Path = std::string(pathBuf.data());
				std::memcpy(&info->icInfo, &icinfo, sizeof(ICINFO));
				std::memcpy(&info->icInfo2, &icinfo2, sizeof(ICINFO));
				info->index = i;
				info->FourCC = FourCCFromInt32(info->icInfo2.fccHandler);

				// Register
				std::memset(&info->obsInfo, 0, sizeof(obs_encoder_info));
				info->obsInfo.id = info->Id.data();
				info->obsInfo.type = OBS_ENCODER_VIDEO;
				info->obsInfo.codec = info->FourCC.c_str();
				info->obsInfo.type_data = info; // circular reference but whatever, it's not reference counted
				info->obsInfo.get_name = VFW::Encoder::get_name;
				info->obsInfo.create = VFW::Encoder::create;
				info->obsInfo.destroy = VFW::Encoder::destroy;
				info->obsInfo.encode = VFW::Encoder::encode;
				info->obsInfo.get_properties = VFW::Encoder::get_properties;
				info->obsInfo.update = VFW::Encoder::update;
				info->obsInfo.get_extra_data = VFW::Encoder::get_extra_data;
				info->obsInfo.get_sei_data = VFW::Encoder::get_sei_data;
				info->obsInfo.get_video_info = VFW::Encoder::get_video_info;

				PLOG_INFO("Registering: %s %s",
					info->Id.c_str(),
					info->Name.c_str());

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
	fpsNum = voi->fps_num;	fpsDen = voi->fps_den;
	kfinterval = uint32_t(double_t(fpsDen) / double_t(fpsNum) * obs_data_get_double(settings, PROP_KEYFRAME_INTERVAL));
	width = obs_encoder_get_width(encoder);	height = obs_encoder_get_height(encoder);

	PLOG_DEBUG("Initializing at %" PRIu32 "x%" PRIu32 " with %" PRIu32 "/%" PRIu32 " FPS.",
		width, height, fpsNum, fpsDen);

	hIC = ICOpen(myInfo->icInfo.fccType, myInfo->icInfo.fccHandler, ICMODE_FASTCOMPRESS);
	if (!hIC) {
		PLOG_ERROR("Failed to create '%s' VFW encoder.",
			myInfo->Name.c_str());
		throw std::exception();
	} else {
		PLOG_INFO("Created '%s' VFW encoder.",
			myInfo->Name.c_str());
	}

	#pragma region Get Bitmap Information
	vbiInput.resize(sizeof(BITMAPINFO));
	biInput = reinterpret_cast<BITMAPINFO*>(vbiInput.data());
	biInput->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	biInput->bmiHeader.biWidth = width;
	biInput->bmiHeader.biHeight = height;
	biInput->bmiHeader.biPlanes = 1;
	biInput->bmiHeader.biBitCount = 32;
	biInput->bmiHeader.biCompression = BI_RGB;
	biInput->bmiHeader.biSizeImage = width * height * 4;
	biInput->bmiHeader.biXPelsPerMeter = 1;
	biInput->bmiHeader.biYPelsPerMeter = 1;
	biInput->bmiHeader.biClrUsed = 0;
	biInput->bmiHeader.biClrImportant = 0;

	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, reinterpret_cast<DWORD_PTR>(biInput), NULL);
	if (err <= 0) {
		PLOG_ERROR("Unable to retrieve format information size: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}

	vbiOutput.resize(err);
	biOutput = reinterpret_cast<BITMAPINFO*>(vbiOutput.data());
	biOutput->bmiHeader.biSize = err;
	err = ICSendMessage(hIC, ICM_COMPRESS_GET_FORMAT, reinterpret_cast<DWORD_PTR>(biInput), reinterpret_cast<DWORD_PTR>(biOutput));
	if (err != ICERR_OK) {
		PLOG_ERROR("Unable to retrieve format information: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}
	#pragma endregion Get Bitmap Information

	err = ICSendMessage(hIC, ICM_COMPRESS_GET_SIZE, reinterpret_cast<DWORD_PTR>(biInput), reinterpret_cast<DWORD_PTR>(biOutput));
	if (err <= 0) {
		PLOG_ERROR("Unable to retrieve expected buffer size: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}

	frameBuffer.resize(err);
	oldFrameBuffer.resize(err);

	err = ICCompressBegin(hIC, &biInput, &biOutput);
	if (err != ICERR_OK) {
		PLOG_ERROR("Unable to begin encoding: %s.",
			FormattedICCError(err).c_str());
		throw std::exception();
	}
}

void VFW::Encoder::destroy(void* data) {
	delete static_cast<VFW::Encoder*>(data);
}

VFW::Encoder::~Encoder() {
	ICCompressEnd(hIC);
	ICClose(hIC);
}

bool VFW::Encoder::encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	return static_cast<VFW::Encoder*>(data)->encode(frame, packet, received_packet);
}

bool VFW::Encoder::encode(struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	bool keyframe = (frame->pts % kfinterval) == 0;
	LRESULT err = ICCompress(hIC,
		keyframe ? ICCOMPRESS_KEYFRAME : 0,
		&(biOutput->bmiHeader),
		reinterpret_cast<LPVOID>(frameBuffer.data()),
		&(biInput->bmiHeader),
		frame->data[0],
		0,
		0,
		frame->pts,
		frameBuffer.size(),
		1000,
		&(biOutputOld != nullptr ? biOutputOld->bmiHeader : biOutput->bmiHeader),
		keyframe ? NULL : reinterpret_cast<LPVOID>(oldFrameBuffer.data())
	);
	if (err == ICERR_OK) {
		// Store old information.
		std::memcpy(oldFrameBuffer.data(), frameBuffer.data(), oldFrameBuffer.size());

		*received_packet = true;
		packet->keyframe = keyframe;
		packet->dts = packet->pts = frame->pts;
		packet->size = oldFrameBuffer.size();
		packet->data = reinterpret_cast<uint8_t*>(oldFrameBuffer.data());
		return true;
	} else {
		PLOG_ERROR("Unable to encode: %s.",
			FormattedICCError(err).c_str());
		return false;
	}
}

obs_properties_t* VFW::Encoder::get_properties(void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(data);

	obs_properties_t* pr = obs_properties_create();
	obs_properties_set_param(pr, data, nullptr);
	obs_property_t* p;

	p = obs_properties_add_int(pr, PROP_BITRATE, "Bitrate", 1, 1000000, 1);
	p = obs_properties_add_float(pr, PROP_QUALITY, "Quality", 1, 100, 0.01);
	p = obs_properties_add_float(pr, PROP_KEYFRAME_INTERVAL, "Keyframe Interval", 0.01, 30.00, 0.01);
	p = obs_properties_add_button(pr, PROP_CONFIGURE, "Configure", cb_configure);
	p = obs_properties_add_button(pr, PROP_ABOUT, "About", cb_about);

	return pr;
}

bool VFW::Encoder::cb_configure(obs_properties_t *pr, obs_property_t *p, void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));

	return false;
}

bool VFW::Encoder::cb_about(obs_properties_t *pr, obs_property_t *p, void *data) {
	VFW::Info* info = static_cast<VFW::Info*>(obs_properties_get_param(pr));


	return false;
}

bool VFW::Encoder::update(void *data, obs_data_t *settings) {
	return static_cast<VFW::Encoder*>(data)->update(settings);
}

bool VFW::Encoder::update(obs_data_t* settings) {
	return false;
}

bool VFW::Encoder::get_extra_data(void *data, uint8_t **extra_data, size_t *size) {
	return static_cast<VFW::Encoder*>(data)->get_extra_data(extra_data, size);
}

bool VFW::Encoder::get_extra_data(uint8_t** extra_data, size_t* size) {
	return false;
}

bool VFW::Encoder::get_sei_data(void *data, uint8_t **sei_data, size_t *size) {
	return static_cast<VFW::Encoder*>(data)->get_sei_data(sei_data, size);
}

bool VFW::Encoder::get_sei_data(uint8_t** sei_data, size_t* size) {
	return false;
}

void VFW::Encoder::get_video_info(void *data, struct video_scale_info *info) {
	return static_cast<VFW::Encoder*>(data)->get_video_info(info);
}

void VFW::Encoder::get_video_info(struct video_scale_info *info) {
	info->format = VIDEO_FORMAT_BGRX;
}
