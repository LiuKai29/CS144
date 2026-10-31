#include "router.hh"
#include "ipv4_datagram.hh"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routing_table_.push_back( { route_prefix, prefix_length, next_hop, interface_num } );
  return;
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( const auto& network_interface : _interfaces ) {
    auto& received = network_interface->datagrams_received();
    while ( !received.empty() ) {
      InternetDatagram dgram = std::move( received.front() );
      received.pop();

      if ( dgram.header.ttl <= 1 )
        continue;
      else {
        dgram.header.ttl--;
        dgram.header.compute_checksum();
      }

      uint32_t dst = dgram.header.dst;
      optional<RouteEntry> best_route;
      // 找到最长 prefix
      for ( const auto& entry : routing_table_ ) {
        const uint32_t route_prefix = entry.route_prefix;
        const uint8_t prefix_length = entry.prefix_length;

        bool match = prefix_length == 0 || dst >> ( 32 - prefix_length ) == route_prefix >> ( 32 - prefix_length );
        if ( match ) {
          if ( !best_route.has_value() )
            best_route = entry;
          else {
            if ( prefix_length > best_route->prefix_length )
              best_route = entry;
          }
        }
      }
      if ( !best_route.has_value() )
        continue;

      // 发送数据包
      const Address next_hop = best_route->next_hop.value_or( Address::from_ipv4_numeric( dgram.header.dst ) );
      interface( best_route->interface_num )->send_datagram( dgram, next_hop );
    }
  }
  return;
}
