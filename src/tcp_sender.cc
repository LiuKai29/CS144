#include "tcp_sender.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <algorithm>
#include <cstdint>
#include <limits>

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return next_seqno_ - acked_seqno_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  const uint64_t window_size = max<uint64_t>( msg_receive_.window_size, 1 );

  while ( !fin_sent_ && sequence_numbers_in_flight() < window_size ) {
    const uint64_t remaining = window_size - sequence_numbers_in_flight();

    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap( next_seqno_, isn_ );
    msg.SYN = ( next_seqno_ == 0 );
    msg.RST = input_.has_error();

    // 不能超过 MAX_PAYLOAD_SIZE
    const uint64_t payload_size = min<uint64_t>( remaining - msg.SYN, TCPConfig::MAX_PAYLOAD_SIZE );
    read( input_.reader(), payload_size, msg.payload );

    // 窗口为 FIN 预留位置
    msg.FIN = input_.reader().is_finished() && msg.sequence_length() < remaining;

    if ( msg.sequence_length() == 0 ) {
      break;
    }

    next_seqno_ += msg.sequence_length();
    fin_sent_ = msg.FIN;
    outstanding_messages_.push( { msg, next_seqno_ } );

    if ( !timer_running_ ) {
      timer_running_ = true;
      timer_elapsed_ms_ = 0;
    }

    transmit( msg );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_seqno_, isn_ );
  msg.RST = input_.has_error();
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  uint64_t ackno = acked_seqno_;
  if ( msg.ackno.has_value() ) {
    ackno = msg.ackno->unwrap( isn_, next_seqno_ );
    if ( ackno > next_seqno_ ) {
      return; // 如果 ackno 确认了未发送的内容
    }
  }

  msg_receive_ = msg;
  if ( ackno <= acked_seqno_ ) {
    return; // 重复发送的 ackno
  }

  acked_seqno_ = ackno;
  while ( !outstanding_messages_.empty() && outstanding_messages_.front().end_seqno <= acked_seqno_ ) {
    outstanding_messages_.pop();
  }

  current_RTO_ms_ = initial_RTO_ms_;
  timer_elapsed_ms_ = 0;
  timer_running_ = !outstanding_messages_.empty();
  consecutive_retransmissions_ = 0;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( !timer_running_ ) {
    return;
  }

  if ( ms_since_last_tick < current_RTO_ms_ - timer_elapsed_ms_ ) {
    timer_elapsed_ms_ += ms_since_last_tick;
    return;
  }

  TCPSenderMessage msg = outstanding_messages_.front().message;
  msg.RST = input_.has_error();

  if ( msg_receive_.window_size != 0 ) {
    ++consecutive_retransmissions_;
    current_RTO_ms_ = current_RTO_ms_ << 1;
  }

  // 重新计时
  timer_elapsed_ms_ = 0;
  transmit( msg );
}
