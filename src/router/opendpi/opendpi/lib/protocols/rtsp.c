/*
 * rtsp.c
 * Copyright (C) 2009-2010 by ipoque GmbH
 * 
 * This file is part of OpenDPI, an open source deep packet inspection
 * library based on the PACE technology by ipoque GmbH
 * 
 * OpenDPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * OpenDPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenDPI.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */


#include "ipq_protocols.h"

#ifdef IPOQUE_PROTOCOL_RTSP
#ifndef IPOQUE_PROTOCOL_RTP
#error RTSP requires RTP detection to work correctly
#endif
#ifndef IPOQUE_PROTOCOL_RTSP
#error RTSP requires RTSP detection to work correctly
#endif
#ifndef IPOQUE_PROTOCOL_RDP
#error RTSP requires RDP detection to work correctly
#endif

/* this function deals with UDP connections */
static void ipoque_search_rdt_connection(struct ipoque_detection_module_struct
										 *ipoque_struct)
{
	struct ipoque_packet_struct *packet = &ipoque_struct->packet;
	struct ipoque_id_struct *src = ipoque_struct->src;


	IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found UDP\n");


	if (src != NULL) {
		// UDP packets, check in case of timeout, bitmask, packet length and payload -> search the RDT Request which has the type 0xff03
		if (src->rtsp_ts_set == 1
			&& ((IPOQUE_TIMESTAMP_COUNTER_SIZE) (packet->tick_timestamp - src->rtsp_timer)) <
			ipoque_struct->rtsp_connection_timeout) {
			if (ipq_packet_dst_ip_eql(packet, &src->rtsp_ip_address)
				&& IPOQUE_COMPARE_PROTOCOL_TO_BITMASK(src->detected_protocol_bitmask, IPOQUE_PROTOCOL_RTSP) != 0) {
				if (packet->payload_packet_len == 3 && packet->payload[0] == 0x00 && packet->payload[1] == 0xff
					&& packet->payload[2] == 0x03) {
					IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found RTSP RDT.\n");
					ipq_connection_detected(ipoque_struct, IPOQUE_PROTOCOL_RTSP);
					return;
				}
			}
		} else {
			src->rtsp_ts_set = 0;
		}
	}

	IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "didn't find RDT stream.\n");
	return;
}

/* this function searches for a rtsp-"handshake" over tcp or udp. */
static void ipoque_search_rtsp_tcp_udp(struct ipoque_detection_module_struct
								*ipoque_struct)
{
	struct ipoque_packet_struct *packet = &ipoque_struct->packet;
	struct ipoque_flow_struct *flow = ipoque_struct->flow;
	struct ipoque_id_struct *src = ipoque_struct->src;
	struct ipoque_id_struct *dst = ipoque_struct->dst;

	// in case of rtsp control flow, update timestamp from time to time
	if (flow->detected_protocol == IPOQUE_PROTOCOL_RTSP && flow->rtsp_control_flow == 1) {
		IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "RTSP control flow update timestamp.\n");
		if (dst != NULL) {
			ipq_packet_src_ip_get(packet, &dst->rtsp_ip_address);
			dst->rtsp_timer = packet->tick_timestamp;
			dst->rtsp_ts_set = 1;
		}
		if (src != NULL) {
			ipq_packet_dst_ip_get(packet, &src->rtsp_ip_address);
			src->rtsp_timer = packet->tick_timestamp;
			src->rtsp_ts_set = 1;
		}
		return;
	}

	if (flow->rtsprdt_stage == 0) {
		flow->rtsprdt_stage = 1 + packet->packet_direction;

		if (packet->udp != NULL) {
			/*this function checks if it concerns a rtsp-data-transfer over udp. */
			ipoque_search_rdt_connection(ipoque_struct);
			if (packet->detected_protocol == IPOQUE_PROTOCOL_RTSP && flow->rtsp_control_flow == 0) {
				return;
			}
		}

		IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "maybe handshake 1; need next packet.\n");
		return;
	}

	if (flow->packet_counter < 3 && flow->rtsprdt_stage == 1 + packet->packet_direction) {

		IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "maybe handshake 2; need next packet.\n");
		return;
	}

	if (packet->payload_packet_len > 20 && flow->rtsprdt_stage == 2 - packet->packet_direction) {

		// RTSP Server Message
		if (memcmp(packet->payload, "RTSP/1.0 ", 9) == 0) {


			IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found RTSP/1.0 .\n");

			if (dst != NULL) {
				IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found dst.\n");
				ipq_packet_src_ip_get(packet, &dst->rtsp_ip_address);
				dst->rtsp_timer = packet->tick_timestamp;
				dst->rtsp_ts_set = 1;
			}
			if (src != NULL) {
				IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found src.\n");
				ipq_packet_dst_ip_get(packet, &src->rtsp_ip_address);
				src->rtsp_timer = packet->tick_timestamp;
				src->rtsp_ts_set = 1;
			}
			IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "found RTSP.\n");
			flow->rtsp_control_flow = 1;
			ipq_connection_detected(ipoque_struct, IPOQUE_PROTOCOL_RTSP);
			return;
		}
	}
	if (packet->udp != NULL && packet->detected_protocol == IPOQUE_PROTOCOL_UNKNOWN && flow->packet_counter < 5) {
		IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "maybe RTSP RTP; need next packet.\n");
		return;
	}

	IPQ_LOG(IPOQUE_PROTOCOL_RTSP, ipoque_struct, IPQ_LOG_DEBUG, "didn't find handshake, exclude.\n");
	IPOQUE_ADD_PROTOCOL_TO_BITMASK(flow->excluded_protocol_bitmask, IPOQUE_PROTOCOL_RTSP);
	return;
}


#endif
