#include "arp.h"
#include "ethernet.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
/**
 * @brief 初始的arp包
 *
 */
static const arp_pkt_t arp_init_pkt = {.hw_type16 = constswap16(ARP_HW_ETHER),
                                       .pro_type16 =
                                           constswap16(NET_PROTOCOL_IP),
                                       .hw_len = NET_MAC_LEN,
                                       .pro_len = NET_IP_LEN,
                                       .sender_ip = NET_IF_IP,
                                       .sender_mac = NET_IF_MAC,
                                       .target_mac = {0}};

/**
 * @brief arp地址转换表，<ip,mac>的容器
 *
 */
map_t arp_table;

/**
 * @brief arp buffer，<ip,buf_t>的容器
 *
 */
map_t arp_buf;

/**
 * @brief 打印一条arp表项
 *
 * @param ip 表项的ip地址
 * @param mac 表项的mac地址
 * @param timestamp 表项的更新时间
 */
void arp_entry_print(void *ip, void *mac, time_t *timestamp) {
  printf("%s | %s | %s\n", iptos(ip), mactos(mac), timetos(*timestamp));
}

/**
 * @brief 打印整个arp表
 *
 */
void arp_print() {
  printf("===ARP TABLE BEGIN===\n");
  map_foreach(&arp_table, arp_entry_print);
  printf("===ARP TABLE  END ===\n");
}

/**
 * @brief 发送一个arp请求
 *
 * @param target_ip 想要知道的目标的ip地址
 */
void arp_req(uint8_t *target_ip) {
  // 1. init txbuf
  buf_init(&txbuf, sizeof(arp_pkt_t));

  // 2. fill up ARP header
  arp_pkt_t arp_pkt = arp_init_pkt;
  arp_pkt.hw_type16 = swap16(ARP_HW_ETHER);
  arp_pkt.pro_type16 = swap16(NET_PROTOCOL_IP);
  arp_pkt.hw_len = NET_MAC_LEN;
  arp_pkt.pro_len = NET_IP_LEN;
  arp_pkt.opcode16 = swap16(ARP_REQUEST);
  memcpy(arp_pkt.sender_mac, net_if_mac, NET_MAC_LEN);
  memcpy(arp_pkt.sender_ip, net_if_ip, NET_IP_LEN);
  memcpy(arp_pkt.target_ip, target_ip, NET_IP_LEN);

  memcpy(txbuf.data, &arp_pkt, sizeof(arp_pkt_t));

  // 3. send out ARP packet with broadcast address
  ethernet_out(&txbuf, ether_broadcast_mac, NET_PROTOCOL_ARP);
}

/**
 * @brief 发送一个arp响应
 *
 * @param target_ip 目标ip地址
 * @param target_mac 目标mac地址
 */
void arp_resp(uint8_t *target_ip, uint8_t *target_mac) {
  // TO-DO
}

/**
 * @brief 处理一个收到的数据包
 *
 * @param buf 要处理的数据包
 * @param src_mac 源mac地址
 */
void arp_in(buf_t *buf, uint8_t *src_mac) {
  // 1. check length
  if (buf->len < sizeof(arp_pkt_t)) {
    return;
  }

  // 2. check if header is intact
  arp_pkt_t *arp_pkt = (arp_pkt_t *)buf->data;
  if (arp_pkt->hw_type16 != swap16(ARP_HW_ETHER) ||
      arp_pkt->pro_type16 != swap16(NET_PROTOCOL_IP) ||
      arp_pkt->hw_len != NET_MAC_LEN || arp_pkt->pro_len != NET_IP_LEN ||
      (arp_pkt->opcode16 != swap16(ARP_REQUEST) &&
       arp_pkt->opcode16 != swap16(ARP_REPLY))) {
    return;
  }

  // 3. update ARP entry
  map_set(&arp_table, arp_pkt->sender_ip, src_mac);

  // 4. check if there is correspond arp_buf
  void *ip_buf = map_get(&arp_buf, arp_pkt->sender_ip);

  // send to 'sender' with MAC
  if (ip_buf != NULL) {
    ethernet_out(ip_buf, src_mac, NET_PROTOCOL_IP);
    map_delete(&arp_buf, arp_pkt->sender_ip);
    return;
  }

  // check if we need to response self MAC
  if (arp_pkt->pro_type16 == swap16(ARP_REQUEST) &&
      memcmp(arp_pkt->target_ip, net_if_ip, NET_IP_LEN * sizeof(uint8_t)) ==
          0) {
    arp_resp(arp_pkt->sender_ip, src_mac);
  }
}

/**
 * @brief 处理一个要发送的数据包
 *
 * @param buf 要处理的数据包
 * @param ip 目标ip地址
 * @param protocol 上层协议
 */
void arp_out(buf_t *buf, uint8_t *ip) {
  // 1. search in arp_table
  void *mac = map_get(&arp_table, ip);

  // 2. check entry
  if (mac != NULL) {
    ethernet_out(buf, mac, NET_PROTOCOL_IP);
    return;
  }

  // 3. check is there a packet
  if (map_get(&arp_buf, ip) == NULL) {
    map_set(&arp_buf, ip, buf);
    arp_req(ip);
  }
}

/**
 * @brief 初始化arp协议
 *
 */
void arp_init() {
  map_init(&arp_table, NET_IP_LEN, NET_MAC_LEN, 0, ARP_TIMEOUT_SEC, NULL);
  map_init(&arp_buf, NET_IP_LEN, sizeof(buf_t), 0, ARP_MIN_INTERVAL, buf_copy);
  net_add_protocol(NET_PROTOCOL_ARP, arp_in);
  arp_req(net_if_ip);
}
