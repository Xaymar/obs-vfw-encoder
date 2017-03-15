#include "enc-vfw.h"
#include "libobs/obs-encoder.h"

#include <list>
#include <vector>
#include <map>

std::map<std::string, VFW::Info> _IdToInfo;

bool VFW::Initialize() {
	// Initialize all VFW Encoders (we can only use one anyway)
	ICINFO icinfo;
	std::memset(&icinfo, 0, sizeof(ICINFO));
	icinfo.dwSize = sizeof(icinfo);

	DWORD fccType = 0;
	for (size_t i = 0; ICInfo(fccType, i, &icinfo); i++) {
		HIC hIC = ICOpen(icinfo.fccType, icinfo.fccHandler, ICMODE_QUERY);
		if (hIC) {
			if (ICGetInfo(hIC, &icinfo, sizeof(icinfo))) {
				std::vector<char> idBuf(64);
				snprintf(idBuf.data(), idBuf.size(), "%ls", icinfo.szName);
				std::vector<char> nameBuf(1024);
				snprintf(nameBuf.data(), nameBuf.size(), "%ls (" PLUGIN_NAME ")", icinfo.szDescription);

				// Track
				VFW::Info info;
				info.Id = std::string(idBuf.data());
				info.Name = std::string(nameBuf.data());
				info.icInfo = icinfo;

				// Register
				std::memset(&info.obsInfo, 0, sizeof(obs_encoder_info));
				info.obsInfo.id = info.Id.data();
				info.obsInfo.type = OBS_ENCODER_VIDEO;
				info.obsInfo.codec = "vidc";
				info.obsInfo.type_data = &info; // circular reference but whatever, it's not reference counted
				info.obsInfo.get_name = VFW::Encoder::get_name;
				info.obsInfo.create = VFW::Encoder::create;
				info.obsInfo.destroy = VFW::Encoder::destroy;
				info.obsInfo.encode = VFW::Encoder::encode;
				info.obsInfo.get_properties = VFW::Encoder::get_properties;
				info.obsInfo.update = VFW::Encoder::update;
				info.obsInfo.get_extra_data = VFW::Encoder::get_extra_data;
				info.obsInfo.get_sei_data = VFW::Encoder::get_sei_data;
				info.obsInfo.get_video_info = VFW::Encoder::get_video_info;

				PLOG_INFO("%s %s",
					info.Id.data(),
					info.Name.data());

				obs_register_encoder(&info.obsInfo);
				_IdToInfo.insert(std::make_pair(info.Id, info));
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

}

void* VFW::Encoder::create(obs_data_t *settings, obs_encoder_t *encoder) {
	return new VFW::Encoder(settings, encoder);
}

VFW::Encoder::Encoder(obs_data_t *settings, obs_encoder_t *encoder) {

}

void VFW::Encoder::destroy(void* data) {
	delete static_cast<VFW::Encoder*>(data);
}

VFW::Encoder::~Encoder() {

}

bool VFW::Encoder::encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	return static_cast<VFW::Encoder*>(data)->encode(frame, packet, received_packet);
}

bool VFW::Encoder::encode(struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	return false;
}

obs_properties_t* VFW::Encoder::get_properties(void *data) {
	return static_cast<VFW::Encoder*>(data)->get_properties();
}

obs_properties_t* VFW::Encoder::get_properties() {
	obs_properties_t* pr = obs_properties_create();

	return pr;
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

void VFW::Encoder::get_video_info(struct video_scale_info *info) {}
