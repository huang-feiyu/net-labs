#include "ethernet.h"
#include "arp.h"
#include "driver.h"
#include "ip.h"
#include "utils.h"
/**
 * @brief 处理一个收到的数据包
 *
 * @param buf 要处理的数据包
 */
void ethernet_in(buf_t *buf) {}
/**
 * @brief 处理一个要发送的数据包
 *
 * @param buf 要处理的数据包
 * @param mac 目标MAC地址
 * @param protocol 上层协议
 */
void ethernet_out(buf_t *buf, const uint8_t *mac, net_protocol_t protocol) {
  // 1. padding if less than 46
  if (buf->len < ETHERNET_MIN_TRANSPORT_UNIT) {
    buf_add_padding(buf, ETHERNET_MIN_TRANSPORT_UNIT - buf->len);
  }

  // 2. add ethernet header
  buf_add_header(buf, sizeof(ether_hdr_t)); // allocate header & move data ptr
  ether_hdr_t *hdr = (ether_hdr_t *)buf->data;

  // 3. destination MAC address
  memcpy(hdr->dst, mac, NET_MAC_LEN);

  // 4. source MAC address
  memcpy(hdr->src, net_if_mac, NET_MAC_LEN);

  // 5. protocol type
  hdr->protocol16 = swap16(protocol);

  // 6. send data frame to driver
  driver_send(buf);
}
/**
 * @brief 初始化以太网协议
 *
 */
void ethernet_init() {
  buf_init(&rxbuf, ETHERNET_MAX_TRANSPORT_UNIT + sizeof(ether_hdr_t));
}

/**
 * @brief 一次以太网轮询
 *
 */
void ethernet_poll() {
  if (driver_recv(&rxbuf) > 0)
    ethernet_in(&rxbuf);
}
