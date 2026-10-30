#include <cstdint>
#include <iostream>

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // Your code here.
  uint32_t ip = next_hop.ipv4_numeric();

  if ( arp_cache_.find( ip ) != arp_cache_.end() ) {
    EthernetFrame frame;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.header.src = ethernet_address_;
    frame.header.dst = arp_cache_[ip].mac;
    frame.payload = serialize( dgram );
    transmit( frame );
  } else {
    wait_transmit_[ip].push( dgram );

    if ( arp_request_age_.find( ip ) != arp_request_age_.end() ) {
      return;
    }

    ARPMessage request {};
    request.opcode = ARPMessage::OPCODE_REQUEST;
    request.sender_ethernet_address = ethernet_address_;
    request.sender_ip_address = ip_address_.ipv4_numeric();
    // request.target_ethernet_address 未知
    request.target_ip_address = next_hop.ipv4_numeric();

    EthernetFrame frame {};
    frame.header.type = EthernetHeader::TYPE_ARP;
    frame.header.src = ethernet_address_;
    frame.header.dst = ETHERNET_BROADCAST;
    frame.payload = serialize( request );

    arp_request_age_[ip] = 0;
    transmit( frame );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST )
    return;

  switch ( frame.header.type ) {
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram dgram {};
      if ( !parse( dgram, frame.payload ) )
        return;
      datagrams_received_.push( std::move( dgram ) );
      break;
    }
    case EthernetHeader::TYPE_ARP: {
      ARPMessage message;
      if ( !parse( message, frame.payload ) )
        return;
      arp_cache_[message.sender_ip_address] = ARPEntry { message.sender_ethernet_address, 0 };
      if ( message.opcode == ARPMessage::OPCODE_REQUEST
           && message.target_ip_address == ip_address_.ipv4_numeric() ) {
        ARPMessage reply {};
        reply.opcode = ARPMessage::OPCODE_REPLY;
        reply.sender_ethernet_address = ethernet_address_;
        reply.sender_ip_address = ip_address_.ipv4_numeric();
        reply.target_ethernet_address = message.sender_ethernet_address;
        reply.target_ip_address = message.sender_ip_address;

        EthernetFrame reply_frame {};
        reply_frame.header.type = EthernetHeader::TYPE_ARP;
        reply_frame.header.src = ethernet_address_;
        reply_frame.header.dst = message.sender_ethernet_address;
        reply_frame.payload = serialize( reply );
        transmit( reply_frame );
      }
      // 重发消息
      auto it = wait_transmit_.find( message.sender_ip_address );
      if ( it != wait_transmit_.end() ) {
        auto& pending = it->second;

        while ( !pending.empty() ) {
          EthernetFrame wait_frame {};
          wait_frame.header.type = EthernetHeader::TYPE_IPv4;
          wait_frame.header.src = ethernet_address_;
          wait_frame.header.dst = message.sender_ethernet_address;
          wait_frame.payload = serialize( pending.front() );

          transmit( wait_frame );
          pending.pop();
        }
        wait_transmit_.erase( it );
      }
      break;
    }
    default:
      break;
  };
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  for ( auto it = arp_cache_.begin(); it != arp_cache_.end(); ) {
    it->second.cache_ms += ms_since_last_tick;

    if ( it->second.cache_ms >= 30000 ) {
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }

  // ARP 重传
  for ( auto it = arp_request_age_.begin(); it != arp_request_age_.end(); ) {
    it->second += ms_since_last_tick;
    if ( it->second >= 5000 ) {
      it = arp_request_age_.erase( it );
    } else {
      ++it;
    }
  }
  return;
}
