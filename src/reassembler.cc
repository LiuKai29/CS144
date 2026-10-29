#include "reassembler.hh"

#include "byte_stream.hh"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <linux/if.h>
#include <utility>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( output_.writer().is_closed() ) {
    return;
  }

  const uint64_t substring_end = first_index + data.size();
  if ( is_last_substring ) {
    end_index_ = substring_end;
  }

  const uint64_t next_index = output_.writer().bytes_pushed();
  const uint64_t window_end = next_index + output_.writer().available_capacity();
  const uint64_t accepted_begin = max( first_index, next_index );
  const uint64_t accepted_end = min( substring_end, window_end );

  if ( accepted_begin < accepted_end ) {
    insert_pending( accepted_begin, data.substr( accepted_begin - first_index, accepted_end - accepted_begin ) );
  }

  flush_pending();
}

// 维护 pending_ 状态
// pending_ 一定满足:
// 片段保持在可写入范围内 [output_.writer().bytes_pushed(),output_.writer().bytes_pushed() +
// output_.writer().available_capacity())
// 所有片段一定不重叠也不相邻
// 每个片段非空 
void Reassembler::insert_pending( uint64_t first_index, string data )
{
  auto it = pending_.upper_bound( first_index );

  if ( it != pending_.begin() && prev( it )->first + prev( it )->second.size() >= first_index ) {
    --it;
    const uint64_t existing_end = it->first + it->second.size();
    const uint64_t new_end = first_index + data.size();
    if ( new_end > existing_end ) {
      it->second.append( data, existing_end - first_index, new_end - existing_end );
    }
  } else {
    it = pending_.emplace_hint( it, first_index, move( data ) );
  }

  auto following = next( it ); // 向右合并所有重叠或相邻的片段
  while ( following != pending_.end() && following->first <= it->first + it->second.size() ) {
    const uint64_t merged_end = it->first + it->second.size();
    const uint64_t following_end = following->first + following->second.size();
    if ( following_end > merged_end ) {
      it->second.append( following->second, merged_end - following->first, following_end - merged_end );
    }
    following = pending_.erase( following ); // erase 返回后面的迭代器
  }
}

void Reassembler::flush_pending()
{
  while ( !pending_.empty() && pending_.begin()->first == output_.writer().bytes_pushed() ) {
    auto it = pending_.begin();
    output_.writer().push( move( it->second ) );
    pending_.erase( it );
  }

  if ( end_index_.has_value() && output_.writer().bytes_pushed() == *end_index_ ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  uint64_t bytes_pending = 0;
  for ( const auto& [first_index, data] : pending_ ) {
    bytes_pending += data.size();
  }

  return bytes_pending;
}
