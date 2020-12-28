#pragma once
#define WIN32_INTEROP_TYPES_H
extern "C" {
	//#include <Win32_Interop/win32fixes.h>
#include <eXosip2/eXosip.h>
}
#include <winsock2.h>
#include "UDPClient.h"
#include "NaluProvider.h"
#include <string>
#include <vector>
#include <functional>
#include "Message.h"
#include <thread>
#include <chrono>


//----exosip----//

#pragma comment(lib, "eXosip.lib")
#pragma comment(lib, "libcares.lib")
#pragma comment(lib, "osip2.lib")
#pragma comment(lib, "osipparser2.lib")

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Qwave.lib")
#pragma comment(lib, "delayimp.lib")



class Device {

	char deviceId[128] = { 0 };

	char videoChannelId[128] = { 0 };

	char server_sip_id[128] = { 0 };

	char  server_sip_realm[10] = { 0 };

	char server_ip[128] = { 0 };

	int server_port;

	char password[128] = { 0 };

	char  local_ip[128] = { 0 };

	int local_port;

	eXosip_t * sip_context = nullptr;

	NaluProvider* nalu_provider = nullptr;
	uint32_t _ssrc = 0;


public:
	Device(const char * deviceId, const char * server_sip_id, const char * server_ip, int server_port, const char * password,
		NaluProvider* nalu_provider) {
		memcpy(this->deviceId, deviceId, strlen(deviceId));
		memcpy(this->videoChannelId, deviceId, strlen(deviceId));
		memcpy(this->server_sip_id, server_sip_id, strlen(server_sip_id));
		memcpy(server_sip_realm, server_sip_id, 10);
		memcpy(this->server_ip, server_ip, strlen(server_ip));
		this->server_port = server_port;
		memcpy(this->password, password, strlen(password));
		this->nalu_provider = nalu_provider;
	}
	void start_sip_client(int local_port);

	void stop_sip_client();

	int list_index = 0;

	void set_callback(std::function<void(int index, Message msg)> callback);

	~Device() {
		is_pushing = false;
		is_runing = false;

		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

private:

	void process_request();

	void send_response_ok(eXosip_event_t *evt);

	void send_response(eXosip_event_t *evt, osip_message_t * message);

	osip_message_t * create_request();
	
	void send_request(osip_message_t * request);

	bool register_success;

	bool is_tcp;

	const char * target_ip;

	int target_port;

	int listen_port;

	UDPClient* udp_client = nullptr;

	bool is_pushing;

	bool is_runing;

	int callId = -1;

	int dialogId = -1;

	void push_task();

	void heartbeat_task();

	std::function<void(int index, Message msg)> callback;
	
};
