#include "tcp_receiver.hh"

#include <algorithm>
#include <cstdint>
#include <utility>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reader().set_error();
    return;
  }

  if ( reader().has_error() ) {
    return;
  }

  if ( message.SYN && !zero_point_.has_value() ) {
    zero_point_ = message.seqno;
  }

  if ( !zero_point_.has_value() ) {
    return;
  }

  const uint64_t checkpoint = writer().bytes_pushed() + 1;
  const uint64_t absolute_seqno = message.seqno.unwrap( *zero_point_, checkpoint );
  const uint64_t payload_seqno = absolute_seqno + message.SYN; // 绝对序号

  if ( payload_seqno == 0 ) {
    return;
  }

  // 绝对序号转换为字节流索引
  reassembler_.insert( payload_seqno - 1, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage message {};

  if ( zero_point_.has_value() ) {
    const uint64_t absolute_ackno = writer().bytes_pushed() + 1 + writer().is_closed();
    message.ackno = Wrap32::wrap( absolute_ackno, *zero_point_ );
  }

  message.window_size = static_cast<uint16_t>( min<uint64_t>( writer().available_capacity(), UINT16_MAX ) );
  message.RST = reader().has_error();
  return message;
}
