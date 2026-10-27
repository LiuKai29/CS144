#include "byte_stream.hh"
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <sys/types.h>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  return closed_;
}

void Writer::push( string data )
{

  if ( closed_ ) {
    return;
  }

  uint64_t capacity_remaining = capacity_ - buffer_.length();
  uint64_t len = min( capacity_remaining, data.length() );
  buffer_.append( data, 0, len );
  bytes_pushed_ += len;
  return;
}

void Writer::close()
{
  closed_ = true;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_.length();
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

bool Reader::is_finished() const
{
  return closed_ && buffer_.empty();
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}

string_view Reader::peek() const
{
  return string_view { buffer_ };
}

void Reader::pop( uint64_t len )
{
  len = min( len, buffer_.size() );
  buffer_.erase( 0, len );
  bytes_popped_ += len;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.length();
}
