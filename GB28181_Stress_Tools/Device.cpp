#pragma once
#include <iostream>
#include <thread>
#include "Device.h"
#include "pugixml.hpp"
#include <vector>
#include <sstream>
#include "gb28181_header_maker.h"


using namespace std;

static int start_port = 40000;
static int SN_MAX = 99999999;
static int sn;
static int get_port() {
	start_port++;
	return start_port;
}
static int get_sn() {
	if (sn >= SN_MAX) {
		sn = 0;
	}
	sn++;
	return sn;
}

void Device::heartbeat_task() {
	while (register_success) {
		stringstream ss;
		ss << "<?xml version=\"1.0\"?>\r\n";
		ss << "<Notify>\r\n";
		ss << "<CmdType>Keepalive</CmdType>\r\n";
		ss << "<SN>" << get_sn() << "</SN>\r\n";
		ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
		ss << "<Status>OK</Status>\r\n";
		ss << "</Notify>\r\n";

		osip_message_t* request = create_request();
		if (request != NULL) {
			osip_message_set_content_type(request, "Application/MANSCDP+xml");
			osip_message_set_body(request, ss.str().c_str(), strlen(ss.str().c_str()));
			send_request(request);
		}

		std::this_thread::sleep_for(std::chrono::seconds(60));
	}
	
}
void Device::push_task(){
	udp_client = new UDPClient(is_tcp);

	int status = is_tcp ? udp_client->bind(local_ip, listen_port,target_ip, target_port):udp_client->bind(local_ip, listen_port);

	if (0 != status) {
		cout << "client bind port failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"�󶨱��ض˿ڻ�������ʧ��" });
		}
		if (callId != -1 && dialogId != -1) {
			eXosip_lock(sip_context);
			int ret = eXosip_call_terminate(sip_context, callId, dialogId);
			eXosip_unlock(sip_context);
		}
		return;
	}
	is_pushing = true;

	char ps_header[PS_HDR_LEN];

	char ps_system_header[SYS_HDR_LEN];

	char ps_map_header[PSM_HDR_LEN];

	char pes_header[PES_HDR_LEN];

	char rtp_header[RTP_HDR_LEN];

	int time_base = 90000;
	int fps = 25;
	int send_packet_interval = 1000 / fps;

	int interval = time_base / fps;
	long pts = 0;

	char frame[1024 * 128];

	int single_packet_max_length = 1400;

	char rtp_packet[RTP_HDR_LEN+1400];

	int ssrc = _ssrc;
	int rtp_seq = 0;

	Nalu *nalu = new Nalu();
	nalu->packet = (char *)malloc(1024*128);
	nalu->length = 1024 * 128;


	extern std::vector<Nalu*> nalu_vector;
	size_t size = nalu_vector.size();

	while (is_pushing)
	{

		
	for (int i = 0; i < size; i++) {
		if (!is_pushing) {
			break;
		}
		if (!nalu_provider->get_nalu(i, nalu)) {
			continue;
		}

		NaluType  type = nalu->type;
		int length = nalu->length;
		char * packet = nalu->packet;

		int index = 0;
		if (NALU_TYPE_IDR == type) {

			 gb28181_make_ps_header(ps_header, pts);

			 memcpy(frame,ps_header,PS_HDR_LEN);
			 index += PS_HDR_LEN;

			 gb28181_make_sys_header(ps_system_header, 0x3f);

			 memcpy(frame+ index, ps_system_header, SYS_HDR_LEN);
			 index += SYS_HDR_LEN;

			 gb28181_make_psm_header(ps_map_header);

			 memcpy(frame + index, ps_map_header, PSM_HDR_LEN);
			 index += PSM_HDR_LEN;

		}
		else {
			gb28181_make_ps_header(ps_header, pts);

			memcpy(frame, ps_header, PS_HDR_LEN);
			index += PS_HDR_LEN;
		}

		//��װpes
		gb28181_make_pes_header(pes_header,0xe0, length,pts,pts);

		memcpy(frame+index, pes_header, PES_HDR_LEN);
		index += PES_HDR_LEN;


		memcpy(frame + index, packet, length);
		index += length;

		//���rtp

		int rtp_packet_count = ((index - 1) / single_packet_max_length) + 1;

		for (int i = 0; i < rtp_packet_count; i++) {

			gb28181_make_rtp_header(rtp_header,rtp_seq,pts,ssrc, i == (rtp_packet_count - 1));

			int writed_count = single_packet_max_length;

			if ((i + 1)*single_packet_max_length > index) {
				writed_count = index - (i* single_packet_max_length);
			}
			//��Ӱ����ֽ�
			int rtp_start_index=0;

			unsigned short rtp_packet_length = RTP_HDR_LEN + writed_count;
			if (is_tcp) {
				unsigned char  packt_length_ary[2];
				packt_length_ary[0] = (rtp_packet_length >> 8) & 0xff;
				packt_length_ary[1] = rtp_packet_length & 0xff;
				memcpy(rtp_packet, packt_length_ary, 2);
				rtp_start_index = 2;
			}
			memcpy(rtp_packet+ rtp_start_index,rtp_header,RTP_HDR_LEN);
			memcpy(rtp_packet+ +rtp_start_index + RTP_HDR_LEN,frame+ (i* single_packet_max_length), writed_count);
			rtp_seq++;

			if (is_pushing) {
				udp_client->send_packet(target_ip, target_port, rtp_packet, rtp_start_index + rtp_packet_length);
			}
			else {
				if (nalu != nullptr) {
					delete nalu;
					nalu = nullptr;
				}
				return;
			}
		}

		pts += interval;
		/*ULONGLONG end_time = GetTickCount64();
		ULONGLONG dis = end_time - start_time;
		if (dis < send_packet_interval) {
			std::this_thread::sleep_for(std::chrono::milliseconds(send_packet_interval -dis));
		}*/
		std::this_thread::sleep_for(std::chrono::milliseconds(38));

		}
	}

	try {
		if (udp_client != nullptr) {
			udp_client->release();
			delete udp_client;
			udp_client = nullptr;
		}
		if (nalu != nullptr) {
			delete nalu;
			nalu = nullptr;
		}
		is_pushing = false;
		//if (!is_runing) {
		//	delete this;
		//}
	}
	catch (exception& e) {
		std::cout << "catch runtime error. " << e.what() << std::endl;
	}
	
}

void Device::send_request(osip_message_t * request) {
	eXosip_lock(sip_context);
	eXosip_message_send_request(sip_context, request);
	eXosip_unlock(sip_context);
}

osip_message_t* Device::create_request() {

	osip_message_t* request = NULL;

	char fromSip[256] = { 0 };
	char toSip[256] = { 0 };

	if (!is_runing) {
		return nullptr;
	}

	sprintf(fromSip,"<sip:%s@%s:%d>", deviceId, local_ip,local_port);

	sprintf(toSip,"<sip:%s@%s:%d>", server_sip_id, server_ip, server_port);

	int status = eXosip_message_build_request(sip_context,
		&request, "MESSAGE", toSip, fromSip, NULL);
	if (OSIP_SUCCESS != status) {
		cout << "build requests failed" << endl;
	}

	return request;

}

void Device::send_response(eXosip_event_t *evt, osip_message_t * message) {
	eXosip_lock(sip_context);
	eXosip_message_send_answer(sip_context, evt->tid, 200, message);
	eXosip_unlock(sip_context);
}

void Device::send_response_ok(eXosip_event_t *evt) {
	osip_message_t * message = evt->request;
	eXosip_message_build_answer(sip_context, evt->tid, 200, &message);
	send_response(evt, message);
}
void Device::process_request() {
	eXosip_event_t *evt;

	while (is_runing)
	{
		evt = eXosip_event_wait(sip_context, 0, 100);
		if (!is_runing) {
			break;
		}
		//eXosip_lock(sip_context);
		//eXosip_automatic_action(sip_context);
		//eXosip_unlock(sip_context);
		if (evt == NULL) {
			continue;
		}
		//std::cout << "evt_type" << evt->type << std::endl;
		switch (evt->type){

		case EXOSIP_MESSAGE_NEW:
			 if (MSG_IS_MESSAGE(evt->request)) {
				osip_body_t *body = NULL;
				osip_message_get_body(evt->request, 0, &body);
				if (body != NULL) {
					printf("request >>> %s", body->body);
				}
				
				send_response_ok(evt);

				pugi::xml_document doucument;
				if (!doucument.load(body->body)) {
					cout << "load xml failed" << endl;
					break;
				}
				pugi::xml_node root_node = doucument.first_child();

				if (!root_node) {
					cout << "root node get failed" << endl;
					break;
				}
				string root_name = root_node.name();
				if ("Query" == root_name) {
					pugi::xml_node cmd_node = root_node.child("CmdType");
					

					if (!cmd_node) {
						cout << "root node get failed" << endl;
						break;
					}

					pugi::xml_node sn_node = root_node.child("SN");
					string  cmd = cmd_node.child_value();
					if ("Catalog" == cmd) {
						stringstream ss;
						ss << "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
						ss << "<Response>\r\n";
						ss << "<CmdType>Catalog</CmdType>\r\n";
						ss << "<SN>" << sn_node.child_value() << "</SN>\r\n";
						ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
						ss << "<SumNum>" << 1 << "</SumNum>\r\n";
						ss << "<DeviceList Num=\"" << 1 << "\">\r\n";
						ss << "<Item>\r\n";
						ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
						ss << "<Name>IPC</Name>\r\n";
						ss << "<Status>ON</Status>\r\n";
						ss << "<ParentID>" << server_sip_id << "</ParentID>\r\n";
						ss << "</Item>\r\n";
						ss << "</DeviceList>\r\n";
						ss << "</Response>\r\n";
						osip_message_t* request = create_request();
						if (request != NULL) {
							osip_message_set_content_type(request, "Application/MANSCDP+xml");
							osip_message_set_body(request, ss.str().c_str(), strlen(ss.str().c_str()));
							send_request(request);
						}
					}
					else if ("RecordInfo" == cmd) {
						//processRecordInfo(root_note);
					}

				}
			}
			else if (MSG_IS_BYE(evt->request)) {

				std::cout << "accept bye" << std::endl;

			}
			break;
		case EXOSIP_REGISTRATION_SUCCESS:
			cout << "ע��ɹ�" << endl;
			{
			if (callback != nullptr) {
				callback(list_index, Message{ STATUS_TYPE ,"ע��ɹ�" });
			}
			register_success = true;
			thread heartbeat_task_thread(&Device::heartbeat_task,this);
			heartbeat_task_thread.detach();
			}
			break;
		case EXOSIP_REGISTRATION_FAILURE:
			register_success = false;
			if (evt->response == NULL) {
				return;
			}
			if (401 == evt->response->status_code) {
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"ע�� 401, �����֤��Ϣ����ע����..." });
				}
				osip_www_authenticate_t* www_authenticate_header;

				osip_message_get_www_authenticate(evt->response, 0, &www_authenticate_header);

				osip_message_t* reg = NULL;
				int32_t result = -1;
			
				eXosip_lock(sip_context);
				eXosip_clear_authentication_info(sip_context);
				//struct eXosip_t *excontext, const char *username, const char *userid, const char *passwd, const char *ha1, const char *realm
				result = eXosip_add_authentication_info(sip_context, deviceId, deviceId, password, "MD5", www_authenticate_header->realm);
				if (result != 0) {
					std::cout << "eXosip_add_authentication_info failed." << std::endl;
					return;
				}

				result = eXosip_register_build_register(sip_context, evt->rid, 3600, &reg);
				if (result != 0) {
					std::cout << "eXosip_register_build_register failed." << std::endl;
					return;
				}

				result = eXosip_register_send_register(sip_context, evt->rid, reg);
				eXosip_unlock(sip_context);
				if (0 != result){
					std::cout << "eXosip_register_send_register authorization error!" << std::endl;
					return;
				}
				std::cout << "eXosip_register_send_register authorization success!" << std::endl;
			}
			else {
				if (callback) {
					callback(list_index, Message{ STATUS_TYPE, "ע��ʧ��!" });
				}
			}
			
			break;
		case EXOSIP_CALL_ACK:
			//������
			cout << "���յ�ack����ʼ����" << endl;
			callId = evt->cid;
			dialogId =  evt->did;

			if(udp_client != nullptr){
				udp_client->release();
				delete udp_client;
				udp_client = nullptr;
			}else {
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"��ʼ����" });
				}
				thread t(&Device::push_task, this);
				t.detach();
			}
			break;
		case EXOSIP_CALL_CLOSED:
			callId = -1;
			dialogId = -1;
			if (callback != nullptr) {
				callback(list_index, Message{ STATUS_TYPE ,"��������" });
			}
			cout << "���յ�Bye����������" << endl;
			is_pushing = false;
			if (udp_client != nullptr) {
				udp_client->release();
				delete udp_client;
				udp_client = nullptr;
			}
			break;
		case EXOSIP_CALL_INVITE:
			cout << "call" << endl;
			//����sdp
			osip_body_t *sdp_body = NULL;
			osip_message_get_body(evt->request, 0, &sdp_body);
			if (sdp_body != NULL) {
				printf("request >>> %s", sdp_body->body);
			}
			else {
				cout << "sdp error" << endl;
				break;
			}
			sdp_message_t * sdp = NULL;

			if (OSIP_SUCCESS != sdp_message_init(&sdp)) {
				cout << "sdp_message_init failed" << endl;
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"sdp_message_init failed" });
				}
				break;
			}
			if (OSIP_SUCCESS != sdp_message_parse(sdp, sdp_body->body)) {
				cout << "sdp_message_parse failed" << endl;
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"sdp_message_parse failed" });
				}
				break;
			}
			sdp_connection_t * connect = eXosip_get_video_connection(sdp);
			sdp_media_t * media = eXosip_get_video_media(sdp);
			target_ip = connect->c_addr;
			target_port = atoi(media->m_port);
			char * protocol = media->m_proto;
			is_tcp = strstr(protocol,"TCP");
			
			if (callback != nullptr) {
				char port[5];
				snprintf(port,5,"%d",target_port);
				callback(list_index, Message{ PULL_STREAM_PROTOCOL_TYPE ,is_tcp?"TCP":"UDP"});
				callback(list_index, Message{ PULL_STREAM_PORT_TYPE ,port});
			}
			int ssrc = 0;
			char  ssrc_c[10] = { 0 };
			char * ssrc_address = strstr(sdp_body->body, "y=");
			if (ssrc_address) {
				char * end_address = strstr(ssrc_address, "\r\n");
				size_t length = end_address - ssrc_address;
				memcpy(ssrc_c, ssrc_address + 2, length+1);
				ssrc = atoi(ssrc_c);
				_ssrc = ssrc;
			}
			//����200_ok
			listen_port = get_port();
			char port[10];
			snprintf(port, 10, "%d", listen_port);
			if (callback != nullptr) {
				callback(list_index, Message{ PULL_STREAM_PORT_TYPE ,port });
			}
			stringstream ss;
			ss << "v=0\r\n";
			ss << "o=" << videoChannelId << " 0 0 IN IP4 " << local_ip << "\r\n";
			ss << "s=Play\r\n";
			ss << "c=IN IP4 " << local_ip << "\r\n";
			ss << "t=0 0\r\n";
			if (!is_tcp) {
				ss << "m=video " << listen_port << " TCP/RTP/AVP 96\r\n";
			}
			else {
				ss << "m=video " << listen_port << " RTP/AVP 96\r\n";
			}
			ss << "a=sendonly\r\n";
			ss << "a=rtpmap:96 PS/90000\r\n";
			ss << "y=" << _ssrc << "\r\n";
			ss << "f=" << "\r\n";
			string sdp_str  = ss.str();


			size_t size = sdp_str.size();

			osip_message_t * message = evt->request;
			int status = eXosip_call_build_answer(sip_context, evt->tid, 200, &message);

			if (status != 0) {
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"�ظ�inviteʧ��" });
				}
				break;
			}
			
			osip_message_set_content_type(message, "APPLICATION/SDP");
			osip_message_set_body(message, sdp_str.c_str(), sdp_str.size());

			eXosip_call_send_answer(sip_context, evt->tid, 200, message);

			cout << "reply sdp " << sdp_str.c_str() << endl;
			break;
			}

		if (evt != NULL) {
			eXosip_event_free(evt);
			evt = NULL;
		}
	}
	if (sip_context != NULL) {
		eXosip_quit(sip_context);
		sip_context = NULL;
	}
	is_runing = false;
	//if (!is_pushing) {
	//	delete this;
	//}
}

void Device::start_sip_client(int local_port) {
	this->local_port = local_port;
	sip_context =  eXosip_malloc();

	if (OSIP_SUCCESS !=  eXosip_init(sip_context)) {
		cout << "sip init failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"exo_sip init failed"});
		}
		return;
	}

	if (OSIP_SUCCESS != eXosip_listen_addr(sip_context, IPPROTO_UDP, NULL, local_port, AF_INET, 0)) {
		cout << "sip bind port failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"sip bind port failed" });
		}
		eXosip_quit(sip_context);
		sip_context = nullptr;
		return;
	}
	is_runing = true;
	thread th(&Device::process_request, this);
	th.detach();

	char from_uri[128] = { 0 };
	char proxy_uri[128] = { 0 };
	char contact[128] = { 0 };

	eXosip_guess_localip(sip_context,AF_INET, local_ip,128);
	//������<>��������ȻeXosip_register_build_initial_register ��һֱ���� -5���﷨����
	sprintf(from_uri,"sip:%s@%s:%d",deviceId, local_ip, local_port);
	sprintf(contact,"sip:%s@%s:%d",deviceId,local_ip,local_port);
	//sprintf(contact,"<sip:%s@%s:%d>",deviceId,server_ip,server_port);
	sprintf(proxy_uri,"sip:%s@%s:%d",server_sip_id,server_ip,server_port);

	eXosip_clear_authentication_info(sip_context);


	osip_message_t * register_message = NULL;
	//struct eXosip_t *excontext, const char *from, const char *proxy, const char *contact, int expires, osip_message_t ** reg
	int register_id = eXosip_register_build_initial_register(sip_context, from_uri, proxy_uri, contact,3600,&register_message);
	if (register_message == NULL) {
		cout << "eXosip_register_build_initial_register failed" << endl;
		return;
	}
	//��ǰ��������֤��Ϣ������ϢΪ401������eXosip_automatic_action()�Զ�����
	eXosip_add_authentication_info(sip_context, deviceId, deviceId, password, "MD5", NULL);
	eXosip_lock(sip_context);
	eXosip_register_send_register(sip_context, register_id, register_message);
	eXosip_unlock(sip_context);
	if (callback != nullptr) {
		callback(list_index, Message{ STATUS_TYPE ,"����ע����Ϣ" });
	}
}

void Device::stop_sip_client() {
	is_pushing = false;
	is_runing = false;
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
void Device::set_callback(std::function<void(int index, Message msg)> callback) {
	this->callback = std::move(callback);
}
