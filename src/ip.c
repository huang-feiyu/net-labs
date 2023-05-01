#include "ip.h"
#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "net.h"

/**
 * @brief 处理一个收到的数据包
 *
 * @param buf 要处理的数据包
 * @param src_mac 源mac地址
 */
void ip_in(buf_t *buf, uint8_t *src_mac) {
  ip_hdr_t *ip_hdr = (ip_hdr_t *) buf->data;

  // 1. Check length
  if (buf->len < sizeof(ip_hdr_t)) {
    return;
  }

  // 2. Check IP version and length
  if (ip_hdr->version != IP_VERSION_4 || swap16(ip_hdr->total_len16) > buf->len) {
    return;
  }

  // 3. checksum
  uint16_t tmp_checksum = ip_hdr->hdr_checksum16;
  ip_hdr->hdr_checksum16 = 0;
  if (tmp_checksum != checksum16((uint16_t *) ip_hdr, ip_hdr->hdr_len * IP_HDR_LEN_PER_BYTE)) {
    // checksum failed
    return;
  }
  ip_hdr->hdr_checksum16 = tmp_checksum;

  // 4. Check IP
  if (memcmp(net_if_ip, ip_hdr->dst_ip, NET_IP_LEN) != 0) {
    return;
  }

  // 5. Remove padding if needed
  if (buf->len > swap16(ip_hdr->total_len16)) {
    buf_remove_padding(buf, buf->len - swap16(ip_hdr->total_len16));
  }

  // 6. Remove header
  buf_remove_header(buf, ip_hdr->hdr_len * IP_HDR_LEN_PER_BYTE);

  // 7. Transfer to above as well as check whether can handle with the protocol
  uint8_t src_ip[NET_IP_LEN];
  memcpy(src_ip, ip_hdr->src_ip, NET_IP_LEN);
  uint8_t protocol = ip_hdr->protocol;
  if (!(protocol == NET_PROTOCOL_ICMP || protocol == NET_PROTOCOL_UDP)) {
    icmp_unreachable(buf, src_ip, ICMP_CODE_PROTOCOL_UNREACH);
    return;
  }
  net_in(buf, protocol, src_ip);
}

/**
 * @brief 处理一个要发送的ip分片
 *
 * @param buf 要发送的分片
 * @param ip 目标ip地址
 * @param protocol 上层协议
 * @param id 数据包id
 * @param offset 分片offset，必须被8整除
 * @param mf 分片mf标志，是否有下一个分片
 */
void ip_fragment_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol, int id,
                     uint16_t offset, int mf) {
  // 1. Add header in buffer
  buf_add_header(buf, sizeof(ip_hdr_t));

  // 2. Fill up ip header
  ip_hdr_t *ip_hdr = (ip_hdr_t *) buf->data;
  ip_hdr->version = IP_VERSION_4;
  ip_hdr->hdr_len = 5;
  ip_hdr->tos = 0;
  ip_hdr->total_len16 = swap16(buf->len);
  ip_hdr->id16 = swap16(id);
  ip_hdr->flags_fragment16 = swap16(mf ? IP_MORE_FRAGMENT | offset : offset);
  ip_hdr->ttl = IP_DEFALUT_TTL;
  ip_hdr->protocol = protocol;
  ip_hdr->hdr_checksum16 = 0;

  memcpy(ip_hdr->src_ip, net_if_ip, NET_IP_LEN);
  memcpy(ip_hdr->dst_ip, ip, NET_IP_LEN);

  // 3. Compute checksum
  uint16_t hdr_checksum = checksum16((uint16_t *) ip_hdr, sizeof(ip_hdr_t));
  ip_hdr->hdr_checksum16 = hdr_checksum;

  // 4. Encapsulate and send out
  arp_out(buf, ip);
}

/**
 * @brief 处理一个要发送的ip数据包
 *
 * @param buf 要处理的包
 * @param ip 目标ip地址
 * @param protocol 上层协议
 */
void ip_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol) {
  const int MTU = ETHERNET_MAX_TRANSPORT_UNIT - 20;

  static uint16_t ip_id = 0;

  // 1. Check whether need to package
  if (buf->len <= MTU) {
    ip_fragment_out(buf, ip, protocol, ip_id++, 0, 0);
    return;
  }

  // 2. Send as packages
  uint16_t cur = 0;
  buf_t *fragment = (buf_t *) malloc(sizeof(buf_t));
  while (buf->len > MTU) {
    buf_init(fragment, MTU);
    memcpy(fragment->data, buf->data, MTU);
    buf_remove_header(buf, MTU);
    ip_fragment_out(fragment, ip, protocol, ip_id, cur / IP_HDR_OFFSET_PER_BYTE, 1);
    cur += MTU;
  }

  // 3. The last package
  if (buf->len > 0) {
    buf_init(fragment, buf->len);
    memcpy(fragment->data, buf->data, buf->len);
    buf_remove_header(buf, buf->len);
    ip_fragment_out(fragment, ip, protocol, ip_id, cur / IP_HDR_OFFSET_PER_BYTE, 0);
  }
  ip_id++;
}

/**
 * @brief 初始化ip协议
 *
 */
void ip_init() { net_add_protocol(NET_PROTOCOL_IP, ip_in); }
